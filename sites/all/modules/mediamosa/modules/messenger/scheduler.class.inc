<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class scheduler {

  function __construct()
  {
    mediamosa_debug::log('Construct class ' . __CLASS__, array(), 'Toon');

    // Who exactly calls scheduler doesn't matter for now
    // Boot the servers here

    // if
    $this->boot_servers();
  }

  // één stap te ver
  function boot_servers(){
    // 1: Check if servers are already booted
    // 1.1: Check which servers should be online

    // ...
    // 1.2: Check if those servers are online
    // If servers online
    //   skip
    // else:
    //   start servers (set their value on ON)

    //$servers = $this->get_enabled_job_processor();
  }

  function get_enabled_job_processor(array $enabled_status = array(mediamosa_server_db::SERVER_STATUS_ON, mediamosa_server_db::SERVER_STATUS_CLOSE)) {
    return $this->get_enabled(array(mediamosa_server_db::SERVER_TYPE_JOB_PROCESSOR), $enabled_status);
  }

  function get_enabled(array $server_types = array(), array $enabled_status = array(mediamosa_server_db::SERVER_STATUS_ON, mediamosa_server_db::SERVER_STATUS_CLOSE)) {
    $query = mediamosa_db::db_select(mediamosa_server_db::TABLE_NAME, 'ms')
      ->fields('ms');

    // Any specific types?
    if (!empty($server_types)) {
      $query->condition(mediamosa_server_db::SERVER_TYPE, $server_types, 'IN');
    }

    if (!empty($enabled_status)) {
      $query->condition(mediamosa_server_db::SERVER_STATUS, $enabled_status, 'IN');
    }

    return $query
      ->orderBy(mediamosa_server_db::SERVER_TYPE)
      ->execute();
  }

  function send($job) {
    $this->open_connection();

    $this->channel->exchange_declare('jobs', 'direct', false, false, false);
    $this->channel->queue_declare('ANALYSE', false, true, false, false);
    //$this->channel->queue_bind($this->server_type, 'jobs', $this->server_type);

    $msg = new AMQPMessage(
      serialize($job),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    # Send the message.
    //echo 'sent with routing_key: ' . $job['job_type'];
    $this->channel->basic_publish($msg, 'jobs', $job['job_type']);

    $this->close_connection();
  }

  function variable_get($name, $default = NULL) {
    return variable_get($name, $default);
  }

  function variable_set($name, $value) {
    variable_set($name, $value);
  }

  function open_connection() {

    $host = $this->variable_get('mediamosa_scheduler_host', 'localhost');
    $port = $this->variable_get('mediamosa_scheduler_port', 5672);
    $username = $this->variable_get('mediamosa_scheduler_username', 'guest');
    $password = $this->variable_get('mediamosa_scheduler_password', 'guest');

    $this->connection = new AMQPStreamConnection($host, $port, $username, $password);
    $this->channel = $this->connection->channel();
  }

  function close_connection() {
    $this->channel->close();
    $this->connection->close();
  }

  function round_up_jobs(){
    mediamosa_debug::log('Start method ' . __METHOD__, array(), 'Toon');

    $sql = 'SELECT job_id, job_type, asset_id, mediafile_id 
            FROM {#mediamosa_job} AS mj
            WHERE mj.#job_status = :status';

    $sql = strtr($sql, array(
      '#job_status' => mediamosa_job_db::JOB_STATUS,
      '#mediamosa_job' => mediamosa_job_db::TABLE_NAME
    ));

    $jobs = mediamosa_db::db_query($sql,
      array(
        ':status' => mediamosa_job_db::JOB_STATUS_WAITING
      )
    )->fetchAll();

    foreach ($jobs as $job) {

      switch ($job['job_type']) {
        case mediamosa_job_db::JOB_TYPE_STILL:
          $job_parameters = $this->get_job_parameters($job);
          $job['blackstill_check'] = $job_parameters['blackstill_check'];
          $job['still_parameters'] = $job_parameters['still_parameters'];
          $job['size'] = $job_parameters['size'];
          break;
        case mediamosa_job_db::JOB_TYPE_TRANSCODE:
          mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'Toon - TRANSCODE' );
          $job_parameters = $this->get_job_parameters($job);
//          $this->get_transcode_exec($job['job_id'], $job['mediafile_id']);
          break;
      }

      mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'Toon - STILL' );

      $this->send($job);
    }
  }

  /**
   * Generate the string that is used for the vpx_transcode script.
   *
   * @param string $jobserver_job_id
   * @param string $mediafile_id
   */
  function get_transcode_exec($jobserver_job_id, $mediafile_id) {

    // Get it.
    $job_server_transcode = mediamosa_job_server_transcode::get($jobserver_job_id);
    mediamosa_debug::log_export($job_server_transcode, WATCHDOG_NOTICE, 'Toon - TRANSCODE' );

    if (empty($job_server_transcode)) {
      mediamosa_job_server::log_mediafile($mediafile_id, 'Transcode job not found, jobserver_id: @jobserver_id', array('@jobserver_id' => $jobserver_job_id));
      return '';
    }

    $tool = $job_server_transcode[mediamosa_job_server_transcode_db::TOOL];
    $file_extension = $job_server_transcode[mediamosa_job_server_transcode_db::FILE_EXTENSION];
    $command = $job_server_transcode[mediamosa_job_server_transcode_db::COMMAND];

    // Get the mediafile.
    $mediafile = mediamosa_asset_mediafile::must_exists($mediafile_id);

    // Create parameter string from the command.
    $commands = mediamosa_transcode_profile::commandToArray($command);

    // Build the parameter string.
    $parameters = array();
    foreach ($commands as $name => $value) {
      if ($value == mediamosa_tool_params_db::ALLOWED_VALUE_FOR_SWITCH) {
        $value = '';
      }
      $parameters[] = $name . ' ' . $value;
    }

    // Rebuild.
    $parameter_string = implode(' ', $parameters);

    // The name of the status file is '<job_id>.status'.
    $job_server = mediamosa_job_server::get($jobserver_job_id);
    $status_file = mediamosa_storage::get_realpath_status_file($job_server[mediamosa_job_server_db::JOB_ID]);

    // Empty.
    $execution_string = '';

    // Combine based on selection.
    switch ($tool) {
      // TODO: move to tool.
      case 'ffmpeg':
        $parameter_string = trim($parameter_string);
        if (!empty($parameter_string)) {
          $parameter_string = escapeshellarg($parameter_string);
        }

        $execution_string = sprintf('%s %s %s %s %s %s %s > /dev/null &', mediamosa_settings::lua_transcode_script(), mediamosa_storage::get_local_mediafile_path($mediafile), mediamosa_storage::get_realpath_temporary(), $mediafile_id, $job_server[mediamosa_job_server_db::JOB_ID], $file_extension, $parameter_string);
        break;

      default:
        // Now check if for this tool the hook exists.
        $class_name = 'mediamosa_tool_' . $tool;

        // FIXME:
        // This code here is first attempt to rewrite the jobs module in more
        // flexible one. In future ::generate_transcode() is called directly and
        // jobs will no longer worry about exec strings.
        // Will start moving all ffmpeg code out of core into ffmpeg tool very
        // soon.

        // Now see if transcode function is found.
        if (class_exists($class_name) && method_exists($class_name, 'get_transcode_exec')) {
          $args = array(
            // Job ID.
            'job_id' => $job_server[mediamosa_job_server_db::JOB_ID],
            // ID of mediafile to transcode.
            'mediafile_id' => $mediafile_id,
            // File extension of dest.
            'file_extension' => $file_extension,
            // Parameter string for cmd.
            'parameter_string' => $parameter_string,
            // the data dir in sannas (extra).
            'path_mount_point_data' => mediamosa_storage::get_local_mediafile_path($mediafile),
            // Path to the transcode file.
            'location_dest_file' => mediamosa_storage::get_realpath_temporary_file($job_server[mediamosa_job_server_db::JOB_ID]),
            // Location of source mediafile.
            'location_source_file' => mediamosa_storage::get_realpath_mediafile($mediafile),
            // Location of the status_file.
            'status_file' => $status_file,
          );

          // PHP 5.2.3 or higher.
          $execution_string = call_user_func($class_name . '::get_transcode_exec', $args);
        }
    }

    // Unknown.
    if (empty($execution_string)) {
      return strtr('{ echo "Status: error"; echo "Errors: Error"; } > @status', array(
        '@status' => $status_file,
      ));

      // One tool has been found. Process started. Return now.
    }

    return $execution_string;
  } // End get_transcode_exec()

  // Toon: Is modelled exactly after the real method
  function get_job_parameters($job) {
    $job_id = $job['job_id'];
    $mediafile_id = $job['mediafile_id'];
    $job_type = $job['job_type'];


    $result = array();

    switch ($job_type) {

      case mediamosa_job_db::JOB_TYPE_TRANSCODE:
        // Get job transcode.
//        $job_transcode = mediamosa_job_transcode::get($job_id);
//
//        $result['profile_id'] = $job_transcode[mediamosa_job_transcode_db::TRANSCODE_PROFILE_ID];
//        $result['tool'] = $job_transcode[mediamosa_job_transcode_db::TOOL];
//        $result['file_extension'] = $job_transcode[mediamosa_job_transcode_db::FILE_EXTENSION];
//        $result['command'] = self::map_parameters($job_transcode[mediamosa_job_transcode_db::TOOL], $job_transcode[mediamosa_job_transcode_db::COMMAND], $mediafile_id);
        break;

      case mediamosa_job_db::JOB_TYPE_STILL:
        $query_job = mediamosa_job_still::get($job_id);

        if ($query_job) {
          $result['blackstill_check'] = $query_job['blackstill_check'];
          $result['still_parameters'] = unserialize($query_job['still_parameters']);
          $result['frametime'] = $result['still_parameters']['frametime'];
          $result['h_padding'] = $result['still_parameters']['h_padding'];
          $result['v_padding'] = $result['still_parameters']['v_padding'];
          $result['tag'] = $result['still_parameters']['tag'];

          $mediafile = mediamosa_asset_mediafile::get($mediafile_id, NULL, array(mediamosa_asset_mediafile_db::APP_ID));
          $app_id = $mediafile[mediamosa_asset_mediafile_db::APP_ID];

          // Pre-defined ratios.
          $sizes = array(
            'sqcif' => '128x96', 'qcif' => '176x144', 'cif' => '352x288', '4cif' => '704x576',
            'qqvga' => '160x120', 'qvga' => '320x240', 'vga' => '640x480', 'svga' => '800x600',
            'xga' => '1024x768', 'uxga' => '1600x1200', 'qxga' => '2048x1536', 'sxga' => '1280x1024',
            'qsxga' => '2560x2048', 'hsxga' => '5120x4096', 'wvga' => '852x480', 'wxga' => '1366x768',
            'wsxga' => '1600x1024', 'wuxga' => '1920x1200', 'woxga' => '2560x1600',
            'wqsxga' => '3200x2048', 'wquxga' => '3840x2400', 'whsxga' => '6400x4096',
            'whuxga' => '7680x4800', 'cga' => '320x200', 'ega' => '640x350', 'hd360' => '640x360',
            'hd480' => '852x480', 'hd720' => '1280x720', 'hd1080' => '1920x1080',
          );

          // Find target size.
          if (isset($sizes[$query_job['size']])) {
            $target_size = $sizes[$query_job['size']];
          }
          else {
            // Use size of still parameters instead.
            $target_size = $result['still_parameters']['size'];

            // Check the size.
            if (!preg_match('/(\d+)x(\d+)/', $target_size)) {
              // If there is a still default size for the client app available,
              // then use that instead.
              $target_size = mediamosa_app::get_still_default_size($app_id);
              if (!$target_size) {
                // Get the video size.
                $target_size = mediamosa_asset_mediafile::get_size($mediafile_id);
              }
            }
          }

          // First get source width and height.
          $metadata = mediamosa_asset_mediafile_metadata::get_with_mediafileid($mediafile_id, array(
            array(
              'prop_name' => mediamosa_asset_mediafile_metadata::WIDTH,
              'type' => mediamosa_asset_mediafile_metadata_property_db::TYPE_INT,
            ),
            array(
              'prop_name' => mediamosa_asset_mediafile_metadata::HEIGHT,
              'type' => mediamosa_asset_mediafile_metadata_property_db::TYPE_INT,
            ),
          ));
          $width = $metadata[mediamosa_asset_mediafile_metadata::WIDTH];
          $height = $metadata[mediamosa_asset_mediafile_metadata::HEIGHT];
          $still_padding = mediamosa_app::get_still_padding_value($app_id);

          // Get the parameter settings.
          $calc_aspect_ratio = mediamosa_gd::calcAspectRatio($width, $height, $target_size, $result['h_padding'], $result['v_padding'], $still_padding == mediamosa_app_db::STILL_PADDING_YES);

          // Set result.
          if ($calc_aspect_ratio) {
            $result['size'] = $calc_aspect_ratio['width'] . 'x' . $calc_aspect_ratio['height'];
            $result['h_padding'] = $calc_aspect_ratio['h_padding'];
            $result['v_padding'] = $calc_aspect_ratio['v_padding'];
          }
          else {
            if ($width && $height) {
              $result['size'] = $width . 'x' . $height;
            }
            else {
              $result['size'] = '640x360';
            }
            $result['h_padding'] = 0;
            $result['v_padding'] = 0;
          }
        }
        else {
          mediamosa_debug::log('Something went wrong in the analyse script!', array(), 'Toon - STILL');
          // Something went wrong in the analyse script.
          // Fall back to the default values.
          $result['frametime'] = mediamosa_settings::STILL_DEFAULT_FRAME_TIME;
          $result['size'] = '640x360';
          $result['h_padding'] = 0;
          $result['v_padding'] = 0;
          $result['blackstill_check'] = 'FALSE';
          $result['tag'] = '';
        }
        break;

    }

    return $result;
  }

}