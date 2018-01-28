<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

abstract class mediamosa_messenger_scheduler extends mediamosa_messenger_rmq
{
  // The queue to which the scheduler returns keepalive messages.
  protected $queue_keepalive_server_guardian;
  // The AMQP connection object.
  protected $connection;
  // The channel on which to open a connection.
  protected $channel;

  const QUEUE_NAME = 'SCHEDULER';

  // Types of messages
  const MSG_KILL = 'KILL';
  const MSG_JOB = 'JOB';
  const MSG_START_JOB = 'START_JOB';
  const MSG_PROGRESSION = 'PROGRESSION';

  // Message attributes
  const MSG_ID = 'MSG_ID';
  const MSG_TYPE = 'MSG_TYPE';
  const MSG_STATUS = 'MSG_STATUS';
  const MSG_DATA = 'DATA';
  const MSG_QUEUE = 'QUEUE';
  const MSG_TIMESTAMP = 'TIMESTAMP';
  const MSG_UNIT_TEST_PREFIX = 'UNIT_TEST_PREFIX';

  // Status of a server
  const SERVER_STATUS_ONLINE = 'ONLINE';
  const SERVER_STATUS_PENDING = 'PENDING';

  /**
   * Process a msg. If the message's purpose was to start a new job, compose new
   * message, gets new MSG_ID, enriches jobs with and more information they may need, determine where the message needs
   * to go to based on its content. If the msg was a progression update job, pass along the job of the msg to the update function.
   *
   * @param $msg
   *   Unpacked msg received in the listen queue.
   *
   */
  protected function mediamosa_messenger_process_msg($msg) {
    if ($msg[self::MSG_TYPE] == self::MSG_START_JOB) {
      $msg[self::MSG_ID] = mediamosa_job::mediamosa_messenger_get_unique_msg_id();

      switch ($msg[mediamosa_messenger_scheduler::MSG_DATA][mediamosa_job_db::JOB_TYPE]) {
        case mediamosa_job_db::JOB_TYPE_ANALYSE:
          $msg[mediamosa_messenger_scheduler::MSG_DATA] = $this->mediamosa_messenger_enrich_job($msg[mediamosa_messenger_scheduler::MSG_DATA]);
          break;

        case mediamosa_job_db::JOB_TYPE_STILL:
          $msg[mediamosa_messenger_scheduler::MSG_DATA] = $this->mediamosa_messenger_enrich_job($msg[mediamosa_messenger_scheduler::MSG_DATA]);
          break;

        case mediamosa_job_db::JOB_TYPE_TRANSCODE:
          $msg[mediamosa_messenger_scheduler::MSG_DATA] = $this->mediamosa_messenger_enrich_job($msg[mediamosa_messenger_scheduler::MSG_DATA]);
          break;
      }

      // Piece together the desired destination of this msg based on its contents.
      $msg[mediamosa_messenger_scheduler::MSG_QUEUE] = $msg[mediamosa_messenger_scheduler::MSG_UNIT_TEST_PREFIX] . $msg[mediamosa_messenger_scheduler::MSG_DATA][mediamosa_job_db::JOB_TYPE];

      // Send the prepared job to a jobcore.
      $this->rmq_send($msg, $msg[mediamosa_messenger_scheduler::MSG_QUEUE]);
    } elseif ($msg[self::MSG_TYPE] == self::MSG_PROGRESSION) {
      $job = $msg[mediamosa_messenger_scheduler::MSG_DATA];
      // Pass along the job of this msg to update it.
      $this->mediamosa_messenger_update_progression($job, $job[mediamosa_job_db::ID], $job[mediamosa_job_db::JOB_TYPE], $job[mediamosa_job_db::ASSET_ID], $job[mediamosa_job_db::MEDIAFILE_ID], $job[mediamosa_job_db::JOB_STATUS], $job['new_job_status'], $job[mediamosa_job_db::APP_ID], $job[mediamosa_job_db::OWNER_ID], $job[mediamosa_job_db::STILL_PARAMETERS]);
    }
  }


