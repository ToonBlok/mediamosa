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







    $servers = $this->get_enabled_job_processor();
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
      if ($job['job_type'] == 'STILL') {
        $job_parameters = $this->get_job_parameters($job['job_id'], $job['mediafile_id']);
        $job['blackstill_check'] = $job_parameters['blackstill_check'];
        $job['still_parameters'] = $job_parameters['still_parameters'];
        $job['size'] = $job_parameters['size'];
      }
      mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'Toon - STILL' );

      $this->send($job);
    }
  }

  // Toon: Is modelled exactly after the real method
  function get_job_parameters($job_id, $mediafile_id) {
    $result = array();

    // Get something from mediamosa_still_db
    $query_job = mediamosa_job_still::get($job_id);
//    mediamosa_debug::log_export($query_job, WATCHDOG_NOTICE, 'Toon - STILL' );

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

    return $result;
  }

}