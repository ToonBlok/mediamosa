<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

abstract class mediamosa_scheduler extends mediamosa_rest_call
{

  /**
   *
   *
   * @var
   */
  protected $queue_keepalive_server_guardian;
  protected $channel;
  protected $connection;
  const QUEUE_NAME = 'SCHEDULER';

  const MSG_KILL = 'KILL';
  const MSG_JOB = 'JOB';
  const MSG_START_JOB = 'START_JOB';
  const MSG_PROGRESSION = 'PROGRESSION';

  const MSG_ID = 'MSG_ID';
  const MSG_TYPE = 'MSG_TYPE';
  const QUEUE = 'QUEUE';
  const TIMESTAMP = 'TIMESTAMP';
  const UNIT_TEST_PREFIX = 'UNIT_TEST_PREFIX';
  const DATA = 'DATA';

  public function rmq_send($msg, $queue)
  {
    mediamosa_debug::log('Entered send()', array(), 'T - SCHEDULER');

    $this->channel->exchange_declare(mediamosa_messenger_jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($queue, false, true, false, false);
    $this->channel->queue_bind($queue, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $queue);

    $msg = new AMQPMessage(
      serialize($msg),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    $this->channel->basic_publish($msg, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $queue);

  }

  // Just a reused round_up_jobs() for the new start job after first job is completed feature
  function process_msg($msg)
  {
    $msg['MSG_ID'] = variable_get("alive_message_unique_id", 1);
    variable_set("alive_message_unique_id", $msg['MSG_ID']++);
    // If the message has this array that means its an update message
//      if (isset($msg['new_job_status'])) {
//        $this->update_progression($msg);
//      }

    if ($msg['MSG_TYPE'] == 'START_JOB') {
      mediamosa_debug::log('This msg arrived in the scheduler:', array(), 'T - MISC');
      mediamosa_debug::log_export($msg, WATCHDOG_NOTICE, 'T - MISC');


      $job = $msg['DATA'];
      mediamosa_debug::log('Unpacked job:', array(), 'T - MISC');
      mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - MISC');

      switch ($msg['DATA'][mediamosa_job_db::JOB_TYPE]) {
        case mediamosa_job_db::JOB_TYPE_ANALYSE:
          $msg['DATA'] = $this->enrich_job($msg['DATA']);
          break;
        case mediamosa_job_db::JOB_TYPE_STILL:
          $msg['DATA'] = $this->enrich_job($msg['DATA']);
          break;
        case mediamosa_job_db::JOB_TYPE_TRANSCODE:
          $msg['DATA'] = $this->enrich_job($msg['DATA']);
          break;
      }

      $msg['QUEUE'] = $msg['UNIT_TEST_PREFIX'] . $msg['DATA'][mediamosa_job_db::JOB_TYPE];

      mediamosa_debug::log(__CLASS__ . ' sent to queue: ' . $msg['QUEUE'] . ' this job: ', array(), 'T - RMQ');
      mediamosa_debug::log_export($msg, WATCHDOG_NOTICE, 'T - RMQ');

//      if ($msg['DATA'][mediamosa_job_db::JOB_TYPE] == mediamosa_job_db::JOB_TYPE_TRANSCODE) {
//        mediamosa_debug::log('Stopped TRANSCODE from happening', array(), 'T - RMQ');
//      } else {
      $this->rmq_send($msg, $msg['QUEUE']);
//      }


    } elseif ($msg['MSG_TYPE'] == 'PROGRESSION') {
      $this->update_progression($msg['DATA']);
    }

  }
//    mediamosa_debug::log('Entered process_next_job', array(), 'T - SCHEDULER');
//
//    switch ($job[mediamosa_job_db::JOB_TYPE]) {
//      case mediamosa_job_db::JOB_TYPE_ANALYSE:
//        $job['asset_link'] = $this->get_asset_link($job['job_id']);
//
//        break;
////      case mediamosa_job_db::JOB_TYPE_STILL:
////        $job_parameters = $this->get_job_parameters($job);
////
////        $job += array(
////          mediamosa_job_still_db::BLACKSTILL_CHECK => $job_parameters[mediamosa_job_still_db::BLACKSTILL_CHECK],
////          mediamosa_job_db::STILL_PARAMETERS => $job_parameters[mediamosa_job_db::STILL_PARAMETERS],
////          mediamosa_job_still_db::SIZE => $job_parameters[mediamosa_job_still_db::SIZE],
////          'asset_link' => $this->get_asset_link($job['job_id'])
////        );
////
//////          mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - SCHEDULER' );
///
////
////        break;
//      case mediamosa_job_db::JOB_TYPE_TRANSCODE:
//        mediamosa_debug::log('[' . $job[mediamosa_job_db::ID] . '] is asking for more parameters', array(), 'T - SCHEDULER');
//        mediamosa_debug::log('[' . $job[mediamosa_job_db::ID] . '] before receiving parameters: ', array(), 'T - SCHEDULER');
//        mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - SCHEDULER');
//        $transcode_parameters = $this->get_job_parameters($job);
//
//        // If retrieving transcode_parameters succeeded
//        if ($transcode_parameters) {
//          $job += array(
//            'transcode_parameters' => serialize($transcode_parameters)
//          );
//        } else {
//          // Should throw error here
//          return;
//        }
//
////        $job += array(
////          'mediafile_path' => mediamosa_storage::get_local_mediafile_path($job[mediamosa_job_db::MEDIAFILE_ID])
////        );
//
//        $job['asset_link'] = $this->get_asset_link($job['job_id']);
//        mediamosa_debug::log('[' . $job[mediamosa_job_db::ID] . '] after receiving parameters: ', array(), 'T - SCHEDULER');
//        mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - SCHEDULER');
//        break;
//    }
//
//    try {
//      // Scheduler to jobcore
//      $this->rmq_send($job);
//    } catch (Exception $e) {
//      mediamosa_debug::log($e, array(), 'T - Scheduler');
//    }

  function update_progression($job) {
    $job_type = $job[mediamosa_job_server_db::JOB_TYPE];
    $job_id = $job[mediamosa_job_server_db::JOB_ID];
    $mediafile_id_src = $job[mediamosa_job_db::MEDIAFILE_ID]; // hmmmmmmm
    $status = mediamosa_job_db::JOB_STATUS_INPROGRESS; // Default status
    $job_status = $job['new_job_status'];
    mediamosa_debug::log('$job when updating:', array(), 'T - JAN15');
    mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - JAN15');
    mediamosa_debug::log('$job after requesting db:', array(), 'T - JAN15');
    $job2 = mediamosa_job::get($job_id, array(mediamosa_job_db::MEDIAFILE_DEST));
    mediamosa_debug::log_export($job2, WATCHDOG_NOTICE, 'T - JAN15');

    // No status file found, we just have to wait.
    if (empty($job_status['Status'])) {
      mediamosa_job_server::log_debug_mediafile($mediafile_id_src, "No status file found with @statusfile for job @job_id, maybe next run.", array('@statusfile' => mediamosa_storage::get_realpath_status_file($job_id), '@job_id' => $job_id));
      return;
    }

    $unit_test_prefix = '';
    if (mediamosa::in_simpletest_sandbox()) {
      $unit_test_prefix = $GLOBALS['drupal_test_info']['test_run_id'] . '_';
    }

    switch ($job_type) {
      case $unit_test_prefix . mediamosa_job_server_db::JOB_TYPE_STILL:
        // Default status.
        $status = mediamosa_job_server_db::JOB_STATUS_INPROGRESS;

        // Scene still filename.
        $file_scene = mediamosa_storage::get_uri_scene_file($job_id);

        if (!mediamosa_io::file_exists($file_scene) && empty($job_status)) {
          // No status file found, we just have to wait.
          mediamosa_job_server::log_debug_mediafile($job['mediafile_id'], 'No status file found with name @name for job @job_id, maybe next run.', array('@name' => mediamosa_storage::get_realpath_status_file($job_id), '@job_id' => $job_id));
          return;
        }

        // Set defaults, to fix some possible notices.
        $job_status += array(
          'Status' => '',
          'Errors' => 'none',
          'Progress' => '0.000',
        );

        if (mediamosa_io::file_exists($file_scene) || ($job_status['Status'] == 'done' && $job_status['Errors'] == 'none')) {
          $status = $this->store_new_still($job);

          //mediamosa_debug::log('END RESULT OF STORE STILL: ' . $status, array(), 'T - STILL');

          if ($status == mediamosa_job_server_db::JOB_STATUS_INPROGRESS) {
            mediamosa_job_server::log_debug_mediafile($mediafile_id_src, 'Running @job_type job (storing file busy), Job ID @job_id, with status: @status', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status));
          } else {
            mediamosa_job_server::log_debug_mediafile($mediafile_id_src, 'End @job_type job, Job ID @job_id, with status: @status', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status));
          }
        } elseif ($job_status['Status'] == 'error' && $job_status['Errors'] != 'none') {
          $status = mediamosa_job_server_db::JOB_STATUS_FAILED;
          $link_asset = $this->get_asset_link($job_id);
          mediamosa_job_server::log_debug_mediafile($mediafile_id_src, 'End @job_type job, Job ID @job_id, with status: @status<br /><br />@link', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status, '@link' => $link_asset));
          mediamosa_job_server::log_debug_high_mediafile($mediafile_id_src, "Info @job_type job, Job ID @job_id, status file '@statusfile'", array('@job_type' => $job_type, '@job_id' => $job_id, '@statusfile' => $this->get_status_contents($job_id, TRUE)));
        }

        // Update the status.
        if (!mediamosa_io::file_exists($file_scene) && $job_status['Errors'] != 'none') {
          // Might be because there is no status file, dont bother to update.
          if (isset($job_status['Errors'])) {
            $this->set_job_status($job, $status, $job_status['Progress'], $job_status['Errors']);
          }
        } else {
          $this->set_job_status($job, $status, $job_status['Progress']);
        }

        break;
      case $unit_test_prefix . mediamosa_job_server_db::JOB_TYPE_TRANSCODE:
        if ($job_status['Status'] == 'done' && $job_status['Errors'] == 'none') {
          mediamosa_debug::log('Transcode done and errors are none.', array(), 'T - TRANSCODE');
          // Status to finished.
          $status = mediamosa_job_db::JOB_STATUS_FINISHED;

          // Store the transcode.
          $mediafile_id_dest = $this->store_new_mediafile($job);

          // Log it.
//          mediamosa_job_server::log_mediafile($mediafile_id_src, 'End job @job_type, Job ID: @job_id, status: @status', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status));

          // Set job status.
          $this->set_job_status($job, $status, $job_status['Progress']);

          // Generate event transcode finished.
          mediamosa::rules_invoke_event('mediamosa_event_transcode_finished', $mediafile_id_src);
//      mediamosa_debug::log('MY CODE: Invoked rules_invoke_event with mediafile_id_src: ' . $mediafile_id_src, array(), 'T - Transcode');

          $job = $this->get_job_where($job['job_id']);
          mediamosa_debug::log('$job after requesting when done:', array(), 'T - JAN15');
//          mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - JAN15');
          // Toon: This is completely somewhere else in the original code

          mediamosa_statistics::insert_job_transcode($mediafile_id_dest, $job['job_id']);
          $this->parse_finished_transcode($job['job_id'], $job['mediafile_id'], $mediafile_id_dest); // If the transcode job is completed, add it to the mediafile.

        } elseif ($job_status['Status'] == 'error' && (empty($job_status['Errors']) || $job_status['Errors'] != 'none')) {
          $status = mediamosa_job_server_db::JOB_STATUS_FAILED;
          $link_asset = mediamosa_job_server::get_asset_link($job_id);

          mediamosa_job_server::log_mediafile($mediafile_id_src, "End @job_type job, Job ID @job_id, with status: @status<br /><br />@link", array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status, '@link' => $link_asset));
          mediamosa_job_server::log_mediafile($mediafile_id_src, "Info @job_type job, Job ID @job_id, status file '@statusfile'", array('@job_type' => $job_type, '@job_id' => $job_id, '@statusfile' => mediamosa_job_server::get_status_contents($job_id, TRUE)));

          // Set status to failed.
          $this->set_job_status($job, $status, $job_status['Progress'], isset($job_status["ffmpeg-output"]) ? ($job_status["Errors"] != "" ? $job_status["Errors"] . "-\n" : '') . $job_status["ffmpeg-output"] : $job_status["Errors"]);

          // Generate event transcode failed.
          mediamosa::rules_invoke_event('mediamosa_event_transcode_failed', $mediafile_id_src);
        } else {
          // Set job status.
          $this->set_job_status($job, $status, $job_status['Progress']);
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
   *  Link to an asset.
   */
  function get_asset_link($job_id) {

    // Get the job.
    $jobserver_job = mediamosa_job::get($job_id);

    // Get asset ID from job.
    $asset_id = $jobserver_job[mediamosa_job_db::ASSET_ID];

    // Return link.
    return l(mediamosa::t('Go to asset @asset_id', array('@asset_id' => $asset_id)), mediamosa_settings::get_url_asset($asset_id));
  }

  /**
   * Update the job.
   *
   * Updates running jobs.
   *
   * 1. Get possible status file and parse it.
   * 2. When transcode or still job is done, files will be moved to the
   *    correct location.
   */
  // Toon: Modelled after running_job_update

  function get_status_contents($job_id, $orig = FALSE) {
    // Get the statusfile filename.
    $statusfile = mediamosa_storage::get_realpath_status_file($job_id);
    mediamosa_io::clearstatcache($statusfile);
    if (!mediamosa_io::file_exists($statusfile)) {
      mediamosa_job_server::log('Unable to load status contents; file @file does not exists.', array('@file' => $statusfile), WATCHDOG_CRITICAL);
      return array();
    }

    $result = array();
    $lines = array();

    // Set default.
    $result += array(
      'Errors' => 'none',
    );

    // FIXME: move to mediamosa_io
    $handle = @fopen($statusfile, 'r');
    if (is_resource($handle)) {
      while (($line = fgets($handle)) !== false) {
        $lines[] = $line;
      }
      fclose($handle);
    } else {
      mediamosa_job_server::log_debug('Unable to open status file using fopen; @file', array('@file' => $statusfile));
    }

    // Return the original?
    if ($orig) {
      return implode('', $lines);
    }

    // Strip the garbage from the file.
    foreach ($lines as $line) {
      if (mediamosa_unicode::strpos($line, ':') === FALSE) {
        continue;
      }

      list($name, $value) = explode(':', $line, 2);
      if ($name == 'Progress' && empty($value)) {
        $value = '0.000';
      } elseif ($name == 'Progress' || $name == 'Status' || $name == 'Errors') {
        $result[$name] = trim($value);
      } elseif ($name == 'ffmpeg-output') {
        $result[$name] = implode("\n", explode('}-{', trim($value)));
      }
    }

    // If there is no result we return empty array.
    if (!empty($result)) {
      // Set defaults, to fix some possible notices.
      $result += array(
        'Status' => '',
        'Errors' => 'none',
        'Progress' => '0.000',
      );
    }

    return $result;
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
  function store_new_still($job) {
    $job_type = $job[mediamosa_job_db::JOB_TYPE];
    $job_id = $job[mediamosa_job_db::ID];
    $mediafile_id_src = $job[mediamosa_job_db::MEDIAFILE_ID];

    $append_fields = mediamosa_job::get($job_id, array(mediamosa_job_db::APP_ID, mediamosa_job_db::HINT, mediamosa_job_db::STILL_PARAMETERS, mediamosa_job_db::OWNER_ID));
    $job = array_merge($job, $append_fields);
    //mediamosa_debug::log('$job array was enriched in ' . __CLASS__ . ', ' . __METHOD__,  array(), 'T - STILL');
    //mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - STILL' );

    $app_id = $job[mediamosa_job_db::APP_ID];

    $base_filename = mediamosa_io::get_base_filename($job_id);

    // Check if there really is an image ($file_size > 0)
    $filename = mediamosa_storage::get_uri_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, 1) . '.jpeg');

    if (!mediamosa_io::file_exists($filename) || !mediamosa_io::filesize($filename)) {
      mediamosa_debug::log('STILL creation failed, file does not exist. ' . __CLASS__ . ', ' . __METHOD__ . ', on line: ' . __LINE__, array(), 'T - STILL');
      // Something failed. Remove the files and fail the job.

      $still_error = mediamosa_error::error_code_find_description(mediamosa_error::ERRORCODE_STILL_IS_NOT_CREATABLE, array('@mediafile_id' => $mediafile_id_src));

      // Update status.
      //$this->set_job_status($job_id, mediamosa_job_db::JOB_STATUS_FAILED, $job_type, $mediafile_id_src,'1.000', $still_error);
      // My edited version of update
      $this->set_job_status($job, mediamosa_job_db::JOB_STATUS_FAILED, $still_error);


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
        $myvar = 1;
//        mediamosa_debug::log('Stills made: ' . $myvar, array(), 'T - STILL');
        $myvar += 1;
      } else {
        // Reached the maximum, just delete the remain stills.
        mediamosa_io::unlink(mediamosa_storage::get_realpath_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, $i) . '.jpeg'));
      }

      $i++;
    }
    mediamosa_io::unlink(mediamosa_storage::get_realpath_status_file($job_id));

