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

  function ask_db($status) {
    $sql = 'SELECT job_id, job_type, asset_id, mediafile_id, app_id
            FROM {#mediamosa_job} AS mj
            WHERE mj.#job_status = :status';

    $sql = strtr($sql, array(
      '#job_status' => mediamosa_job_db::JOB_STATUS,
      '#mediamosa_job' => mediamosa_job_db::TABLE_NAME
    ));

    return mediamosa_db::db_query($sql,
      array(
        ':status' => $status
      )
    )->fetchAll();

  }

  function round_up_jobs(){
    mediamosa_debug::log('Start method ' . __METHOD__, array(), 'Toon');

    $jobs = $this->ask_db(mediamosa_job_db::JOB_STATUS_WAITING);
//    $sql = 'SELECT job_id, job_type, asset_id, mediafile_id, app_id
//            FROM {#mediamosa_job} AS mj
//            WHERE mj.#job_status = :status';
//
//    $sql = strtr($sql, array(
//      '#job_status' => mediamosa_job_db::JOB_STATUS,
//      '#mediamosa_job' => mediamosa_job_db::TABLE_NAME
//    ));
//
//    $jobs = mediamosa_db::db_query($sql,
//      array(
//        ':status' => mediamosa_job_db::JOB_STATUS_WAITING
//      )
//    )->fetchAll();

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
          // ########################################################################
//          // Just useful to call in scheduler to use debug
//          $transcode = new transcode();
//          $transcode->start($job);
////      $this->set_job_status($job, mediamosa_job_db::JOB_STATUS_INPROGRESS, '0.000'); // Set job in progress.
//          $transcode->update($job);
//          // Mediafile aanmaken hoort echt in scheduler thuis:
////          $this->start_server_job_media();
////          return;
          // ########################################################################
          break;
      }

      try {
        $this->send($job);
      } catch (Exception $e) {
        mediamosa_debug::log($e, array(), 'Toon - SCHEDULER');
      }

    }
  }

  function update_jobs(){

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

//  // Imitates update_server_job_status()
//  public static function update_job_status($uri) {
//
//    // Get response.
//    // Toon: Returns XML with waiting jobs from mediamosa_server_job
//    $response = mediamosa_http::do_internal_call($uri, 'server/joblist');
//    if ($response->code != '200') {
//      self::log('Call to server/joblist did not return a 200 result (header: @header)', array('@header' => var_export($response, TRUE)));
//      return;
//    }
//
//    try {
//      // Toon: Parse the raw XML in $response to $response_data, a object that contains all values from the XML data in $response.
//      $response_data = new mediamosa_connector_response($response->data);
//    }
//    catch (Exception $e) {
//      mediamosa_debug::log_export($response->data);
//      self::log(
//        'Parse XML: @msg (@resp)',
//        array(
//          '@msg' => $e->getMessage(),
//          '@resp' => print_r($response->data, TRUE),
//        ),
//        WATCHDOG_ERROR
//      );
//      return;
//    }
//
//    // Toon: The jobs here come from the mediamosa_server_job table
//    for ($i = 0; $i < $response_data->header->item_count; $i++) {
//      // Get current status.
//      // Toon: Retrieve the actual job
//      // de job in de loop waar hij nu is.
//      $job = mediamosa_job::get((string) $response_data->items->item[$i]->job_id);
//
//      // Toon: Get some fields of this job, store them in the $fiels array.
//      // Toon: Is verzameling alle wijzingen
//      $fields = array(
//        mediamosa_job_db::JOB_STATUS => $response_data->items->item[$i]->status,
//        mediamosa_job_db::PROGRESS => $response_data->items->item[$i]->progress,
//        mediamosa_job_db::ERROR_DESCRIPTION => $response_data->items->item[$i]->error_description,
//      );
//
//      // Add started time.
//      if ($response_data->items->item[$i]->started != '') {
//        $fields[mediamosa_job_db::STARTED] = $response_data->items->item[$i]->started;
//      }
//
//      // Add finished time.
//      if ($response_data->items->item[$i]->finished != '') {
//        $fields[mediamosa_job_db::FINISHED] = $response_data->items->item[$i]->finished;
//      }
//
//      // Add changed.
//      $fields = mediamosa_db::db_update_enrich($fields);
//
//      // Update.
//      // Toon: mediamosa_job en mediamosa_server_job both contain the same job, however the row in
//      // mediamosa_server_job table is more up-to-date and the row in mediamosa_job has to be updated.
//      mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
//        ->fields($fields)
//        ->condition(mediamosa_job_db::ID, (string) $response_data->items->item[$i]->job_id)
//        ->execute();
//
//      // Trigger url.
//      // Toon: Deze methode notified the frontend wanneer een transcoding job voltooid is. $job is de oude waarde, $responsedate is net geupdate in de db statement hierboven.
//      // Toon: recording 20170913_004
//      mediamosa_job::notify_transcoding($job[mediamosa_job_db::JOB_STATUS], (string) $response_data->items->item[$i]->status, (string) $response_data->items->item[$i]->job_id);
//
//      if ($response_data->items->item[$i]->status == mediamosa_job_db::JOB_STATUS_FINISHED ||
//        $response_data->items->item[$i]->status == mediamosa_job_db::JOB_STATUS_FAILED ||
//        $response_data->items->item[$i]->status == mediamosa_job_db::JOB_STATUS_CANCELLED) {
//
//        // Remove link between server and job.
//        // Toon: Server is klaar met job, dus moet uit server_job tabel weggehaald moeten worden. de server_job tabel is de tabel die aangeeft welke jobs er uit gedeeld zijn aan deze server
//        mediamosa_server_job::delete_by_jobid((string) $response_data->items->item[$i]->job_id);
//
//        // Remove job on jobserver.
//        self::remove_job_on_server($uri, (string) $response_data->items->item[$i]->job_id);
//      }
//
//      if ($response_data->items->item[$i]->status == mediamosa_job_db::JOB_STATUS_FINISHED) {
//        switch ($response_data->items->item[$i]->job_type) {
//          case mediamosa_job_db::JOB_TYPE_TRANSCODE:
//            // Statistics.
//            // statistiek bij houden hoeveel jobs er zijn gestart
//            mediamosa_statistics::insert_job_transcode($response_data->items->item[$i]->mediafile_dest, $job[mediamosa_job_db::ID]);
//            //
//            // If the transcode job is completed, add it to the mediafile.
//            self::parse_finished_transcode((string) $response_data->items->item[$i]->job_id, (string) $response_data->items->item[$i]->mediafile_src, (string) $response_data->items->item[$i]->mediafile_dest);
//            break;
//
//          case mediamosa_job_db::JOB_TYPE_ANALYSE:
//            // If the analyse job is completed, add it to the technical
//            // metadata.
//            mediamosa_asset_mediafile_metadata::store_analyse(
//              (string) $response_data->items->item[$i]->job_id,
//              unserialize((string) $response_data->items->item[$i]->analyse_result)
//            );
//            break;
//
//          case mediamosa_job_db::JOB_TYPE_STILL:
//            // If the still job is completed, add the still to db.
//            // We serialize it, because of the multiple stills.
//            mediamosa_debug::log_export($response_data->items->item[$i]->mediafile_dest, WATCHDOG_NOTICE, 'Toon - STILL' );
//            self::add_still_to_db((string) $response_data->items->item[$i]->job_id, unserialize((string) $response_data->items->item[$i]->mediafile_dest));
//            break;
//        }
//      }
//      elseif ($response_data->items->item[$i]->status == mediamosa_job_db::JOB_STATUS_FAILED ||
//        $response_data->items->item[$i]->status == mediamosa_job_db::JOB_STATUS_CANCELLED) {
//
//        switch ($response_data->items->item[$i]->job_type) {
//          case mediamosa_job_db::JOB_TYPE_TRANSCODE:
//            // When transcode has failed.
//            self::parse_failed_transcode((string) $response_data->items->item[$i]->job_id);
//            break;
//        }
//      }
//    }
//  }

}