  /**
   * Update progression of a job.
   *
   * @param array $job
   * The entire job.
   * @param int $job_id
   * @param string $job_type
   * @param string $asset_id
   * @param string $mediafile_id_src
   * @param array $current_job_status
   * Contains information from the progression file.
   * @param array $job_progression
   * The completion percentage of this job.
   * @param int $app_id
   * @param string $owner
   * @param array $still_parameters
   */
  protected function mediamosa_messenger_update_progression($job, $job_id, $job_type, $asset_id, $mediafile_id_src, $current_job_status, $job_progression, $app_id, $owner, $still_parameters) {
    $new_job_status = mediamosa_job_db::JOB_STATUS_INPROGRESS;
//
    // No status file found, we just have to wait.
    if (empty($job_progression['Status'])) {
      mediamosa_job_server::log_debug_mediafile($mediafile_id_src, "No status file found with @statusfile for job @job_id, maybe next run.", array('@statusfile' => mediamosa_storage::get_realpath_status_file($job_id), '@job_id' => $job_id));
      return;
    }

    switch ($job_type) {
      case mediamosa_job_server_db::JOB_TYPE_STILL:
        $file_scene = mediamosa_storage::get_uri_scene_file($job_id);

        if (!mediamosa_io::file_exists($file_scene) && empty($job_progression)) {
          // No status file found, we just have to wait.
          mediamosa_job_server::log_debug_mediafile($mediafile_id_src, 'No status file found with name @name for job @job_id, maybe next run.', array('@name' => mediamosa_storage::get_realpath_status_file($job_id), '@job_id' => $job_id));
          return;
        }

        // Set defaults, to fix some possible notices.
        $job_progression += array(
          'Status' => '',
          'Errors' => 'none',
          'Progress' => '0.000',
        );

        if (mediamosa_io::file_exists($file_scene) || ($job_progression['Status'] == 'done' && $job_progression['Errors'] == 'none')) {
          $new_job_status = $this->mediamosa_messenger_store_new_still($job_id, $job_type, $current_job_status, $asset_id, $app_id, $owner, $mediafile_id_src, $still_parameters);

          if ($new_job_status == mediamosa_job_server_db::JOB_STATUS_INPROGRESS) {
            mediamosa_job_server::log_debug_mediafile($mediafile_id_src, 'Running @job_type job (storing file busy), Job ID @job_id, with status: @status', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $new_job_status));
          } else {
            mediamosa_job_server::log_debug_mediafile($mediafile_id_src, 'End @job_type job, Job ID @job_id, with status: @status', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $new_job_status));
          }

        } elseif ($job_progression['Status'] == 'error' && $job_progression['Errors'] != 'none') {
          $new_job_status = mediamosa_job_server_db::JOB_STATUS_FAILED;
          $link_asset = $this->mediamosa_messenger_get_asset_link($job_id);

          mediamosa_job_server::log_debug_mediafile($mediafile_id_src, 'End @job_type job, Job ID @job_id, with status: @status<br /><br />@link', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $new_job_status, '@link' => $link_asset));
          mediamosa_job_server::log_debug_high_mediafile($mediafile_id_src, "Info @job_type job, Job ID @job_id, status file '@statusfile'", array('@job_type' => $job_type, '@job_id' => $job_id, '@statusfile' => $job_progression));
        }

        // Update the status.
        if (!mediamosa_io::file_exists($file_scene) && $job_progression['Errors'] != 'none') {
          // Might be because there is no status file, dont bother to update.
          if (isset($job_progression['Errors'])) {
            $this->mediamosa_messenger_set_job_status($job_id, $asset_id, $mediafile_id_src, $job_type, $current_job_status, $new_job_status, $job_progression['Progress'], $job_progression['Errors']);
          }
        } else {
          $this->mediamosa_messenger_set_job_status($job_id, $asset_id, $mediafile_id_src, $job_type, $current_job_status, $new_job_status, $job_progression['Progress']);
        }
        break;

      case mediamosa_job_server_db::JOB_TYPE_TRANSCODE:
        if ($job_progression['Status'] == 'done' && $job_progression['Errors'] == 'none') {
          $new_job_status = mediamosa_job_db::JOB_STATUS_FINISHED;

          // Store the transcode.
          $mediafile_id_dest = $this->mediamose_messenger_store_new_mediafile($job);

          // Set job status.
          $this->mediamosa_messenger_set_job_status($job_id, $asset_id, $mediafile_id_src, $job_type, $current_job_status, $new_job_status, $job_progression['Progress']);

          // Generate event transcode finished.
          mediamosa::rules_invoke_event('mediamosa_event_transcode_finished', $mediafile_id_src);

          $job = $this->mediamosa_messenger_get_job_where($job_id);

          mediamosa_statistics::insert_job_transcode($mediafile_id_dest, $job[mediamosa_job_db::ID]);
          $this->mediamosa_messenger_parse_finished_transcode($job['job_id'], $job['mediafile_id'], $mediafile_id_dest); // If the transcode job is completed, add it to the mediafile.

        } elseif ($job_progression['Status'] == 'error' && (empty($job_progression['Errors']) || $job_progression['Errors'] != 'none')) {
          $new_job_status = mediamosa_job_server_db::JOB_STATUS_FAILED;
          $link_asset = mediamosa_job_server::get_asset_link($job_id);

          mediamosa_job_server::log_mediafile($mediafile_id_src, "End @job_type job, Job ID @job_id, with status: @status<br /><br />@link", array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $new_job_status, '@link' => $link_asset));
          mediamosa_job_server::log_mediafile($mediafile_id_src, "Info @job_type job, Job ID @job_id, status file '@statusfile'", array('@job_type' => $job_type, '@job_id' => $job_id, '@statusfile' => mediamosa_job_server::get_status_contents($job_id, TRUE)));

          // Set status to failed.
          $this->mediamosa_messenger_set_job_status($job_id, $asset_id, $mediafile_id_src, $job_type, $current_job_status, $new_job_status, $job_progression['Progress'], isset($job_progression["ffmpeg-output"]) ? ($job_progression["Errors"] != "" ? $job_progression["Errors"] . "-\n" : '') . $job_progression["ffmpeg-output"] : $job_progression["Errors"]);

          // Generate event transcode failed.
          mediamosa::rules_invoke_event('mediamosa_event_transcode_failed', $mediafile_id_src);
        } else {
          // Set job status.
          $this->mediamosa_messenger_set_job_status($job_id, $asset_id, $mediafile_id_src, $job_type, $current_job_status, $new_job_status, $job_progression['Progress']);
        }
        break;
    }
  }

  /**
   * Create a link to the parent asset belonging to a given job id.
   *
   * @param int $job_id
   *
   * @return string
   * Link to an asset.
   */
  protected function mediamosa_messenger_get_asset_link($asset_id) {
    return l(mediamosa::t('Go to asset @asset_id', array('@asset_id' => $asset_id)), mediamosa_settings::get_url_asset($asset_id));
  }


  /**
   * Check the created still and save it if everything is ok.
   *
   * @param string $job_id
   *   Current job id.
   * @param string $mediafile_id_src
   *   Contains a file path to the mediafile
   * @return string
   *   Contains the error message
   */
  protected function mediamosa_messenger_store_new_still($job_id, $job_type, $current_job_status, $asset_id, $app_id, $owner, $mediafile_id_src, $still_parameters) {
    $base_filename = mediamosa_io::get_base_filename($job_id);
    $filename = mediamosa_storage::get_uri_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, 1) . '.jpeg');

    if (!mediamosa_io::file_exists($filename) || !mediamosa_io::filesize($filename)) {
      // Something failed. Remove the files and fail the job.
      $still_error = mediamosa_error::error_code_find_description(mediamosa_error::ERRORCODE_STILL_IS_NOT_CREATABLE, array('@mediafile_id' => $mediafile_id_src));

      $this->mediamosa_messenger_set_job_status($job_id, $asset_id, $mediafile_id_src, $job_type, $current_job_status, mediamosa_job_db::JOB_STATUS_FAILED, $still_error);

      // Remove all of the still images.
      $i = 1;
      while (mediamosa_io::file_exists(mediamosa_storage::get_realpath_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, $i) . '.jpeg')) && $i <= mediamosa_settings::STILL_MAXIMUM) {
        mediamosa_io::unlink(mediamosa_storage::get_realpath_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, $i) . '.jpeg'));
        $i++;
      }
      mediamosa_io::unlink(mediamosa_storage::get_realpath_status_file($job_id));

      mediamosa_job_server::log_mediafile($mediafile_id_src, $still_error);
      return mediamosa_job_server_db::JOB_STATUS_FAILED;
    }
    // Check if the frame has any usefull content. We do this by checking the amount of dominant colors.
    mediamosa_job_server_still::still_validate($job_id, $base_filename);

    $i = 1;
    $mediafile_dest = array();
    while (mediamosa_io::file_exists(mediamosa_storage::get_uri_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, $i) . '.jpeg'))) {
      if ($i <= mediamosa_settings::STILL_MAXIMUM) {
        // Generate new hash./domio
        $mediafile_id = mediamosa_db::uuid($app_id);

        $source_uri = mediamosa_storage::get_uri_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, $i) . '.jpeg');
        $destination_uri = mediamosa_storage::create_local_mediafile_uri($app_id, $mediafile_id);

        // Make sure destination dir exists.
        mediamosa_io::mkdir(mediamosa_io::dirname($destination_uri));

        // Everything went ok, move the still and remove other files
        mediamosa_io::rename($source_uri, $destination_uri);
        $mediafile_dest[] = $mediafile_id;
      } else {
        // Reached the maximum, just delete the remain stills.
        mediamosa_io::unlink(mediamosa_storage::get_realpath_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, $i) . '.jpeg'));
      }

      $i++;
    }
    mediamosa_io::unlink(mediamosa_storage::get_realpath_status_file($job_id));

    $fields = array(
      mediamosa_job_server_db::MEDIAFILE_DEST => serialize($mediafile_dest),
    );
    // Add changed.
    $fields = mediamosa_db::db_update_enrich($fields);

    mediamosa_db::db_update(mediamosa_job_server_db::TABLE_NAME)
      ->fields($fields)
      ->condition(mediamosa_job_server_db::JOB_ID, $job_id)
      ->execute();

    // Log it.
    mediamosa_job_server::log_mediafile($mediafile_id_src, 'Job (job_id: @job_id) finished: Multiple stills saved as e.g.: @filenames.', array('@job_id' => $job_id, '@filenames' => implode(',', $mediafile_dest)));

    $this->mediamosa_messenger_add_still_to_db($job_id, $asset_id, $app_id, $owner, $mediafile_id_src, $mediafile_dest, $still_parameters); // $job here is nothing but app_id and hint

    return mediamosa_job_server_db::JOB_STATUS_FINISHED;
  }

  /**
   * Jobs may need some extra data that was not included in the original message.
   *
   * @param $job
   * The job we want to enrich, can be either TRANSCODE or STILL.
   *
   * @return array
   * Array that contains the updated job.
   */
  protected function mediamosa_messenger_enrich_job($job) {
    $job += array(
      'asset_link' => ''
    );

    $job['asset_link'] = $this->mediamosa_messenger_get_asset_link($job[mediamosa_job_db::ASSET_ID]);

    if (empty($job['asset_link'])) {
      mediamosa_debug::log('Error retrieving asset_link!', array(), 'T - MISC');
      // Something went wrong, we cannot proceed
    }

    switch ($job[mediamosa_job_db::JOB_TYPE]) {
      case mediamosa_job_db::JOB_TYPE_STILL:
        $still_parameters = unserialize($job[mediamosa_job_db::STILL_PARAMETERS]);

        $job += array(
          'blackstill_check' => '',
        );

        switch ($still_parameters['still_type']) {
          case mediamosa_asset_mediafile_metadata::STILL_TYPE_NORMAL:
          case mediamosa_asset_mediafile_metadata::STILL_TYPE_SECOND:
          case mediamosa_asset_mediafile_metadata::STILL_TYPE_SCENE:
            $job['blackstill_check'] = mediamosa_job_still_db::BLACKSTILL_CHECK_FALSE;
            break;

          default:
            $job['blackstill_check'] = mediamosa_job_still_db::BLACKSTILL_CHECK_TRUE;
            break;
        }

        if (empty($job['blackstill_check'])) {
          // Something went wrong, we cannot proceed
        }

        // We still dont have fps and video duration:
        $a_mediafile = mediamosa_asset_mediafile_metadata::get_with_mediafileid($job[mediamosa_job_db::MEDIAFILE_ID], array(
          array(
            'prop_name' => mediamosa_asset_mediafile_metadata::FILE_DURATION,
            'type' => mediamosa_asset_mediafile_metadata_property_db::TYPE_CHAR,
          ),
          array(
            'prop_name' => mediamosa_asset_mediafile_metadata::FPS,
            'type' => mediamosa_asset_mediafile_metadata_property_db::TYPE_CHAR,
          ),
        ));
        $actual_duration = $a_mediafile[mediamosa_asset_mediafile_metadata::FILE_DURATION];
        $fps = $a_mediafile[mediamosa_asset_mediafile_metadata::FPS];
        $video_duration = 0;
        if ($actual_duration && $actual_duration != '') {
          // Remove the ms from time.
          $actual_duration = mediamosa_unicode::substr($actual_duration, 0, 8);
          @list($uren, $minuten, $seconden) = explode(':', $actual_duration, 3);
          $video_duration = (($uren * 3600) + ($minuten * 60) + $seconden);
          if ($video_duration < $still_parameters['frametime']) {
            // If the video duration is less than still picture time, we decrease
            // the still picture time to half of video duration.
            $still_parameters['frametime'] = ($video_duration >> 1);
          }
        }

        $still_parameters += array(
          'fps' => $fps,
          'video_duration' => $video_duration
        );

        $job[mediamosa_job_db::STILL_PARAMETERS] = serialize($still_parameters);
        break;

      case mediamosa_job_db::JOB_TYPE_TRANSCODE:
        $job += array(
          'transcode_parameters' => ''
        );

        $job['transcode_parameters'] = $this->mediamosa_messenger_get_transcode_parameters($job);

        if (empty($job['transcode_parameters'])) {
          mediamosa_debug::log('Error retrieving transcode_parameters!', array(), 'T - MISC');
          // Something went wrong, we cannot proceed
        }
        break;
    }

    return $job;
  }

  /**
   * Add a still to the database.
   *
   * @param int $job_id
   * @param string $asset_id
   * @param int $app_id
   * @param string $owner
   * @param string $mediafile_id_src
   * @param array $filenames
   * @param array $still_parameters
   */
  protected function mediamosa_messenger_add_still_to_db($job_id, $asset_id, $app_id, $owner, $mediafile_id_src, $filenames, $still_parameters) {
    // Scene changes.
    $scene_realpath = mediamosa_storage::get_realpath_temporary_file($job_id . '_scene.txt');

    $scenes = array();

    if (mediamosa_io::file_exists($scene_realpath)) {
      $fh = @fopen($scene_realpath, 'r');
      if ($fh) {
        while (!feof($fh)) {
          $scenes[] = (int)fgets($fh);
        }
        fclose($fh);
      }
    }

    mediamosa_job_scheduler::log_mediafile(
      $mediafile_id_src,
      'Start creation DB multiple still, e.g. @filename, job: @job_id, still_parameters: @still_parameters',
      array(
        '@filename' => $filenames[0],
        '@job_id' => (string)$job_id,
        '@still_parameters' => print_r($still_parameters, TRUE),
      ),
      $asset_id
    );


    // Remove old stills
    // We have multiple stills now, so we don't delete the old ones
    // And deleting with asset_id is definetly not a good idea, while we have
    // multiple stills per mediafile _media_management_delete_still($asset_id);
    //
    // Add record to the mediafile metadata table.
    if (is_array($filenames)) {

      $frametime = $still_parameters['frametime'];
      if (isset($still_parameters['framerate']) && is_numeric($still_parameters['framerate'])) {
        $second = $still_parameters['framerate'] > 0 ? 1 / $still_parameters['framerate'] : 0;
      }
      $tag = $still_parameters['tag'];

      $order = 0;
      $sec = 0;
      if (isset($frametime) && is_numeric($frametime)) {
        $sec = $frametime;
      }
      $i = 0;
      foreach ($filenames as $filename) {
        mediamosa_asset_mediafile_still::create($asset_id, $filename, $app_id, $owner, '', $order, !$order, $still_parameters, ($scenes == array() ? $sec : $scenes[$i]), $mediafile_id_src, $tag);

        $order++;
        if (isset($second) && is_numeric($second)) {
          $sec += $second;
        }

        $i++;
      }
    }

    // Remove file.
    mediamosa_io::unlink($scene_realpath);
  }

  /**
   * Store a new mediafile.
   *
   * @param array $job
   *
   * @return int
   * Mediafile ID.
   */
  protected function mediamose_messenger_store_new_mediafile($job) {
    $job_id = $job[mediamosa_job_db::ID];
    $transcode_parameters = $job['transcode_parameters'];
    $app_id = $job[mediamosa_job_db::APP_ID];

    $mediafile_id = mediamosa_db::uuid($job_id); // Generate new mediafile ID.
    $file_extension = $transcode_parameters[mediamosa_job_server_transcode_db::FILE_EXTENSION];

    // Get the filenames.
    $file_status_uri = mediamosa_storage::get_uri_status_file($job_id);
    $file_transcode_uri = mediamosa_storage::get_uri_temporary_file($job_id . '.' . $file_extension);
    $file_destination_uri = mediamosa_storage::create_local_mediafile_uri($app_id, $mediafile_id);

    // Rename transcoded file to new dest.
    mediamosa_io::rename($file_transcode_uri, $file_destination_uri);

    // Now remove the status file.
    mediamosa_io::unlink($file_status_uri);

    return $mediafile_id;
  }

  /**
   * Update the status of a job.
   *
   * @param string $job_id
   * @param string $asset_id
   * @param string $mediafile_id
   * @param string $job_type
   * @param array $current_job_status
   * Contains information from the progression file.
   * @param string $new_job_status
   * The new determined job status
   * @param int $progress
   * The percentage of the job
   * @param string $error_description
   * @param string $error_description_args
   */
  protected function mediamosa_messenger_set_job_status($job_id, $asset_id, $mediafile_id, $job_type, $current_job_status, $new_job_status, $progress, $error_description = '', $error_description_args = array()) {
    mediamosa_job::mediamosa_messenger_set_job_status($job_id, $mediafile_id, $job_type, $current_job_status, $new_job_status, $progress, $error_description, $error_description_args);
    $this->mediamosa_messenger_process_next_job($job_id, $asset_id, $new_job_status);
  }

  /**
   * Determine if we can send the next job.
   *
   * @param string $job_id
   * @param string $asset_id
   * @param string $new_job_status
   * The new determined job status
   */
  protected function mediamosa_messenger_process_next_job($job_id, $asset_id, $new_job_status) {
    if (!mediamosa::in_simpletest_sandbox()) {
      if ($new_job_status == mediamosa_job_db::JOB_STATUS_FINISHED) {
        // If no job is already in progress
        $count_jobs_in_progress = mediamosa_db::db_select(mediamosa_job_db::TABLE_NAME, 'mj')
          ->fields('mj')
          ->condition(mediamosa_job_db::ASSET_ID, $asset_id)
          ->condition(mediamosa_job_db::JOB_TYPE, mediamosa_job_db::JOB_TYPE_UPLOAD, '!=')
          ->condition(mediamosa_job_db::JOB_STATUS, mediamosa_job_db::JOB_STATUS_INPROGRESS)
          ->orderBy('mj.job_id', 'ASC')
          ->countQuery()
          ->execute()
          ->fetchField();

        if ($count_jobs_in_progress == 0) {
          $next_job = mediamosa_db::db_select(mediamosa_job_db::TABLE_NAME, 'mj')
            ->fields('mj')
            ->condition(mediamosa_job_db::ASSET_ID, $asset_id)
            ->condition(mediamosa_job_db::ID, $job_id, '>')
            ->condition(mediamosa_job_db::JOB_TYPE, mediamosa_job_db::JOB_TYPE_UPLOAD, '!=')
            ->condition(mediamosa_job_db::JOB_STATUS, mediamosa_job_db::JOB_STATUS_WAITING)
            ->orderBy('mj.job_id', 'ASC')
            ->execute()
            ->fetchAssoc();    //

          if (!empty($next_job)) {

            $unit_test_prefix = '';
            if (!is_null($GLOBALS['drupal_test_info']['test_run_id'])) {
              $unit_test_prefix = $GLOBALS['drupal_test_info']['test_run_id'];
            }

            $msg = [
              mediamosa_messenger_scheduler::MSG_ID => mediamosa_job::mediamosa_messenger_get_unique_msg_id(),
              mediamosa_messenger_scheduler::MSG_TYPE => mediamosa_messenger_scheduler::MSG_START_JOB,
              mediamosa_messenger_scheduler::MSG_QUEUE => $unit_test_prefix . mediamosa_messenger_scheduler::QUEUE_NAME,
              mediamosa_messenger_scheduler::MSG_TIMESTAMP => microtime(TRUE),
              mediamosa_messenger_scheduler::MSG_UNIT_TEST_PREFIX => $unit_test_prefix,
              mediamosa_messenger_scheduler::MSG_DATA => $next_job
            ];

            $this->mediamosa_messenger_process_msg($msg);
          }
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
  protected function mediamosa_messenger_parse_finished_transcode($job_id, $mediafile_id_src, $mediafile_id_dest) {
    // Get the original transcode job.
    $job_info = $this->mediamosa_messenger_get_transcodejob_info($job_id);

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

    /*
     * TODO: Call goes trough old job server, replace with routing new analyse job trough the rabbitmq jobscheduler.
     */
    // Analyse the mediafile.
    $analyse_result = mediamosa_job_server::analyse((string)$mediafile_id_dest, $job_id);

    // Store it.
    $this->mediamosa_messenger_store_analyse_without_job($job_id, $analyse_result, (string)$mediafile_id_dest);
  }

  /**
   * Store the analyse result in the mediafile metadata without running or
   * during a job.
   *
   * @param string $job_id
   * @param array $analyse_result
   * @param string $mediafile_id
   */
  protected function mediamosa_messenger_store_analyse_without_job($job_id, array $analyse_result, $mediafile_id) {
    // Parse and store metadata from the analyse result.
    $ffmpeg_output = mediamosa_asset_mediafile_metadata::store_metadata($mediafile_id, $analyse_result);

    // Store in error description.
    mediamosa_job::store_error_description($job_id, $ffmpeg_output);

    $job = mediamosa_job::get($job_id, array(mediamosa_job_db::MEDIAFILE_DEST));

    if ($job[mediamosa_job_db::MEDIAFILE_DEST]) {
      // Get the realpath before we remove the mediafile from database.
      $mediafile_realpath_dest = mediamosa_storage::get_realpath_mediafile($job[mediamosa_job_db::MEDIAFILE_DEST]);
      mediamosa_debug::log('$mediafile_realpath_dest: ' . $mediafile_realpath_dest . ' and $job[mediafile_dest]: ' . $job[mediamosa_job_db::MEDIAFILE_DEST], array(), 'Toon - TRANSCODE');

      // Delete the original so it can be overwritten.
      mediamosa_asset_mediafile::delete($job[mediamosa_job_db::MEDIAFILE_DEST]);

      // Move the file.
      mediamosa_io::move(
        mediamosa_storage::get_uri_mediafile($mediafile_id),
        $mediafile_realpath_dest
      );

      // Turn off foreign keys so we can move mediafile data and metadata.
      mediamosa_db::db_query('SET foreign_key_checks = 0');

      // Update mediamosa_asset_mediafile table.
      mediamosa_asset_mediafile::update_mediafile_id($mediafile_id, $job[mediamosa_job_db::MEDIAFILE_DEST]);

      // Update mediamosa_asset_mediafile_metadata table.
      mediamosa_asset_mediafile_metadata::move_mediafile_metadata($mediafile_id, $job[mediamosa_job_db::MEDIAFILE_DEST]);

      // And turn it back on.
      mediamosa_db::db_query('SET foreign_key_checks = 1');

      // Take the new mediafile_id.
      $mediafile_id = $job[mediamosa_job_db::MEDIAFILE_DEST];
    }

    // Normalize to asset.
    mediamosa_asset::update_asset_info_with_mediafileid($mediafile_id);
  }

  /**
   * Get transcode job parameters.
   *
   * @param $job
   * The transcode job we want to retreive the parameters for.
   * @return array
   * Transcode parameters.
   */
  protected function mediamosa_messenger_get_transcode_parameters($job) {
    $job_id = $job[mediamosa_job_db::ID];
    $mediafile_id = $job[mediamosa_job_db::MEDIAFILE_ID];

    $result = array();

    $job_transcode = mediamosa_job_transcode::get($job_id);

    $result['profile_id'] = $job_transcode[mediamosa_job_transcode_db::TRANSCODE_PROFILE_ID];
    $result['tool'] = $job_transcode[mediamosa_job_transcode_db::TOOL];
    $result['file_extension'] = $job_transcode[mediamosa_job_transcode_db::FILE_EXTENSION];
    $result['command'] = mediamosa_job_scheduler::map_parameters($job_transcode[mediamosa_job_transcode_db::TOOL], $job_transcode[mediamosa_job_transcode_db::COMMAND], $mediafile_id);

    return $result;
  }


  /**
   * Get the information of transcode job and related info.
   *
   * @param int $job_id
   * @return array
   * Additional transcode related info.
   */
  protected function mediamosa_messenger_get_transcodejob_info($job_id) {
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
      } else {
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
    } else {
      $link = mediamosa_job_server::get_asset_link($job_id);
      mediamosa_job_server::log('Could not find original mediafile for job_id @job_id <br /><br />@link', array('@job_id' => $job_id, '@link' => $link));
    }

    return $result;
  }

  /**
   * Get a specific job.
   *
   * @param int $job_id
   * The job ID.
   * @param string $asset_id
   * The asset ID.
   */
  protected function mediamosa_messenger_get_job_where($job_id, $asset_id = '') {
    $sql = 'SELECT job_id, job_type, asset_id, mediafile_id, app_id, status, mediafile_dest
          FROM {#mediamosa_job} AS mj
          WHERE mj.#job_id = :id';

    $sql = strtr($sql, array(
      '#job_id' => mediamosa_job_db::ID,
      '#mediamosa_job' => mediamosa_job_db::TABLE_NAME
    ));

    if (!empty($asset_id)) {
      $sql .= ' AND
                mj.#asset_id = :asset_id';

      $sql = strtr($sql, array(
        '#asset_id' => mediamosa_job_db::ASSET_ID,
      ));

      return mediamosa_db::db_query($sql,
        array(
          ':id' => $job_id,
          ':asset_id' => $asset_id
        )
      )->fetch();

    } else {

      return mediamosa_db::db_query($sql,
        array(
          ':id' => $job_id
        )
      )->fetch();
    }
  }

  /**
   * Wrapper around variable_get().
   */
  protected function variable_get($name, $default = NULL) {
    return variable_get($name, $default);
  }

  /**
   * Wrapper around variable_set().
   */
  protected function variable_set($name, $value) {
    variable_set($name, $value);
  }
}

/**
 * URI: scheduler/start
 * Method: GET
 *
 * Start the scheduler.
 */
class mediamosa_scheduler_start extends mediamosa_messenger_scheduler
{

  const QUEUE = 'queue';
  const QUEUE_KEEPALIVE = 'scheduler_keepalive';
  const QUEUE_KEEPALIVE_SERVER = 'queue_keepalive_server';
  const QUEUE_KEEPALIVE_SERVER_GUARDIAN = 'queue_keepalive_server_guardian';
  const CORE_TYPE = 'core_type';

  protected $queue_keepalive_server;
  protected $queue_keepalive_server_guardian;

  /**
   * Implements get_var_setup().
                                   */
  public function get_var_setup() {
    $var_setup = array(
      self::VARS => array(
        self::QUEUE => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => '',
        ),
        self::QUEUE_KEEPALIVE_SERVER => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => '',
        ),
        self::QUEUE_KEEPALIVE_SERVER_GUARDIAN => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => '',
        ),
        self::CORE_TYPE => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => '',
        )
      )
    );

    // Enrich with required REST vars.
    return self::get_var_setup_default($var_setup, FALSE);
  }

  /**
   * Implements do_call().
                             */
  public function do_call() {
    $mediamosa = mediamosa::get();
    $queue = unserialize($this->get_param_value(self::QUEUE));
    $this->queue_keepalive_server = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER);
    $this->queue_keepalive_server_guardian = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER_GUARDIAN); // Queue that scheduler needs to send messages BACK to guardian.

    try {
      $this->rmq_listen($queue);
    } catch (Exception $e) {
      // Listen failed
      mediamosa_debug::log($e, array(), 'T - SCHEDULER');
    }

    $mediamosa->add_item(
      array(
        'server_type' => mediamosa_messenger_scheduler::QUEUE_NAME
      )
    );
  }

  /**
   * Start listening to a queue.
   *
   * @param array $queue
   * The queue we are going to listen to.
   */
  protected function rmq_listen($queue) {
    $this->rmq_open_connection();

    $this->channel->exchange_declare(mediamosa_messenger_jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    foreach ($queue as $core) {
      $this->channel->queue_declare($core, false, true, false, false);
      $this->channel->queue_bind($core, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $core);
    }
    $this->channel->queue_declare($this->queue_keepalive_server, false, true, false, false);
    $this->channel->queue_bind($this->queue_keepalive_server, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $this->queue_keepalive_server);

    /**
     * Callback used for the communication between the scheduler and jobcores for the purpose of the updating of a jobs progression.
     *
     * @param $rmq_msg
     *   RabbitMQ message received from the server.
     *
                                                                                                                                       */
    $callback = function ($rmq_msg) {
      $this->rmq_ack($rmq_msg);
      $msg = unserialize($rmq_msg->body);

      if ($msg[self::MSG_TYPE] == self::MSG_KILL) {
        $this->channel->basic_cancel($rmq_msg->delivery_info['consumer_tag']);
        exit();
      } else {
        $this->mediamosa_messenger_process_msg($msg);
      }
    };

    /**
     * Callback used for the communication between the guardian class and this scheduler to keep this scheduler alive.
     *
     * @param $rmq_msg
     *   RabbitMQ message received from the server.
     *
     */
    $keepalive_callback = function ($rmq_msg) {
      $this->rmq_ack($rmq_msg);
      $msg = unserialize($rmq_msg->body);

      $msg[self::MSG_STATUS] = mediamosa_messenger_scheduler::SERVER_STATUS_ONLINE;
      $msg[mediamosa_messenger_scheduler::MSG_TIMESTAMP] = microtime(true);

      $this->rmq_send($msg, $this->queue_keepalive_server_guardian);
    };

    // Tells RabbitMQ not to give more than one message to a worker at a time.
    $this->channel->basic_qos(null, 1, null);

    foreach ($queue as $core) {
      $this->channel->basic_consume($core, '', false, false, false, false, $callback);
    }
    $this->channel->basic_consume($this->queue_keepalive_server, '', false, false, false, false, $keepalive_callback);

    while (count($this->channel->callbacks)) {
      $this->channel->wait();
    }

    $this->rmq_close_connection();
  }

  protected function rmq_peek() {}
}