//    mediamosa_debug::log_export($mediafile_dest, WATCHDOG_NOTICE, 'T - STILL' );
    // Data to update.
    $fields = array(
      // Toon: Original
      mediamosa_job_server_db::MEDIAFILE_DEST => serialize($mediafile_dest),
      // Toon: Modified
//      mediamosa_job_db::MEDIAFILE_DEST => $mediafile_dest,
    );
    // Contents are fine
    //Omediamosa_debug::log_export($fields, WATCHDOG_NOTICE, 'T - STILL' );


    // Add changed.
    $fields = mediamosa_db::db_update_enrich($fields);

//    $a = mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
//      ->fields($fields)
//      ->condition(mediamosa_job_db::ID, $job_id);
//    mediamosa_debug::log($a, array(), 'T - SCHEDULER');
//    mediamosa_debug::log_export($fields, WATCHDOG_NOTICE, 'T - SCHEDULER');

    // Update mediafile_dest of the job
//    mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
//      ->fields($fields)
//      ->condition(mediamosa_job_db::ID, $job_id)
//      ->execute();
//    mediamosa_debug::log('Crash before this', array(), 'T - SCHEDULER');

    mediamosa_db::db_update(mediamosa_job_server_db::TABLE_NAME)
      ->fields($fields)
      ->condition(mediamosa_job_server_db::JOB_ID, $job_id)
      ->execute();

    // Log it.
    mediamosa_job_server::log_mediafile($mediafile_id_src, 'Job (job_id: @job_id) finished: Multiple stills saved as e.g.: @filenames.', array('@job_id' => $job_id, '@filenames' => implode(',', $mediafile_dest)));

    // unserialize turns value into false so away it goes
    //$this->add_still_to_db($job, unserialize((string)$mediafile_dest)); // $job here is nothing but app_id and hint
    //mediamosa_debug::log_export($mediafile_dest, WATCHDOG_NOTICE, 'T - STILL' );

    // Bug #1: mediafile_dest has the wrong contents
    $this->add_still_to_db($job, $mediafile_dest); // $job here is nothing but app_id and hint


    return mediamosa_job_server_db::JOB_STATUS_FINISHED;
  }

  function enrich_job($job) {

    $job += array(
      'asset_link' => ''
    );

    $job['asset_link'] = $this->get_asset_link($job['job_id']);

    if (empty($job['asset_link'])) {
      mediamosa_debug::log('Error retrieving asset_link!', array(), 'T - MISC');
      // Something went wrong, we cannot proceed
    }

    mediamosa_debug::log('$job[asset_link]:', array(), 'T - 999');
    mediamosa_debug::log_export($job['asset_link'], WATCHDOG_NOTICE, 'T - 999');

    switch ($job[mediamosa_job_db::JOB_TYPE]) {
      case mediamosa_job_db::JOB_TYPE_ANALYSE:
        break;
      case mediamosa_job_db::JOB_TYPE_STILL:
        $still_parameters = unserialize($job[mediamosa_job_db::STILL_PARAMETERS]);
        mediamosa_debug::log('Before enrichment:', array(), 'T - 999');
        mediamosa_debug::log_export($still_parameters, WATCHDOG_NOTICE, 'T - 999');

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
          "fps" => $fps,
          "video_duration" => $video_duration
        );

        mediamosa_debug::log('After enrichment:', array(), 'T - 999');
        mediamosa_debug::log_export($still_parameters, WATCHDOG_NOTICE, 'T - 999');

        $job[mediamosa_job_db::STILL_PARAMETERS] = serialize($still_parameters);
        break;
      case mediamosa_job_db::JOB_TYPE_TRANSCODE:
        $job += array(
          'transcode_parameters' => ''
        );

        $job['transcode_parameters'] = $this->get_job_parameters($job);

//        $job += array(
//          'mediafile_path' => mediamosa_storage::get_local_mediafile_path($job[mediamosa_job_db::MEDIAFILE_ID])
//        );

        mediamosa_debug::log('Job ' . $job[mediamosa_job_db::ID] . ' after receiving parameters: ', array(), 'T - SCHEDULER');
        mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - SCHEDULER');

        if (empty($job['transcode_parameters'])) {
          mediamosa_debug::log('Error retrieving transcode_parameters!', array(), 'T - MISC');
        }
        break;
    }



    return $job;
  }

  /**
   * Process the data from a still job.
   *
   * @param int $job_id
   *   The job ID.
   * @param array $filenames
   *   The filenames of the stills.
   */
  public function add_still_to_db($job, $filenames) {
    mediamosa_debug::log('Entered ' . __CLASS__ . ', ' . __METHOD__, array(), 'T - STILL');
    // Vergeet niet dat job id to string werd gedaan
    $job_id = $job['job_id'];
    $asset_id = $job['asset_id'];
    $app_id = $job['app_id'];
    $owner = $job['owner'];
    $still_parameters = unserialize($job['still_parameters']);

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
      $job['mediafile_id'],
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
//      mediamosa_debug::log('$filenames was determined as an array.', array(), 'T - STILL');
      //mediamosa_debug::log_export($filenames, WATCHDOG_NOTICE, 'T - STILL' );

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
        mediamosa_asset_mediafile_still::create($asset_id, $filename, $app_id, $owner, '', $order, !$order, $still_parameters, ($scenes == array() ? $sec : $scenes[$i]), $job['mediafile_id'], $tag);

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

  function store_new_mediafile($job) {
    $job_id = $job['job_id'];
    $transcode_parameters = $job['transcode_parameters'];
//    $job = mediamosa_job::get($job_id, array(mediamosa_job_db::APP_ID, mediamosa_job_db::HINT)); // Get the job.

    $the_job = $this->get_job_where($job_id);
    $app_id = $the_job['app_id'];

//    $app_id = $job[mediamosa_job_db::APP_ID];
    $mediafile_id = mediamosa_db::uuid($job_id); // Generate new mediafile ID.
    $file_extension = $transcode_parameters[mediamosa_job_server_transcode_db::FILE_EXTENSION];
    mediamosa_debug::log('File extension was determined as: ' . $file_extension, array(), 'T - Transcode');

    // Cleared:
    // mediafile_id
    //
    // Get the filenames.
    $file_status_uri = mediamosa_storage::get_uri_status_file($job_id);
    $file_transcode_uri = mediamosa_storage::get_uri_temporary_file($job_id . '.' . $file_extension);
    $file_destination_uri = mediamosa_storage::create_local_mediafile_uri($app_id, $mediafile_id);

    mediamosa_debug::log('We called create_local_mediafile_uri with this $app_id: ' . $app_id . ', outcome is: ' . $file_destination_uri, array(), 'T - SCHEDULER');

    // Rename transcoded file to new dest.
    mediamosa_io::rename($file_transcode_uri, $file_destination_uri);

    // Now remove the status file.
    mediamosa_io::unlink($file_status_uri);

    $fields = array(
      mediamosa_job_server_db::MEDIAFILE_DEST => $mediafile_id,
    );

    mediamosa_debug::log('MEDIAFILE_ID: ' . $job['mediafile_id'], array(), 'T - SCHEDULER');
    mediamosa_debug::log('MEDIAFILE_DEST became: ' . $mediafile_id, array(), 'T - SCHEDULER');

    // Enrich with update date.
    $fields = mediamosa_db::db_update_enrich($fields);

    // Update the filename in mediafile_dest.
//    mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
//      ->fields($fields)
//      ->condition(mediamosa_job_db::ID, $job_id)
//      ->execute();

    // Log it.
//    mediamosa_job_server::log_mediafile($job_server[mediamosa_job_server_db::MEDIAFILE_ID_SRC], "Job with ID @job_id ready, new mediafile stored as '@uri' (@path).", array('@job_id' => $job_id, '@uri' => $file_destination_uri, '@path' => mediamosa_io::realpath($file_destination_uri)));
    return $mediafile_id;
  }

  function set_job_status($job, $job_status, $progress, $error_description = '', $error_description_args = array()) {

    mediamosa_job::set_job_status($job, $job_status, $progress, $error_description, $error_description_args);

    if (!mediamosa::in_simpletest_sandbox()) {

      // [] Job with an ID higher than this job and lower ID than all the other jobs
      // [] Must be same asset id
      // [x] Previous job must be finished
      // [] Chosen job must be waiting status
      // []

      if ($job_status == mediamosa_job_db::JOB_STATUS_FINISHED) {
//
//        $jobs = mediamosa_db::db_select(mediamosa_job_db::TABLE_NAME, 'mj')
//          ->fields('mj')
//          ->condition(mediamosa_job_db::ASSET_ID, $job[mediamosa_job_db::ASSET_ID])
//          ->orderBy('mj.job_id', 'ASC')
//          ->execute()
//          ->fetchAll();    // Hoe all krijgen?


        // If no job is already in progress
        $count_jobs_in_progress = mediamosa_db::db_select(mediamosa_job_db::TABLE_NAME, 'mj')
          ->fields('mj')
          ->condition(mediamosa_job_db::ASSET_ID, $job[mediamosa_job_db::ASSET_ID])
          ->condition(mediamosa_job_db::JOB_TYPE, mediamosa_job_db::JOB_TYPE_UPLOAD, '!=')
          ->condition(mediamosa_job_db::JOB_STATUS, mediamosa_job_db::JOB_STATUS_INPROGRESS)
          ->orderBy('mj.job_id', 'ASC')
          ->countQuery()
          ->execute()
          ->fetchField();
        mediamosa_debug::log(microtime(TRUE) . ' - Count jobs INPROGRESS: ' . $count_jobs_in_progress, array(), 'T - MISC');

        if ($count_jobs_in_progress == 0) {
          $next_job = mediamosa_db::db_select(mediamosa_job_db::TABLE_NAME, 'mj')
            ->fields('mj')
            ->condition(mediamosa_job_db::ASSET_ID, $job[mediamosa_job_db::ASSET_ID])
            ->condition(mediamosa_job_db::ID, $job[mediamosa_job_db::ID], '>')
            ->condition(mediamosa_job_db::JOB_TYPE, mediamosa_job_db::JOB_TYPE_UPLOAD, '!=')
            ->condition(mediamosa_job_db::JOB_STATUS, mediamosa_job_db::JOB_STATUS_WAITING)
            ->orderBy('mj.job_id', 'ASC')
            ->execute()
            ->fetchAssoc();    //

          if (!empty($job)) {

            mediamosa_debug::log('$next_job:', array(), 'T - ABC');
            mediamosa_debug::log_export($next_job, WATCHDOG_NOTICE, 'T - ABC');


            $unit_test_prefix = '';
            if (!is_null($GLOBALS['drupal_test_info']['test_run_id'])) {
              $unit_test_prefix = $GLOBALS['drupal_test_info']['test_run_id'];
            }

            $msg = [
              'MSG_ID' => $this->variable_get('alive_message_unique_id', 1),
              'MSG_TYPE' => 'START_JOB',
              'QUEUE' => $unit_test_prefix . mediamosa_scheduler::QUEUE_NAME,
              'TIMESTAMP' => microtime(TRUE),
              'UNIT_TEST_PREFIX' => $unit_test_prefix,
              'DATA' => $next_job
            ];

            $this->variable_set("alive_message_unique_id", $msg['MSG_ID']++);

            variable_set("alive_message_unique_id", $msg['MSG_ID']++);

            mediamosa_debug::log('Picked this msg to send next to process_msg()', array(), 'T - MISC');
            mediamosa_debug::log_export($msg, WATCHDOG_NOTICE, 'T - MISC');
            $this->process_msg($msg);

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
  // Toon: Een transcode heeft plaatsgevonden, er staat nog niets in de database wat er met de file moet gebeuren, er is alleen een transcode gedaan
  // Zorgt ervoor dat een rij in mediamosa_asset_mediafile tabel terechtkomt, zodat er een verwijzing is naar die file en dat die bij die asset hoort.
  // Dan is hij bekend binnen MM, als we dat niet doen dan blijft de file staan en dan doen we er niets mee. Zo wordt hij zichtbaar.
  function parse_finished_transcode($job_id, $mediafile_id_src, $mediafile_id_dest) {
    mediamosa_debug::log('Transcode successfully finished.', array(), 'T - Scheduler');
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

    // This must go trough my core!!!!!!!!!!!!!!!!!!!!!!!!!!
    // Analyse the mediafile.
    $analyse_result = mediamosa_job_server::analyse((string)$mediafile_id_dest, $job_id);

    // Store it.
    $this->store_analyse_without_job($job_id, $analyse_result, (string)$mediafile_id_dest);
  }

  /**
   * Called when transcode failed or was canceled.
   *
   * @param int $job_id
   *   The job ID.
   */
  public function parse_failed_transcode($job_id) {
    mediamosa_debug::log('Transcode failed.', array(), 'T - Scheduler');
    // Get original.
    $job_original = mediamosa_job::get($job_id);
    $status = $job_original[mediamosa_job_db::JOB_STATUS];

    // Set failed.
    mediamosa_job::set_progress($job_id, '1.000', FALSE, mediamosa_job_db::JOB_STATUS_FAILED);

    // Trigger url.
    mediamosa_job::notify_transcoding($status, mediamosa_job_db::JOB_STATUS_FAILED, $job_id);
  }

  // Toon: De front end wilt soms weten wanneer de transcode klaar is, dat doet hij door een notify url mee te geven.
  public function notify_transcoding($new_status, $job_id) {
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
//      mediamosa_debug::log('Call to completed transcoding URL: ' . $completed_transcoding_url . $new_status, array(), 'Toon - TRANSCODE');

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
  public function store_analyse_without_job($job_id, array $analyse_result, $mediafile_id) {
    // Parse and store metadata from the analyse result.
    $ffmpeg_output = mediamosa_asset_mediafile_metadata::store_metadata($mediafile_id, $analyse_result);

    // Store in error description.
    mediamosa_job::store_error_description($job_id, $ffmpeg_output);

//    mediamosa_debug::log('$mediafile_id (which was mediafile_dest):', array(), 'T - JAN15');
//    mediamosa_debug::log_export($mediafile_id, WATCHDOG_NOTICE, 'T - JAN15');

    $job = mediamosa_job::get($job_id, array(mediamosa_job_db::MEDIAFILE_DEST));
//    mediamosa_debug::log('$job which was just requested from DB:', array(), 'T - JAN15');
//    mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - JAN15');


//    // Have to skip and do retranscode in a different way later
//    // Is it a retranscode job?
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

  // Toon: Is modelled exactly after the real method
  function get_job_parameters($job) {
    $job_id = $job[mediamosa_job_db::ID];
    $mediafile_id = $job[mediamosa_job_db::MEDIAFILE_ID];
    $job_type = $job[mediamosa_job_db::JOB_TYPE];

    mediamosa_debug::log('Triggered get_job_parameters', array(), 'T - SCHEDULER');
    mediamosa_debug::log('$job_id: ' . $job_id, array(), 'T - SCHEDULER');
    mediamosa_debug::log('$mediafile_id: ' . $mediafile_id, array(), 'T - SCHEDULER');
    mediamosa_debug::log('$job_type: ' . $job_type, array(), 'T - SCHEDULER');

    $result = array();

    switch ($job_type) {

      case mediamosa_job_db::JOB_TYPE_TRANSCODE:
        // Get job transcode.
        $job_transcode = mediamosa_job_transcode::get($job_id);

        $result['profile_id'] = $job_transcode[mediamosa_job_transcode_db::TRANSCODE_PROFILE_ID];
        $result['tool'] = $job_transcode[mediamosa_job_transcode_db::TOOL];
        $result['file_extension'] = $job_transcode[mediamosa_job_transcode_db::FILE_EXTENSION];
        $result['command'] = mediamosa_job_scheduler::map_parameters($job_transcode[mediamosa_job_transcode_db::TOOL], $job_transcode[mediamosa_job_transcode_db::COMMAND], $mediafile_id);

        mediamosa_debug::log('Result:', array(), 'T - SCHEDULER');
        mediamosa_debug::log_export($result, WATCHDOG_NOTICE, 'T - SCHEDULER');

        if ($result['tool'] == NULL) {
          return FALSE;
        }

        break;

      case mediamosa_job_db::JOB_TYPE_STILL:
//        $query_job = mediamosa_job_still::get($job_id);
//
//        if ($query_job) {
//          $result['blackstill_check'] = $query_job['blackstill_check'];
//          $result['still_parameters'] = unserialize($query_job['still_parameters']);
//          $result['frametime'] = $result['still_parameters']['frametime'];
//          $result['h_padding'] = $result['still_parameters']['h_padding'];
//          $result['v_padding'] = $result['still_parameters']['v_padding'];
//          $result['tag'] = $result['still_parameters']['tag'];
//
//          $mediafile = mediamosa_asset_mediafile::get($mediafile_id, NULL, array(mediamosa_asset_mediafile_db::APP_ID));
//          $app_id = $mediafile[mediamosa_asset_mediafile_db::APP_ID];
//
//          // Pre-defined ratios.
//          $sizes = array(
//            'sqcif' => '128x96', 'qcif' => '176x144', 'cif' => '352x288', '4cif' => '704x576',
//            'qqvga' => '160x120', 'qvga' => '320x240', 'vga' => '640x480', 'svga' => '800x600',
//            'xga' => '1024x768', 'uxga' => '1600x1200', 'qxga' => '2048x1536', 'sxga' => '1280x1024',
//            'qsxga' => '2560x2048', 'hsxga' => '5120x4096', 'wvga' => '852x480', 'wxga' => '1366x768',
//            'wsxga' => '1600x1024', 'wuxga' => '1920x1200', 'woxga' => '2560x1600',
//            'wqsxga' => '3200x2048', 'wquxga' => '3840x2400', 'whsxga' => '6400x4096',
//            'whuxga' => '7680x4800', 'cga' => '320x200', 'ega' => '640x350', 'hd360' => '640x360',
//            'hd480' => '852x480', 'hd720' => '1280x720', 'hd1080' => '1920x1080',
//          );
//
//          // Find target size.
//          if (isset($sizes[$query_job['size']])) {
//            $target_size = $sizes[$query_job['size']];
//          } else {
//            // Use size of still parameters instead.
//            $target_size = $result['still_parameters']['size'];
//
//            // Check the size.
//            if (!preg_match('/(\d+)x(\d+)/', $target_size)) {
//              // If there is a still default size for the client app available,
//              // then use that instead.
//              $target_size = mediamosa_app::get_still_default_size($app_id);
//              if (!$target_size) {
//                // Get the video size.
//                $target_size = mediamosa_asset_mediafile::get_size($mediafile_id);
//              }
//            }
//          }
//
//          // First get source width and height.
//          $metadata = mediamosa_asset_mediafile_metadata::get_with_mediafileid($mediafile_id, array(
//            array(
//              'prop_name' => mediamosa_asset_mediafile_metadata::WIDTH,
//              'type' => mediamosa_asset_mediafile_metadata_property_db::TYPE_INT,
//            ),
//            array(
//              'prop_name' => mediamosa_asset_mediafile_metadata::HEIGHT,
//              'type' => mediamosa_asset_mediafile_metadata_property_db::TYPE_INT,
//            ),
//          ));
//          $width = $metadata[mediamosa_asset_mediafile_metadata::WIDTH];
//          $height = $metadata[mediamosa_asset_mediafile_metadata::HEIGHT];
//          $still_padding = mediamosa_app::get_still_padding_value($app_id);
//
//          // Get the parameter settings.
//          $calc_aspect_ratio = mediamosa_gd::calcAspectRatio($width, $height, $target_size, $result['h_padding'], $result['v_padding'], $still_padding == mediamosa_app_db::STILL_PADDING_YES);
//
//          // Set result.
//          if ($calc_aspect_ratio) {
//            $result['size'] = $calc_aspect_ratio['width'] . 'x' . $calc_aspect_ratio['height'];
//            $result['h_padding'] = $calc_aspect_ratio['h_padding'];
//            $result['v_padding'] = $calc_aspect_ratio['v_padding'];
//          } else {
//            if ($width && $height) {
//              $result['size'] = $width . 'x' . $height;
//            } else {
//              $result['size'] = '640x360';
//            }
//            $result['h_padding'] = 0;
//            $result['v_padding'] = 0;
//          }
//        } else {
//          mediamosa_debug::log('Something went wrong in the analyse script!', array(), 'T - STILL');
//          // Something went wrong in the analyse script.
//          // Fall back to the default values.
//          $result['frametime'] = mediamosa_settings::STILL_DEFAULT_FRAME_TIME;
//          $result['size'] = '640x360';
//          $result['h_padding'] = 0;
//          $result['v_padding'] = 0;
//          $result['blackstill_check'] = 'FALSE';
//          $result['tag'] = '';
//        }
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
      self::log('Could not find original mediafile for job_id @job_id <br /><br />@link', array('@job_id' => $job_id, '@link' => $link));
    }

    return $result;
  }

  function get_inprogress_jobs() {
    $sql = 'SELECT job_id, job_type, asset_id, mediafile_id, app_id, status, mediafile_dest
          FROM {#mediamosa_job} AS mj
          WHERE mj.#job_status = :status';

    $sql = strtr($sql, array(
      '#job_status' => mediamosa_job_db::JOB_STATUS,
      '#mediamosa_job' => mediamosa_job_db::TABLE_NAME
    ));

    return mediamosa_db::db_query($sql,
      array(
        ':status' => mediamosa_job_db::JOB_STATUS_INPROGRESS
      )
    )->fetchAll();
  }

  function get_job_where($job_id, $asset_id = '') {

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

      mediamosa_debug::log('$sql: ' . $sql, array(), 'T - dec15');

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

//  // BACKUP
//  function get_job_where2($job_id, $asset_id) {
//    $sql = 'SELECT job_id, job_type, asset_id, mediafile_id, app_id, status, mediafile_dest
//            FROM {#mediamosa_job} AS mj
//            WHERE mj.#job_id = :id AND
//                  mj.#asset_id = :asset_id';
//
//    $sql = strtr($sql, array(
//      '#job_id' => mediamosa_job_db::ID,
//      '#asset_id' => mediamosa_job_db::ASSET_ID,
//      '#mediamosa_job' => mediamosa_job_db::TABLE_NAME
//    ));
//
//    return mediamosa_db::db_query($sql,
//      array(
//        ':id' => $job_id,
//        ':asset_id' => $asset_id
//      )
//    )->fetch();
//
//  }


  function rmq_open_connection() {

    $host = $this->variable_get('mediamosa_scheduler_host', 'localhost');
    $port = $this->variable_get('mediamosa_scheduler_port', 5672);
    $username = $this->variable_get('mediamosa_scheduler_username', 'guest');
    $password = $this->variable_get('mediamosa_scheduler_password', 'guest');

    $this->connection = new AMQPStreamConnection($host, $port, $username, $password);
    $this->channel = $this->connection->channel();
  }

  function rmq_close_connection() {
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

/**
 * URI: scheduler/start
 * Method: GET
                          */
class mediamosa_scheduler_start extends mediamosa_scheduler
{

  const QUEUE = 'queue';
  const QUEUE_KEEPALIVE_SERVER = 'queue_keepalive_server';
  const QUEUE_KEEPALIVE_SERVER_GUARDIAN = 'queue_keepalive_server_guardian';
  const CORE_TYPE = 'core_type';

  const QUEUE_KEEPALIVE = 'scheduler_keepalive';
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
          self::VAR_DESCRIPTION => "",
        ),
        self::QUEUE_KEEPALIVE_SERVER => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => "",
        ),
        self::QUEUE_KEEPALIVE_SERVER_GUARDIAN => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => "",
        ),
        self::CORE_TYPE => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => "",
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
    exec('echo `date` >> /home/tony/test.txt');
    exec('echo Scheduler start triggered >> /home/tony/test.txt');

    try {
      $queue = $this->get_param_value(self::QUEUE);
      $this->queue_keepalive_server = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER);
      $this->queue_keepalive_server_guardian = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER_GUARDIAN); // Queue that scheduler needs to send messages BACK to guardian.

//      $this->queue_keepalive_server_guardian = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER_GUARDIAN); // Queue that scheduler needs to send messages BACK to guardian.
//      $queue_keepalive_server = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER); // Queue that scheduler needs to receive messages in from guardian.

//      $this->check_resume_jobs();

      $this->listen($queue);
      mediamosa_debug::log('Started listening', array(), 'T - SCHEDULER');
    } catch (Exception $e) {
      mediamosa_debug::log($e, array(), 'T - SCHEDULER');
      mediamosa_debug::log('Error while starting listening', array(), 'T - SCHEDULER');
    }

    $mediamosa->add_item(
      array(
        'version' => mediamosa_version::get_current_version_str(FALSE)
      )
    );
  }


//  function check_resume_jobs() {
//    // Get jobs that are in progress
//    $jobs = $this->get_inprogress_jobs();
//    mediamosa_debug::log('Rounding up INPROGRESS jobs', array(), 'T - SCHEDULER');
//    mediamosa_debug::log_export($jobs, WATCHDOG_NOTICE, 'T - SCHEDULER');
//
//    if (!empty($jobs)) {
//      $this->rmq_open_connection();
//      foreach ($jobs as $job) {
//        $this->rmq_send($job);
//      }
//      $this->rmq_close_connection();
//    }
//  }


  function listen($queue) {
    $this->rmq_open_connection();
    $this->channel->exchange_declare(mediamosa_messenger_jobcore::EXCHANGE_JOBS, 'direct', false, false, false);

    $this->channel->queue_declare($queue, false, true, false, false);
    $this->channel->queue_bind($queue, mediamosa_messenger_jobcore::EXCHANGE_JOBS, self::QUEUE_NAME);

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

      $rmq_msg->delivery_info['channel']->basic_ack($rmq_msg->delivery_info['delivery_tag']);
      $msg = unserialize($rmq_msg->body);

      mediamosa_debug::log($msg['MSG_ID'] . ' - $callback triggered, $msg:', array(), 'T - SCHEDULER');
      mediamosa_debug::log_export($msg, WATCHDOG_NOTICE, 'T - SCHEDULER');

      if ($msg['MSG_TYPE'] == 'KILL') {
        $this->channel->basic_cancel($rmq_msg->delivery_info['consumer_tag']);
        exit(); // Don't know if belongs here
      } else {
        $this->process_msg($msg);
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
      exec('echo keepalive_callback triggered at `date` >> /home/tony/test.txt');
      mediamosa_debug::log('$keepalive_callback triggered', array(), 'T - SCHEDULER');
      mediamosa_debug::log_export($rmq_msg, WATCHDOG_NOTICE, 'T - SCHEDULER');

      try {
        $rmq_msg->delivery_info['channel']->basic_ack($rmq_msg->delivery_info['delivery_tag']);
      } catch (Exception $e) {
        mediamosa_debug::log($e, array(), 'T - SCHEDULER');
      }

      $msg = unserialize($rmq_msg->body);
      if ((isset($msg['sender'])) and ($msg['sender'] == 'unit_tests')) {
        $job = [
          "job_type" => mediamosa_messenger_jobcore::QUEUE_TESTS
        ];
        $this->rmq_send($job, $job['type']);
      } else {
        mediamosa_debug::log('Scheduler is responding to keepalive message.', array(), 'T - SCHEDULER');
        //$msg->delivery_info['channel']->basic_ack($msg->delivery_info['delivery_tag']);
//
        $update_message['timestamp'] = microtime(true);
        $update_message['job_type'] = $this->queue_keepalive_server_guardian;
//      $update_message['job_type'] = 'bla';
//      mediamosa_debug::log('Sent 1 message at ' . microtime(true), array(), 'T - GUARDIAN');
        $this->rmq_send($update_message, $update_message['job_type']);
      }
    };

    // basic_qos: Tells RabbitMQ not to give more than one message to a worker at a time.
    $this->channel->basic_qos(null, 1, null);
    $this->channel->basic_consume($queue, '', false, false, false, false, $callback);
    $this->channel->basic_consume($this->queue_keepalive_server, '', false, false, false, false, $keepalive_callback);

    exec('echo Started waiting at `date` >> /home/tony/test.txt');
    while (count($this->channel->callbacks)) {
      $this->channel->wait();
    }
    exec('echo Stopped waiting at `date` >> /home/tony/test.txt');

    $this->rmq_close_connection();
  } // LISTEN

}
