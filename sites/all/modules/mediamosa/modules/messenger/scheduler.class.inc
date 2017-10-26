<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class scheduler {

  function send($job) {
    $this->open_connection();

    $this->channel->exchange_declare('jobs', 'direct', false, false, false);
    $this->channel->queue_declare('ANALYSE', false, true, false, false);

    $msg = new AMQPMessage(
      serialize($job),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    $this->channel->basic_publish($msg, 'jobs', $job['job_type']);

    $this->close_connection();
  }


  function round_up_jobs(){
    $jobs = $this->get_jobs(mediamosa_job_db::JOB_STATUS_WAITING);
    mediamosa_debug::log(count($jobs) . ' jobs found with status \"WAITING\" ', array(), 'T - Scheduler');

    foreach ($jobs as $job) {

      switch ($job[mediamosa_job_db::JOB_TYPE]) {
        case mediamosa_job_db::JOB_TYPE_STILL:
          $job_parameters = $this->get_job_parameters($job);

          $job += array(
            mediamosa_job_still_db::BLACKSTILL_CHECK => $job_parameters[mediamosa_job_still_db::BLACKSTILL_CHECK],
            mediamosa_job_db::STILL_PARAMETERS => $job_parameters[mediamosa_job_db::STILL_PARAMETERS],
            mediamosa_job_still_db::SIZE => $job_parameters[mediamosa_job_still_db::SIZE]
          );

          break;
        case mediamosa_job_db::JOB_TYPE_TRANSCODE:
          $job += array(
            'transcode_parameters' => serialize($this->get_job_parameters($job))
          );
          break;
      }

      try {
        $this->send($job);
      } catch (Exception $e) {
        mediamosa_debug::log($e, array(), 'T - Scheduler');
      }

    }
  }

  // Modelled after update_server_job_status
  function update_jobs() {
    $jobs = $this->get_jobs(mediamosa_job_db::JOB_STATUS_FINISHED);

    foreach ($jobs as $job) {
      $this->notify_transcoding($job['status'], $job['job_id']);

      if ($job['job_id'] == 706) {
        echo 'ok';
      }

      if ($job['status'] == mediamosa_job_db::JOB_STATUS_FINISHED) {
        switch ($job['job_type']) {
          case mediamosa_job_db::JOB_TYPE_TRANSCODE:
            mediamosa_statistics::insert_job_transcode($job['mediafile_dest'], $job['job_id']);
            // If the transcode job is completed, add it to the mediafile.
            $this->parse_finished_transcode($job['job_id'], $job['mediafile_id'], $job['mediafile_dest']); // second was mediafile_src ???
            break;
          case mediamosa_job_db::JOB_TYPE_ANALYSE:
            // If the analyse job is completed, add it to the technical
            // metadata.
//            mediamosa_asset_mediafile_metadata::store_analyse(
//              (string)$response_data->items->item[$i]->job_id,
//              unserialize((string)$response_data->items->item[$i]->analyse_result)
//            );
            break;
          case mediamosa_job_db::JOB_TYPE_STILL:
//            // If the still job is completed, add the still to db.
//            // We serialize it, because of the multiple stills.
//            mediamosa_debug::log_export($response_data->items->item[$i]->mediafile_dest, WATCHDOG_NOTICE, 'T - STILL');
//            self::add_still_to_db((string)$response_data->items->item[$i]->job_id, unserialize((string)$response_data->items->item[$i]->mediafile_dest));
            break;
        }
      } elseif ($job['status'] == mediamosa_job_db::JOB_STATUS_FAILED ||
        $job['status'] == mediamosa_job_db::JOB_STATUS_CANCELLED) {

        switch ($job['job_type']) {
          case mediamosa_job_db::JOB_TYPE_TRANSCODE:
            // When transcode has failed.
            $this->parse_failed_transcode((string)$job['job_id']);
            break;
        }
      }
    }
  }

  /**
   * Called when transcode was successfully finished.
   *
   * @param int $job_id
   *   The job ID.
   * @param string $mediafile_id_src
   *   The mediafile ID of the source.
   * @param string $mediafile_id_dest
   *   The mediafile ID of the destination.
   */
  // Toon: Een transcode heeft plaatsgevonden, er staat nog niets in de database wat er met de file moet gebeuren, er is alleen een transcode gedaan
  // Zorgt ervoor dat een rij in mediamosa_asset_mediafile tabel terechtkomt, zodat er een verwijzing is naar die file en dat die bij die asset hoort.
  // Dan is hij bekend binnen MM, als we dat niet doen dan blijft de file staan en dan doen we er niets mee. Zo wordt hij zichtbaar.
  function parse_finished_transcode($job_id, $mediafile_id_src, $mediafile_id_dest) {
    mediamosa_debug::log( 'Transcode successfully finished.', array(), 'T - Scheduler');
    // Get the original transcode job.
    $job_info = $this->get_transcodejob_info($job_id);

    // Get Asset.
    $asset = mediamosa_asset::get($job_info['asset_id']);
    assert(!empty($asset));

    $fields = array(
      mediamosa_asset_mediafile_db::ID => $mediafile_id_dest,
      mediamosa_asset_mediafile_db::ASSET_ID => $job_info['asset_id'],
      mediamosa_asset_mediafile_db::FILENAME => $job_info['filename'],
      mediamosa_asset_mediafile_db::SANNAS_MOUNT_POINT => mediamosa_storage::create_local_mount_point_uri($job_info['app_id']),
      mediamosa_asset_mediafile_db::TRANSCODE_PROFILE_ID => $job_info['transcode_profile_id'],
      mediamosa_asset_mediafile_db::TOOL => $job_info['tool'],
      mediamosa_asset_mediafile_db::FILE_EXTENSION => $job_info['file_extension'],
      mediamosa_asset_mediafile_db::COMMAND => $job_info['command'],
      mediamosa_asset_mediafile_db::OWNER_ID => $job_info['owner'],
      mediamosa_asset_mediafile_db::APP_ID => $job_info['app_id'],
      mediamosa_asset_mediafile_db::IS_ORIGINAL_FILE => mediamosa_asset_mediafile_db::IS_ORIGINAL_FILE_FALSE,
      mediamosa_asset_mediafile_db::MEDIAFILE_ID_SOURCE => $mediafile_id_src,
    );

    // Add created/changed.
    $fields = mediamosa_db::db_insert_enrich($fields);

    // Insert.
    mediamosa_db::db_insert(mediamosa_asset_mediafile_db::TABLE_NAME)
      ->fields($fields)
      ->execute();

    // Relocate mediafile if final storage location is not local.
    mediamosa_storage::relocate_mediafile($job_info['app_id'], $mediafile_id_dest, TRUE);

    // Perform a post convert, if supported.
    if (method_exists('mediamosa_tool_' . $job_info['tool'], 'post_convert')) {
      call_user_func(array('mediamosa_tool_' . $job_info['tool'], 'post_convert'), $job_id, $mediafile_id_dest);
    }

    // Perform a post transcode, if supported.
    if (method_exists('mediamosa_tool_' . $job_info['tool'], 'post_transcode')) {
      call_user_func(array('mediamosa_tool_' . $job_info['tool'], 'post_transcode'), $job_info['asset_id'], $job_info['mediafile_id'], $mediafile_id_dest, $job_info['file_extension']);
    }

    // Perform a post convert, if supported.
    if (method_exists('mediamosa_tool_' . $job_info['tool'], 'post_convert')) {
      call_user_func(array('mediamosa_tool_' . $job_info['tool'], 'post_convert'), $job_id, $mediafile_id_dest);
    }

    // Get mediafile.
    $mediafile = mediamosa_asset_mediafile::get($job_info['mediafile_id']);

    if ($mediafile[mediamosa_asset_mediafile_db::TRANSCODE_INHERITS_ACL] == mediamosa_asset_mediafile_db::TRANSCODE_INHERITS_ACL_TRUE) {
      mediamosa_acl::replace_mediafile_to_mediafile($job_info['mediafile_id'], $mediafile_id_dest);
    }

    // Analyse the mediafile.
    $analyse_result = mediamosa_job_server::analyse((string) $mediafile_id_dest, $job_id);

    // Store it.
    $this->store_analyse_without_job($job_id, $analyse_result, (string) $mediafile_id_dest);
  }

  /**
   * Called when transcode failed or was canceled.
   *
   * @param int $job_id
   *   The job ID.
   */
  public static function parse_failed_transcode($job_id) {
    mediamosa_debug::log( 'Transcode failed.', array(), 'T - Scheduler');
    // Get original.
    $job_original = mediamosa_job::get($job_id);
    $status = $job_original[mediamosa_job_db::JOB_STATUS];

    // Set failed.
    mediamosa_job::set_progress($job_id, '1.000', FALSE, mediamosa_job_db::JOB_STATUS_FAILED);

    // Trigger url.
    mediamosa_job::notify_transcoding($status, mediamosa_job_db::JOB_STATUS_FAILED, $job_id);
  }

  // Toon: De front end wilt soms weten wanneer de transcode klaar is, dat doet hij door een notify url mee te geven.
  public static function notify_transcoding($new_status, $job_id) {
//    if ($old_status == $new_status) {
//      return;
//    }

    $job_transcode = mediamosa_job_transcode::get($job_id);

    // Toon: This action isn't necessary 99% of the time.
    $completed_transcoding_url = $job_transcode[mediamosa_job_transcode_db::COMPLETED_TRANSCODING_URL];
    if (!empty($completed_transcoding_url)) {
      // Add trailing slash if not given.
      if (strpos($completed_transcoding_url, '?') === FALSE) {
        $completed_transcoding_url .= substr($completed_transcoding_url, -1) == '/' ? '' : '/';
      }
      mediamosa_debug::log('Call to completed transcoding URL: ' . $completed_transcoding_url . $new_status, array(), 'Toon - TRANSCODE');

      // Do the call (through proxy).
      mediamosa_http::do_head_call($completed_transcoding_url . $new_status, array('use_curlproxy' => TRUE));
    }
  }


  /**
   * Store the analyse result in the mediafile metadata without running or
   * during a job.
   *
   * @param array $analyse_result
   */
  public static function store_analyse_without_job($job_id, array $analyse_result, $mediafile_id) {
    // Parse and store metadata from the analyse result.
    $ffmpeg_output = mediamosa_asset_mediafile_metadata::store_metadata($mediafile_id, $analyse_result);

    // Store in error description.
    mediamosa_job::store_error_description($job_id, $ffmpeg_output);

    $job = mediamosa_job::get($job_id, array(mediamosa_job_db::MEDIAFILE_DEST));

    mediamosa_debug::log('Mediafile_dest at the moment of truth: ' . $job['mediafile_dest'], array(), 'Toon - TRANSCODE');

    // Have to skip and do retranscode in a different way later
//    // Is it a retranscode job?
//    if ($job[mediamosa_job_db::MEDIAFILE_DEST]) {
//      // Get the realpath before we remove the mediafile from database.
//      $mediafile_realpath_dest = mediamosa_storage::get_realpath_mediafile($job[mediamosa_job_db::MEDIAFILE_DEST]);
//      mediamosa_debug::log('$mediafile_realpath_dest: ' . $mediafile_realpath_dest . ' and $job[mediafile_dest]: ' . $job[mediamosa_job_db::MEDIAFILE_DEST], array(), 'Toon - TRANSCODE');
//
//      // Delete the original so it can be overwritten.
//      mediamosa_asset_mediafile::delete($job[mediamosa_job_db::MEDIAFILE_DEST]);
//
//      // Move the file.
//      mediamosa_io::move(
//        mediamosa_storage::get_uri_mediafile($mediafile_id),
//        $mediafile_realpath_dest
//      );
//
//      // Turn off foreign keys so we can move mediafile data and metadata.
//      mediamosa_db::db_query('SET foreign_key_checks = 0');
//
//      // Update mediamosa_asset_mediafile table.
//      mediamosa_asset_mediafile::update_mediafile_id($mediafile_id, $job[mediamosa_job_db::MEDIAFILE_DEST]);
//
//      // Update mediamosa_asset_mediafile_metadata table.
//      mediamosa_asset_mediafile_metadata::move_mediafile_metadata($mediafile_id, $job[mediamosa_job_db::MEDIAFILE_DEST]);
//
//      // And turn it back on.
//      mediamosa_db::db_query('SET foreign_key_checks = 1');
//
//      // Take the new mediafile_id.
//      $mediafile_id = $job[mediamosa_job_db::MEDIAFILE_DEST];
//    }

    // Normalize to asset.
    mediamosa_asset::update_asset_info_with_mediafileid($mediafile_id);
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
          mediamosa_debug::log('Something went wrong in the analyse script!', array(), 'T - STILL');
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
   * Get the information of transcode job and related info.
   *
   * @param int $job_id
   *   The job ID.
   */
  function get_transcodejob_info($job_id) {
    $result = array();

    $query = mediamosa_db::db_select(mediamosa_job_db::TABLE_NAME, 'j');
    $query->join(mediamosa_asset_mediafile_db::TABLE_NAME, 'mf', 'mf.mediafile_id = j.mediafile_id');
    $query->join(mediamosa_job_transcode_db::TABLE_NAME, 'jt', 'jt.job_id = j.job_id');
    $job_transcode = $query
      ->fields('j', array(
        mediamosa_job_db::ASSET_ID,
        mediamosa_job_db::JOB_TYPE,
        mediamosa_job_db::OWNER_ID,
        mediamosa_job_db::APP_ID,
        mediamosa_job_db::FILENAME,
      ))
      ->fields('mf', array(
        mediamosa_asset_mediafile_db::FILENAME,
        mediamosa_asset_mediafile_db::ID,
      ))
      ->fields('jt', array(
        mediamosa_job_transcode_db::TOOL,
        mediamosa_job_transcode_db::COMMAND,
        mediamosa_job_transcode_db::FILE_EXTENSION,
        mediamosa_job_transcode_db::TRANSCODE_PROFILE_ID,
      ))
      ->condition('j.' . mediamosa_job_db::ID, $job_id)
      ->condition('mf.' . mediamosa_asset_mediafile_db::IS_ORIGINAL_FILE, mediamosa_asset_mediafile_db::IS_ORIGINAL_FILE_TRUE)
      ->execute()
      ->fetchAssoc();

    if ($job_transcode) {
      // Determine new filename, if a tool has given one, use that.
      if ($job_transcode[mediamosa_job_db::FILENAME] != '') {
        $new_filename = $job_transcode[mediamosa_job_db::FILENAME];
      }
      else {
        // Else make one on the basis of the original.
        $new_filename = mediamosa_io::get_base_filename($job_transcode['mf_' . mediamosa_asset_mediafile_db::FILENAME]) . '.' . $job_transcode[mediamosa_asset_mediafile_db::FILE_EXTENSION];
      }

      $result = array(
        'asset_id' => $job_transcode[mediamosa_job_db::ASSET_ID],
        'owner' => $job_transcode[mediamosa_job_db::OWNER_ID],
        'app_id' => $job_transcode[mediamosa_job_db::APP_ID],
        'filename' => $new_filename,
        'transcode_profile_id' => $job_transcode[mediamosa_job_transcode_db::TRANSCODE_PROFILE_ID],
        'tool' => $job_transcode[mediamosa_job_transcode_db::TOOL],
        'file_extension' => $job_transcode[mediamosa_job_transcode_db::FILE_EXTENSION],
        'command' => $job_transcode[mediamosa_job_transcode_db::COMMAND],
        'mediafile_id' => $job_transcode[mediamosa_asset_mediafile_db::ID],
      );
    }
    else {
      $link = mediamosa_job_server::get_asset_link($job_id);
      self::log('Could not find original mediafile for job_id @job_id <br /><br />@link', array('@job_id' => $job_id, '@link' => $link));
    }

    return $result;
  }

  function get_jobs($status) {
    $sql = 'SELECT job_id, job_type, asset_id, mediafile_id, app_id, status, mediafile_dest
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

  function variable_get($name, $default = NULL) {
    return variable_get($name, $default);
  }

  function variable_set($name, $value) {
    variable_set($name, $value);
  }

}