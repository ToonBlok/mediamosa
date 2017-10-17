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
          $job['transcode_parameters'] = serialize($this->get_job_parameters($job));

          break;
      }

      mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'Toon - STILL' );

      // ###
      // Only for debug purpose
      $transcode = new transcode();
      $transcode->start($job);
      // Set job in progress.
//      $this->set_job_status($job, mediamosa_job_db::JOB_STATUS_INPROGRESS, '0.000');
      $this->running_job_update($job);

      return;
      // ###
      $this->send($job);
    }
  }


  // Toon: Is modelled exactly after the real method
  function get_job_parameters($job) {
    $job_id = $job['job_id'];
    $mediafile_id = $job['mediafile_id'];
    $job_type = $job['job_type'];


    $result = array();

    switch ($job_type) {

      case mediamosa_job_db::JOB_TYPE_TRANSCODE:
        // Get job transcode.
        $job_transcode = mediamosa_job_transcode::get($job_id);

        $result['profile_id'] = $job_transcode[mediamosa_job_transcode_db::TRANSCODE_PROFILE_ID];
        $result['tool'] = $job_transcode[mediamosa_job_transcode_db::TOOL];
        $result['file_extension'] = $job_transcode[mediamosa_job_transcode_db::FILE_EXTENSION];
        $result['command'] = mediamosa_job_scheduler::map_parameters($job_transcode[mediamosa_job_transcode_db::TOOL], $job_transcode[mediamosa_job_transcode_db::COMMAND], $mediafile_id);
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

  /**
   * Check and map profile parameters.
   *
   * @param string $tool
   *   Name of the tool.
   * @param string $command
   *   The command string.
   * @param string $mediafile_id
   *   The mediafile ID.
   */
  public static function map_parameters($tool, $command, $mediafile_id) {
    // Result commands.
    $commands_mapped = array();

    // Into array.
    $commands = mediamosa_transcode_profile::commandToArray($command);

    // First determine if we have to alter the aspect ratio.
    $alter_size_param = (isset($commands['maintain_aspect_ratio']) && $commands['maintain_aspect_ratio'] == 'yes' && isset($commands['size']));

    // Get all parameters based on the toolname that can be queried.
    //
    // Check the given paramters.
    $tool_params = mediamosa_tool_params::get_by_tool($tool);
    foreach ($tool_params as $tool_param) {
      if ($tool_param[mediamosa_tool_params_db::NICE_PARAMETER] == 'size' && $alter_size_param) {
        // We assume it exists.
        $target_size = $commands['size'];
      }
      else {
        $mapped = self::mapping_value(
          $commands,
          $tool_param[mediamosa_tool_params_db::NICE_PARAMETER],
          $tool_param[mediamosa_tool_params_db::TOOL_PARAMETER],
          $tool_param[mediamosa_tool_params_db::MIN_VALUE],
          $tool_param[mediamosa_tool_params_db::MAX_VALUE],
          $tool_param[mediamosa_tool_params_db::ALLOWED_VALUE],
          $tool_param[mediamosa_tool_params_db::DEFAULT_VALUE],
          $tool_param[mediamosa_tool_params_db::REQUIRED],
          $tool_param[mediamosa_tool_params_db::TYPE_PARAMETER]
        );

        // Store it?
        if (!empty($mapped) && $mapped['value'] !== '') {
          $commands_mapped[$mapped['name']] = $mapped['value'];
        }
      }
    }

    // Adjust for aspect ratio? Makes only sense if size is set, else we
    // convert to size same as source (default ffmpeg behaviour).
    if ($alter_size_param) {

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

      // Determine if padding required or not.
      $use_padding = !(isset($commands['padding']) && $commands['padding'] == 'no');

      // Get the parameter string.
      $ratio = mediamosa_tool_ffmpeg::calcAspectRatio($metadata[mediamosa_asset_mediafile_metadata::WIDTH], $metadata[mediamosa_asset_mediafile_metadata::HEIGHT], $target_size, TRUE, NULL, NULL, $use_padding);
      if (!empty($ratio)) {
        $commands_mapped = array_merge($commands_mapped, $ratio);
      }
    }

    // Convert from array to string.
    return mediamosa_transcode_profile::arrayToCommand($commands_mapped);
  }

  // ################################################ REMOVE LATER
  /**
   * Update the job.
   *
   * Updates running jobs.
   *
   * 1. Get possible status file and parse it.
   * 2. When transcode or still job is done, files will be moved to the
   *    correct location.
   */
  function running_job_update($job_server_job) {

    // Job type.
    $job_type = $job_server_job[mediamosa_job_server_db::JOB_TYPE];

    // Only update these types.
    switch ($job_type) {
      case mediamosa_job_server_db::JOB_TYPE_TRANSCODE:
      case mediamosa_job_server_db::JOB_TYPE_STILL:
        break;

      default:
        return;
    }

    // Job ID.
    $job_id = $job_server_job[mediamosa_job_server_db::JOB_ID];

    // Source Mediafile ID.
    $mediafile_id_src = $job_server_job[mediamosa_job_server_db::MEDIAFILE_ID_SRC];

    // Read the contents of the status file in an array.
    $job_status = mediamosa_job_server::get_status_contents($job_id);

    // Default status.
    $status = mediamosa_job_db::JOB_STATUS_INPROGRESS;

    switch ($job_type) {
      case mediamosa_job_server_db::JOB_TYPE_TRANSCODE:
        // No status file found, we just have to wait.
        if (empty($job_status['Status'])) {
          mediamosa_job_server::log_debug_mediafile($mediafile_id_src, "No status file found with @statusfile for job @job_id, maybe next run.", array('@statusfile' => mediamosa_storage::get_realpath_status_file($job_id), '@job_id' => $job_id));
          return;
        }

        if ($job_status['Status'] == 'done' && $job_status['Errors'] == 'none') {
          // Status to finished.
          $status = mediamosa_job_db::JOB_STATUS_FINISHED;

          // Store the transcode.
          mediamosa_job_server::store_new_mediafile($job_id);

          // Log it.
//          mediamosa_job_server::log_mediafile($mediafile_id_src, 'End job @job_type, Job ID: @job_id, status: @status', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status));

          // Set job status.

          $this->set_job_status($job_server_job, $status, $job_status['Progress']);

          // Generate event transcode finished.
          mediamosa::rules_invoke_event('mediamosa_event_transcode_finished', $mediafile_id_src);
        }
        elseif ($job_status['Status'] == 'error' && (empty($job_status['Errors']) || $job_status['Errors'] != 'none')) {
          $status = mediamosa_job_server_db::JOB_STATUS_FAILED;
          $link_asset = mediamosa_job_server::get_asset_link($job_id);

          mediamosa_job_server::log_mediafile($mediafile_id_src, "End @job_type job, Job ID @job_id, with status: @status<br /><br />@link", array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status, '@link' => $link_asset));
          mediamosa_job_server::log_mediafile($mediafile_id_src, "Info @job_type job, Job ID @job_id, status file '@statusfile'", array('@job_type' => $job_type, '@job_id' => $job_id, '@statusfile' => mediamosa_job_server::get_status_contents($job_id, TRUE)));

          // Set status to failed.
          $this->set_job_status($job_server_job, $status, $job_status['Progress'],  isset($job_status["ffmpeg-output"]) ? ($job_status["Errors"] != "" ? $job_status["Errors"] . "-\n" : '') . $job_status["ffmpeg-output"] : $job_status["Errors"]);

          // Generate event transcode failed.
          mediamosa::rules_invoke_event('mediamosa_event_transcode_failed', $mediafile_id_src);
        }
        else {
          // Set job status.
          $this->set_job_status($job_server_job, $status, $job_status['Progress']);
        }
        break;

      case mediamosa_job_server_db::JOB_TYPE_STILL:
        // Scene still filename.
        $file_scene = mediamosa_storage::get_uri_scene_file($job_id);

        if (!mediamosa_io::file_exists($file_scene) && empty($job_status)) {
          // No status file found, we just have to wait.
          mediamosa_job_server::log_debug_mediafile($mediafile_id_src, 'No status file found with name @name for job @job_id, maybe next run.', array('@name' => mediamosa_storage::get_realpath_status_file($job_id), '@job_id' => $job_id));
          return;
        }

        // Set defaults, to fix some possible notices.
        $job_status += array(
          'Status' => '',
          'Errors' => 'none',
          'Progress' => '0.000',
        );

        if (mediamosa_io::file_exists($file_scene) || ($job_status['Status'] == 'done' && $job_status['Errors'] == 'none')) {
          $status = mediamosa_job_server::store_new_still($job_id, $mediafile_id_src);

          if ($status == mediamosa_job_server_db::JOB_STATUS_INPROGRESS) {
            mediamosa_job_server::log_debug_mediafile($mediafile_id_src, 'Running @job_type job (storing file busy), Job ID @job_id, with status: @status', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status));
          }
          else {
            mediamosa_job_server::log_debug_mediafile($mediafile_id_src, 'End @job_type job, Job ID @job_id, with status: @status', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status));
          }
        }
        elseif ($job_status['Status'] == 'error' && $job_status['Errors'] != 'none') {
          $status = mediamosa_job_server_db::JOB_STATUS_FAILED;
          $link_asset = mediamosa_job_server::get_asset_link($job_id);
          mediamosa_job_server::log_debug_mediafile($mediafile_id_src, 'End @job_type job, Job ID @job_id, with status: @status<br /><br />@link', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status, '@link' => $link_asset));
          mediamosa_job_server::log_debug_high_mediafile($mediafile_id_src, "Info @job_type job, Job ID @job_id, status file '@statusfile'", array('@job_type' => $job_type, '@job_id' => $job_id, '@statusfile' => mediamosa_job_server::get_status_contents($job_id, TRUE)));
        }

        // Update the status.
        if (!mediamosa_io::file_exists($file_scene) && $job_status['Errors'] != 'none') {
          // Might be because there is no status file, dont bother to update.
          if (isset($job_status['Errors'])) {
            // ############# TEMPORARILY BLOCKED TOON
            $this->set_job_status($job_server_job, $status, $job_status['Progress'], $job_status['Errors']);
            // ############# TEMPORARILY BLOCKED TOON
          }
        }
        else {
          // ############# TEMPORARILY BLOCKED TOON
          $this->set_job_status($job_server_job, $status, $job_status['Progress']);
          // ############# TEMPORARILY BLOCKED TOON
        }

        break;
    }
  }

  function set_job_status($job, $job_status, $progress, $error_description = '', $error_description_args = array()) {
    $job_id = $job['job_id'];
    $mediafile_id = $job['mediafile_id'];

    // Set args in description.
    if (!empty($error_description_args)) {
      $error_description = strtr($error_description, $error_description_args);
    }

    $fields = array(
      mediamosa_job_db::JOB_STATUS => $job_status,
      mediamosa_job_db::PROGRESS => is_null($progress) ? '0.000' : $progress,
    );

    switch ($job_status) {
      case mediamosa_job_db::JOB_STATUS_FINISHED:
      case mediamosa_job_db::JOB_STATUS_FAILED:
      case mediamosa_job_db::JOB_STATUS_CANCELLED:
        $fields[mediamosa_job_db::FINISHED] = mediamosa_datetime::utc_current_timestamp_now(TRUE);
        break;
    }

    // Check if its started.
//    $jobserver_job = self::get_with_jobid($job_id);
//    if (!$jobserver_job) {
//      self::log('Fatal: trying to update job with ID; @job_id', array('@job_id' => $job_id));
//      assert(0);
//      return;
//    }

    // Invalidate technical metadata if analyse fails.
    if ($job_status == mediamosa_job_server_db::JOB_STATUS_FAILED && $job[mediamosa_job_db::JOB_TYPE] == mediamosa_job_db::JOB_TYPE_ANALYSE) {
      // Delete metadata, its no longer valid when analyse fails.
      mediamosa_asset_mediafile_metadata::delete_by_mediafileid($mediafile_id);
    }

    // Set status.
    if ($job[mediamosa_job_db::JOB_STATUS] == mediamosa_job_db::JOB_STATUS_WAITING && $job_status == mediamosa_job_db::JOB_STATUS_INPROGRESS) {
      $fields[mediamosa_job_db::STARTED] = mediamosa_datetime::utc_current_timestamp_now(TRUE);
    }

    if (!empty($error_description)) {
      $fields[mediamosa_job_db::ERROR_DESCRIPTION] = $error_description;
    }

    // Update.
    mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
      ->fields($fields)
      ->condition(mediamosa_job_db::ID, $job_id)
      ->execute();
  }
}