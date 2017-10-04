<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class jobcore{

  function __construct($server_type) {
    //$this->server_type = $server_type;
    mediamosa_debug::log('Construct class ' . __CLASS__, array(), 'Toon');
    $this->server_type = $server_type;
    $this->listen();
  }

  function listen() {
    $this->open_connection();

    $this->channel->exchange_declare('jobs', 'direct', false, false, false);
    $this->channel->queue_declare($this->server_type, false, true, false, false);
    $this->channel->queue_bind($this->server_type, 'jobs', $this->server_type);

    echo ' [*] Waiting for ' . $this->server_type . ' messages. To exit press CTRL+C', "\n";

    $callback = function($msg){
      mediamosa_debug::log('Message received in Listen() in class: ' . __CLASS__, array(), 'Toon');

      $msg_body = unserialize($msg->body);

      $debug_message = 'job_id: ' . $msg_body['job_id'] . ', job_type: ' . $msg_body['job_type'] . ' asset_id: ' . $msg_body['asset_id'] . ' mediafile_id: ' . $msg_body['mediafile_id'];
      echo ' [!] Message received: ' . $debug_message . "\n";

      echo 'Reached';
      switch ($msg_body['job_type']) {
        case mediamosa_job_db::JOB_TYPE_ANALYSE:
          $this->start_analyze($msg_body);
          break;
        case mediamosa_job_db::JOB_TYPE_STILL:
          $this->start_still($msg_body);
          //$execution_string = $this->get_generate_still_exec($jobserver_job_id, $msg_body['mediafile_id']);
          //self::log_mediafile($mediafile_id_src, 'About to start @job_type job: @job_id calling exec: @execution_string', array('@job_type' => $job_type, '@job_id' => $job_id, '@execution_string' => $execution_string));
          break;
        case mediamosa_job_db::JOB_TYPE_TRANSCODE:

          break;
      }
      //const MEDIAFILE_ID_SRC = 'mediafile_src';

      echo ' [x] Done', "\n";
      $msg->delivery_info['channel']->basic_ack($msg->delivery_info['delivery_tag']);

    };

    // basic_qos: Tells RabbitMQ not to give more than one message to a worker at a time.
    $this->channel->basic_qos(null, 1, null);
    $this->channel->basic_consume($this->server_type, '', false, false, false, false, $callback);

    while(count($this->channel->callbacks)) {
      $this->channel->wait();
    }

    $this->close_connection();

  }

  function start_still($job) {
    $var = $this->get_job_parameters($job['job_id'], $job['job_type'], $job['mediafile_id']);
    var_dump($var);

//    $still_parameters = array(
//      'still_type' => $this->get_param_value(self::STILL_TYPE),
//      'still_per_mediafile' => $this->get_param_value(self::STILL_PER_MEDIAFILE),
//      'still_every_second' => $this->get_param_value(self::STILL_EVERY_SECOND),
//      'start_frame' => $this->get_param_value(self::START_FRAME),
//      'end_frame' => $this->get_param_value(self::END_FRAME),
//      'tag' => $this->get_param_value(self::TAG),
//      'video_duration' => $this->get_param_value(self::VIDEO_DURATION),
//      'fps' => $this->get_param_value(self::FPS),
//      // Watermark.
//      self::WATERMARK_ID => $watermark_id,
//      self::WATERMARK_DST_X => $this->get_param_value(self::WATERMARK_DST_X),
//      self::WATERMARK_DST_Y => $this->get_param_value(self::WATERMARK_DST_Y),
//      self::WATERMARK_PCT => $this->get_param_value(self::WATERMARK_PCT),
//      self::WATERMARK_V_ALIGN => $this->get_param_value(self::WATERMARK_V_ALIGN),
//      self::WATERMARK_H_ALIGN => $this->get_param_value(self::WATERMARK_H_ALIGN),
//    );
//
//    // Get params.
//    $frametime = $this->get_param_value(self::FRAMETIME);
//    $size = ($this->get_param_value(self::SIZE) ? $this->get_param_value(self::SIZE) : mediamosa_app::get_still_default_size($app_id));
//    $h_padding = $this->get_param_value(self::H_PADDING);
//    $v_padding = $this->get_param_value(self::V_PADDING);
//    $blackstill_check = $this->get_param_value(self::BLACKSTILL_CHECK);
//
    //mediamosa_job_server::create_job_still($job_id, $mediafile_id_src, $frametime, $size, $h_padding, $v_padding, $blackstill_check, $still_parameters);
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

  function send($job) {
    $this->open_connection();

    $this->channel->exchange_declare('jobs', 'direct', false, false, false);

    $msg = new AMQPMessage(
      serialize($job),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    # Send the message.
    //echo 'sent with routing_key: ' . $job['job_type'];
    $this->channel->basic_publish($msg, 'jobs', $job['job_type']);

    $this->close_connection();
  }

  // modelled after mediamosa_job_server.class.inc line 440
  function start_analyze($job){
    mediamosa_debug::log('Entered method: ' . __METHOD__, array(), 'Toon');

    $analyse_result = array();

    $mediafile_path = mediamosa_storage::get_realpath_mediafile($job['mediafile_id']);
    $mime_type = mediamosa_mimetype::get_mime_type($mediafile_path);

    $analyse_result[mediamosa_asset_mediafile_metadata::MIME_TYPE] = array(
      'type' => mediamosa_asset_mediafile_metadata_property_db::TYPE_CHAR,
      'value' => $mime_type,
    );

    // Based on the mime_type there might be a tool that can analyse.
    // Call the mediamosa_tool_can_analyse hook.
    // Toon: There are 4 modules apparently, and there is one that triggers the if in this statement
    foreach (module_implements('mediamosa_tool_can_analyse') as $module) {
      if (module_invoke($module, 'mediamosa_tool_can_analyse', $mime_type)) {
        $analyse_result += module_invoke($module, 'mediamosa_tool_analyse', $job['mediafile_id']);
      }
    }


    // Make an informative log entry.
    $analyse_output = array();
    foreach ($analyse_result as $key => $value) {
      $analyse_output[] = $key . ' [' . $value['type'] . '] ' . $value['value'];
    }

    $link_asset = self::get_asset_link($job['job_id']);

    mediamosa_job_scheduler::log_mediafile
    (
      $job['mediafile_id'], 'TOON IT WORKS Job analyse (Job ID: @job_id) returned output: @output<br /><br />@link',
      array(
        '@job_id' => $job['job_id'],
        '@output' => implode("\n", $analyse_output) . "\n",
        '@link' => $link_asset,
      )
    );

    if (!empty($analyse_result)) {
      mediamosa_debug::log('FINISHED!', array(), 'Toon');
      $this->set_job_status($job['job_id'], $job['mediafile_id'], mediamosa_job_server_db::JOB_STATUS_FINISHED, '1.000');
    }
    else {
      mediamosa_debug::log('FAILED!', array(), 'Toon');
      $this->set_job_status($job['job_id'], $job['mediafile_id'], mediamosa_job_server_db::JOB_STATUS_FAILED, '1.000', 'Empty result, analyse failed.');
    }

    // Generate event analyse finished.
    //mediamosa::rules_invoke_event('mediamosa_event_analyse_finished', $job['mediafile_id']);

  }

  /**
   * Create a link to the parent asset belonging to a given job id.
   *
   * @param int $job_id
   *
   * @return string
   *  Link to an asset.
   */
  public static function get_asset_link($job_id) {

    // Get the job.
    $jobserver_job = mediamosa_job::get($job_id);

    // Get asset ID from job.
    $asset_id = $jobserver_job[mediamosa_job_db::ASSET_ID];

    // Return link.
    return l(mediamosa::t('Go to asset @asset_id', array('@asset_id' => $asset_id)), mediamosa_settings::get_url_asset($asset_id));
  }

  // Did not protect against mysql injection yet, necessary?
  function set_job_status($job_id, $mediafile_id, $job_status, $progress, $error_description = '', $error_description_args = array()) {

    // Set args in description.
    if (!empty($error_description_args)) {
      $error_description = strtr($error_description, $error_description_args);
    }

    $fields = array(
      mediamosa_job_db::JOB_STATUS => $job_status,
      mediamosa_job_db::PROGRESS => is_null($progress) ? '0.000' : $progress
    );

    // Invalidate technical metadata if analyse fails.
    if ($job_status == mediamosa_job_db::JOB_STATUS_FAILED) {
      mediamosa_asset_mediafile_metadata::delete_by_mediafileid($mediafile_id);
    }

    // Because of there being no break, all cases will be executed. This code was chosen over a large if statement
    switch ($job_status) {
      case mediamosa_job_server_db::JOB_STATUS_FINISHED:
      case mediamosa_job_server_db::JOB_STATUS_FAILED:
      case mediamosa_job_server_db::JOB_STATUS_CANCELLED:
        $fields[mediamosa_job_server_db::FINISHED] = mediamosa_datetime::utc_current_timestamp_now(TRUE); // finished = curent time
        break;
    }

    mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
      ->fields($fields)
      ->condition(mediamosa_job_db::ID, (string) $job_id)
      ->execute();
  }

  // ####################################
  // START
  // Functions to do with STILL
  // ####################################
  /**
   * Based on job type, get more information.
   *
   * @param int $job_id
   *   The job ID.
   * @param string $job_type
   *   The job type, see mediamosa_job_db::JOB_TYPE_*.
   * @param string $mediafile_id
   *   The mediafile ID.
   */
  function get_job_parameters($job_id, $job_type, $mediafile_id) {
    $result = array();

    switch ($job_type) {
      case mediamosa_job_db::JOB_TYPE_TRANSCODE:
        // Get job transcode.
        $job_transcode = mediamosa_job_transcode::get($job_id);

        $result['profile_id'] = $job_transcode[mediamosa_job_transcode_db::TRANSCODE_PROFILE_ID];
        $result['tool'] = $job_transcode[mediamosa_job_transcode_db::TOOL];
        $result['file_extension'] = $job_transcode[mediamosa_job_transcode_db::FILE_EXTENSION];
        $result['command'] = self::map_parameters($job_transcode[mediamosa_job_transcode_db::TOOL], $job_transcode[mediamosa_job_transcode_db::COMMAND], $mediafile_id);
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

      case mediamosa_job_db::JOB_TYPE_ANALYSE:
        break;
    }

    return $result;
  }

  function get_generate_still_exec($jobserver_job_id, $mediafile_id_source) {

    // Get the mime-type.
    $mime_type = mediamosa_asset_mediafile_metadata::get_mediafile_metadata_char($mediafile_id_source, mediamosa_asset_mediafile_metadata::MIME_TYPE);

    // get ...
    $job_info = mediamosa_job_server_still::get($jobserver_job_id);
    $job_info += mediamosa_job_server::get($jobserver_job_id);

    // Call the mediamosa_tool_can_generate_still hook.
    foreach (module_implements('mediamosa_tool_can_generate_still') as $module) {
      if (module_invoke($module, 'mediamosa_tool_can_generate_still', $mime_type)) {
        // Get generate still exec.
        return module_invoke($module, 'mediamosa_tool_get_generate_still_exec', $job_info, $mediafile_id_source);
      }
    }

    // FIXME: Fall back on ffmpeg for now.
    return mediamosa_tool_ffmpeg::get_generate_still_exec($job_info, $mediafile_id_source);
  }
  // ####################################
  // END
  // Functions to do with STILL
  // ####################################

}
