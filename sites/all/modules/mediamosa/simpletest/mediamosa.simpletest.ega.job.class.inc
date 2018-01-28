<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class MediaMosaTestCaseEgaJob extends MediaMosaTestCaseEga {

  protected $unit_test_prefix;
  protected $core_types;
  // -------------------------------------------------------------- Constructor.
  /**
   * Implement contructor.
   */
  public function __construct($test_id = NULL) {
    parent::__construct($test_id);
    $this->skipClasses[__CLASS__] = TRUE;
  }

  // ---------------------------------------------------------------- Functions.
  /**
   * Implements setUp().
   */
  protected function setUp() {
    // Get from outside sandbox.
    $mediamosa_jobscheduler_uri = variable_get('mediamosa_jobscheduler_uri', NULL);

    // Run parent first so we are inside sandbox.
    // Call parent::setUp and preserve arguments.
    $args = func_get_args();

    // Create and setup the CORE module.
//    $args = array_unique(array_merge(array('mediamosa_messenger'), $args));
    $args = array_unique(array_merge(array('mediamosa_job', 'mediamosa_tool_ffmpeg'), $args));
    if (drupal_substr(phpversion(), 0, 3) < '5.3') {
      call_user_func_array(array($this, 'parent::setUp'), $args);
    }
    else {
      call_user_func_array('parent::setUp', $args);
    }

    // Set jobserver selection.
    if (!empty($mediamosa_jobscheduler_uri)) {
      variable_set('mediamosa_jobscheduler_uri', $mediamosa_jobscheduler_uri);
    }

    $this->unit_test_prefix = $GLOBALS['drupal_test_info']['test_run_id'];
    variable_set('mediamosa_scheduler_username', 'admin');
    variable_set('mediamosa_scheduler_password', 'admin');
  }

  /**
   * Implements tearDown().
   */
  protected function tearDown() {

    // Dump job table contents.
    $this->dump_jobs();
    $this->rmq_clear($this->core_types);


    // Call parent.
    parent::tearDown();
  }

  /**
   * Dump the log so we know what happened.
   */
  protected function dump_jobs() {
    // Dump the log.
    $result = db_query('SELECT * FROM {mediamosa_job}');

    $texts = array('Dumping contents of job and server-job table');

    foreach ($result as $log_item) {
      $texts[] = highlight_string('<?php ' . var_export($log_item, TRUE), TRUE);
    }

    $result = db_query('SELECT * FROM {mediamosa_server_job}');
    $texts[] = strtr('Dumping contents (@total) of job server table', array('@total' => $result->rowCount()));
    foreach ($result as $log_item) {
      $texts[] = highlight_string('<?php ' . var_export($log_item, TRUE), TRUE);
    }

    // In verbose mode only.
    $this->verbose(implode('<br />', $texts), 'job main dump');

    $texts = array('Dumping contents of job tables');

    $result = db_query('SELECT * FROM {mediamosa_job_still}');
    if ($result->rowCount()) {
      $texts[] = 'Dumping contents of job still server table';
      foreach ($result as $log_item) {
        $texts[] = highlight_string('<?php ' . var_export($log_item, TRUE), TRUE);
      }
    }

    $result = db_query('SELECT * FROM {mediamosa_job_transcode}');
    if ($result->rowCount()) {
      $texts[] = 'Dumping contents of job transcode table';
      foreach ($result as $log_item) {
        $texts[] = highlight_string('<?php ' . var_export($log_item, TRUE), TRUE);
      }
    }

    $result = db_query('SELECT * FROM {mediamosa_job_upload}');
    if ($result->rowCount()) {
      $texts[] = 'Dumping contents of job upload table';
      foreach ($result as $log_item) {
        $texts[] = highlight_string('<?php ' . var_export($log_item, TRUE), TRUE);
      }
    }
    // In verbose mode only.
    $this->verbose(implode('<br />', $texts), 'job tables dump');

    $texts = array('Dumping contents of job server tables');

    $result = db_query('SELECT * FROM {mediamosa_job_server}');
    if ($result->rowCount()) {
      $texts[] = 'Dumping contents of job server table';
      foreach ($result as $log_item) {
        $texts[] = highlight_string('<?php ' . var_export($log_item, TRUE), TRUE);
      }
    }

    $result = db_query('SELECT * FROM {mediamosa_job_server_analyse}');
    if ($result->rowCount()) {
      $texts[] = 'Dumping contents of job server analyse table';
      foreach ($result as $log_item) {
        $texts[] = highlight_string('<?php ' . var_export($log_item, TRUE), TRUE);
      }
    }

    $result = db_query('SELECT * FROM {mediamosa_job_server_still}');
    if ($result->rowCount()) {
      $texts[] = 'Dumping contents of job server still table';
      foreach ($result as $log_item) {
        $texts[] = highlight_string('<?php ' . var_export($log_item, TRUE), TRUE);
      }
    }

    $result = db_query('SELECT * FROM {mediamosa_job_server_transcode}');
    if ($result->rowCount()) {
      $texts[] = 'Dumping contents of job server table';
      foreach ($result as $log_item) {
        $texts[] = highlight_string('<?php ' . var_export($log_item, TRUE), TRUE);
      }
    }

    // In verbose mode only.
    $this->verbose(implode('<br />', $texts), 'job server dump (these should be empty)');
  }

  protected function start_jobcores($core_types){

    exec('echo start_jobcores triggered >> /home/tony/test.txt');
    // Fixed values because this is the server setup we need for this test.
    $installation_id = 'job1';

    $this->messenger_log('unit_test_prefix', $this->unit_test_prefix);


    for ($i = 0; $i < count($core_types); $i++) {
      $core_type = $core_types[$i][0]; // HAS TO BE 0 for now because for now only 1 task


      $query_data[mediamosa_messenger_jobcore::QUEUE] = $this->unit_test_prefix . '_' . $core_type;
      $query_data[mediamosa_messenger_jobcore::QUEUE_KEEPALIVE_SERVER] = $this->unit_test_prefix . '_' . $core_type . '_' . $installation_id . '_' . $i;
      $query_data[mediamosa_messenger_jobcore::CORE_TYPE] = $core_type;

      $uri = 'guardian/check';

      $this->restCallGet($uri, $query_data);
    }
  }

  protected function rmq_open_connection() {
    $host = variable_get('mediamosa_scheduler_host', 'localhost');
    $port = variable_get('mediamosa_scheduler_port', 5672);
    $username = variable_get('mediamosa_scheduler_username', 'guest');
    $password = variable_get('mediamosa_scheduler_password', 'guest');

    $this->connection = new AMQPStreamConnection($host, $port, $username, $password);
    $this->channel = $this->connection->channel();
  }

  protected function rmq_close_connection() {
    $this->channel->close();
    $this->connection->close();
  }

  protected function messenger_log($name, $value) {
    $this->var_export($name . ':');
    $this->var_export($value); // /scheduler/job_start
  }

  public function rmq_send($msg, $queue) {
    $this->channel->exchange_declare('jobs', 'direct', false, false, false);
    $this->channel->queue_declare($queue, false, true, false, false);
    $this->channel->queue_bind($queue, 'jobs', $queue);

    $msg = new AMQPMessage(
      json_encode( $msg),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    $this->channel->basic_publish($msg, 'jobs', $queue);
  }
  public function rmq_clear($core_types) {

    $this->rmq_open_connection();
    $installation_id = 'job1';


    for ($i = 0; $i < count($core_types); $i++) {
      $core_type = $core_types[$i][0];
      $queue = $this->unit_test_prefix . '_' . $core_type;
      $queue_keepalive_server = $this->unit_test_prefix . '_' . $core_type . '_' . $installation_id . '_' . $i;
      $queue_keepalive_server_guardian = $queue_keepalive_server . '_guardian';

      // Send kill message to cores
      $msg = [
        mediamosa_messenger_scheduler::MSG_TYPE => 'KILL'
      ];
      $this->rmq_send($msg, $queue);

      // Queues need to exists if we want the kill message to arrive. Delay delete statements below.
      sleep(5);

      $this->channel->queue_delete($queue);
      $this->channel->queue_delete($queue_keepalive_server);
      $this->channel->queue_delete($queue_keepalive_server_guardian);
    }

    $this->rmq_close_connection();


  }

  public function rmq_peek($queue_keepalive_server_guardian) {

    $this->channel->exchange_declare('jobs', 'direct', false, false, false);
    $this->channel->queue_declare($queue_keepalive_server_guardian, false, true, false, false);
    $this->channel->queue_bind($queue_keepalive_server_guardian, 'jobs', $queue_keepalive_server_guardian);


    $rmq_msg = $this->channel->basic_get($queue_keepalive_server_guardian);
    if (!is_null($rmq_msg)) {
      $msg = (array) json_decode($rmq_msg->body);
      try {
        $this->channel->basic_ack($rmq_msg->delivery_info['delivery_tag']);
      } catch (Exception $e) {
        // Called ack on empty message, something wrong with the messages being sent.
        throw new mediamosa_exception_error(mediamosa_error::ERRORCODE_STARTING_JOB_FAILED);
      }

      return TRUE;
    }


    return FALSE;
  }

  protected function wait_jobcores_online($core_types){
    // Start with an assumption that we will evaluate later on.
    $all_servers_online = TRUE;

    $this->rmq_open_connection();

    for ($i = 0; $i < 24; $i++) {
      // Unique identifier
      $installation_id = 'job1';
      // Standard server setup
//      $core_types = [
//        ['SCHEDULER'],
//        ['ANALYSE'],
//        ['TRANSCODE'],
//        ['STILL']
//      ];

      // Send a msg to the keepalive queue of each core
      for ($i = 0; $i < count($core_types); $i++) {
        $core_type = $core_types[$i][0];
        $queue_keepalive_server = $this->unit_test_prefix . '_' . $core_type . '_' . $installation_id . '_' . $i;
        $queue_keepalive_server_guardian = $queue_keepalive_server . '_guardian';

        $msg_alive = [
          "MSG_ID" => variable_get('UNIQUE_MSG_ID', 1),
          "STATUS_REQUEST" => "pending",
          "STATUS" => "undetermined"
        ];

        variable_set('UNIQUE_MSG_ID', $msg_alive[mediamosa_messenger_scheduler::MSG_ID]++);

        $this->rmq_send($msg_alive, $queue_keepalive_server);

        // Allow some time for msg to arrive and simultaneously act as a small delay in general.
        sleep(5);

        // We give each server a chance to set the $all_servers_online variable to FALSE.
        // If a server sets the variable to FALSE, we know we must wait longer or the setup has failed already.
        $server_online = $this->rmq_peek($queue_keepalive_server_guardian);
        if (!$server_online) {
          $all_servers_online = FALSE;
          break;
        }
      }

      $this->rmq_close_connection();

      // Return the final evaluated statement. If no server set this statement to FALSE we can assume
      // that all servers are online
      return $all_servers_online;

    }

  }

  protected function peek_all()
  {
    $installation_id = 'job1';
    $core_types = [
      ['SCHEDULER'],
      ['ANALYSE'],
      ['TRANSCODE'],
      ['STILL']
    ];

    for ($i = 0; $i < count($core_types); $i++) {
      $core_type = $core_types[$i][0];
      $queue = $this->unit_test_prefix . '_' . $core_type . '_' . $installation_id . '_' . $i;

      $msg_alive = [
        "MSG_ID" => variable_get('UNIQUE_MSG_ID', 1),
        "STATUS_REQUEST" => "pending",
        "STATUS" => "undetermined"
      ];

      variable_set('UNIQUE_MSG_ID', $msg_alive[mediamosa_messenger_scheduler::MSG_ID]++);

      $this->rmq_send($msg_alive, $queue);

    }

  }

  /**
   *
   * Before we start a job we have to check if all servers (scheduler and all cores) are online. We do this by sending asking
   * the jobcore if it is alive and receiving back a confirmation.
   *
   **/
//  protected function send_all()
//  {
//    // Arrange
//    $installation_id = 'job1';
//    $core_types = [
//      ['SCHEDULER'],
//      ['ANALYSE'],
//      ['TRANSCODE'],
//      ['STILL']
//    ];
//
//    // Act
//    $this->rmq_open_connection();
//    for ($i = 0; $i < count($core_types); $i++) {
//      $core_type = $core_types[$i][0]; // HAS TO BE 0 for now because for now only 1 task
//      $keepalive_queue_name = $core_type . '_' . $installation_id . '_' . $i;
//      $queue = $this->unit_test_prefix . '_' . $core_type . '_' . $installation_id . '_' . $i;
//
//      $job = [
//        "queue" => $keepalive_queue_name,
//        "in_sandbox" => TRUE
//      ];
//
////      $this->rmq_send($job);
//      // Assert
//
//      // Every server check has the possibility to set this variable to FALSE. If the variable gets set to FALSE once during the loop,
//      // we can assume that right now the server setup has either failed or is not ready yet and we will check again in 5 seconds.
//      $all_servers_online = $this->rmq_peek();
//      if (!$all_servers_online) {
//        break;
//      }
//    }
//
//    return;
//
//    $this->rmq_close_connection();
//  }


  /**
   * Create a transcode job.
   */
  protected function createTranscodeJob($job_id, $fields = array()) {
    $fields[mediamosa_job_transcode_db::JOB_ID] = $job_id;

    $fields += array(
      mediamosa_job_transcode_db::TRANSCODE_PROFILE_ID => $this->getDefaultProfileId($this->a_app['app_id']),
      mediamosa_job_transcode_db::TOOL => '',
      mediamosa_job_transcode_db::COMMAND => '',
      mediamosa_job_transcode_db::FILE_EXTENSION => '',
      mediamosa_job_transcode_db::COMPLETED_TRANSCODING_URL => '',
    );

    $fields = mediamosa_db::db_insert_enrich($fields);

    $fields[mediamosa_job_transcode_db::ID] = mediamosa_db::db_insert(mediamosa_job_transcode_db::TABLE_NAME)->fields($fields)->execute();
    return $fields[mediamosa_job_transcode_db::ID];
  }

  /**
   * Get the information about a job using REST.
   *
   * Uses REST /job/$job_id [GET].
   *
   * @param int $job_id
   *   The Job ID.
   *
   * @return mediamosa_connector_response
   *   The mediamosa response class.
   */
  protected function getJob($job_id, array $parameters = array(), array $expected_result_ids = array(mediamosa_error::ERRORCODE_OKAY), $do_internal = FALSE) {
    // Parameters asset.
    $parameters += array(
      mediamosa_rest_call_job_create_still_for_mediafile::USER_ID => self::SIMPLETEST_USER_ID,
    );

    // Set the URI.
    $uri = strtr('job/@job_id/status', array('@job_id' => $job_id));

    // Do Post call.
    $response = $this->restCallGet($uri, $parameters, array(), $expected_result_ids, $do_internal);

    $this->var_export($response);

    return $response['xml'];
  }

  /**
   * Basic upload of test file.
   *
   * @param $options
   *   Options to setup the upload.
   *   - filename (optional)
   *     Filename of the file to upload. Will default to ogg test file.
   *   - asset_id (optional)
   *     The asset to use for the upload.
   *   - use_put (FALSE)
   *     Use PUT as method to test.
   *   - downloadable (FALSE)
   *     Make mediafile downloadable.
   *   - create_still (FALSE)
   *     Create still after upload.
   *   - transcode_inherits_acl (FALSE)
   *     Make transcodes for mediafile same ACL rights as original.
   *   - transcode (empty array)
   *     Array of transcode profile ids to use to transcode the uploaded file.
   *
   * @see MediaMosaTestCaseEga::uploadTestFile()
   */
  protected function uploadTestFile($options = array()) {

    // Call parent.
    $result = parent::uploadTestFile($options);

    // Any options need job processing?
    if (!empty($options['create_still']) || !empty($options['transcode'])) {
      // Parse the queue.
      $this->doQueueCycleAll();
    }

    return $result;
  }

  /**
   * Create still.
   *
   * Based on REST call /mediafile/$mediafile_id/still/create (POST)
   */
  protected function createMediafileStill($mediafile_id, array $parameters = array(), array $expected_result_ids = array(mediamosa_error::ERRORCODE_OKAY), $do_internal = FALSE) {
    // Parameters asset.
    $parameters += array(
      mediamosa_rest_call_job_create_still_for_mediafile::USER_ID => self::SIMPLETEST_USER_ID,
    );

    // Set the URI.
    $uri = strtr('mediafile/@mediafile_id/still/create', array('@mediafile_id' => $mediafile_id));

    // Do Post call.
    $response = $this->restCallPost($uri, $parameters, array(), $expected_result_ids, $do_internal);

    // Check response.
    $this->assertTrue(
      in_array((string) $response['xml']->header->request_result_id, $expected_result_ids),
      strtr(
        "Started still job for mediafile ID: @mediafile_id, got result @result (@result_description)",
        array(
          '@mediafile_id' => $mediafile_id,
          '@result' => (string) $response['xml']->header->request_result_id,
          '@result_description' => (string) $response['xml']->header->request_result_description,
        )
      )
    );

    if ((string) $response['xml']->header->request_result_id != mediamosa_error::ERRORCODE_OKAY) {
      return FALSE;
    }

    // Get the job_id.
    $job_id = $response['xml']->items->item[0]->job_id;

    // Must have job id.
    $this->assertTrue($job_id, strtr('Got valid job ID @job_id.', array('@job_id' => $job_id)));

    // Return the job_id.
    return array('job_id' => $job_id);
  }

  /**
   * Return the most default transcode profile.
   */
  protected function getDefaultTranscodeProfile() {

    $profile = mediamosa_transcode_profile::get_default(0);
    if (isset($profile['transcode_profile_id'])) {
      return $profile['transcode_profile_id'];
    }
    return FALSE;
  }

  /**
   * Create transcode.
   *
   * Based on REST call /mediafile/$mediafile_id/transcode (POST)
   */
  protected function createMediafileAnalyse($mediafile_id, array $parameters = array(), array $expected_result_ids = array(mediamosa_error::ERRORCODE_OKAY), $do_internal = FALSE) {
    // Parameters asset.
    $parameters += array(
      mediamosa_rest_call_job_create_still_for_mediafile::USER_ID => self::SIMPLETEST_USER_ID,
    );

    // Set the URI.
    $uri = strtr('mediafile/@mediafile_id/analyse', array('@mediafile_id' => $mediafile_id));

    // Do Post call.
    $response = $this->restCallPost($uri, $parameters, array(), $expected_result_ids, $do_internal);

    // Check response.
    $this->assertTrue(
      in_array((string) $response['xml']->header->request_result_id, $expected_result_ids),
      strtr(
        'Started transcode job for mediafile ID: @mediafile_id, got result @result (@result_description)',
        array(
          '@mediafile_id' => $mediafile_id,
          '@result' => (string) $response['xml']->header->request_result_id,
          '@result_description' => (string) $response['xml']->header->request_result_description,
        )
      )
    );

    // Get the job_id.
    $job_id = $response['xml']->items->item[0]->job_id;

    // Must have job id.
    $this->assertTrue($job_id, strtr('Got valid job ID @job_id.', array('@job_id' => $job_id)));

    // Return the job_id.
    return array('job_id' => $job_id);
  }

  /**
   * Create transcode.
   *
   * Based on REST call /mediafile/$mediafile_id/transcode (POST)
   */
  protected function createMediafileTranscode($mediafile_id, array $parameters = array(), array $expected_result_ids = array(mediamosa_error::ERRORCODE_OKAY), $do_internal = FALSE) {
    // Parameters asset.
    $parameters += array(
      mediamosa_rest_call_job_create_still_for_mediafile::USER_ID => self::SIMPLETEST_USER_ID,
    );

    // Set the URI.
    $uri = strtr('mediafile/@mediafile_id/transcode', array('@mediafile_id' => $mediafile_id));

    // Do Post call.
    $response = $this->restCallPost($uri, $parameters, array(), $expected_result_ids, $do_internal);

    // Check response.
    $this->assertTrue(
      in_array((string) $response['xml']->header->request_result_id, $expected_result_ids),
      strtr(
        'Started transcode job for mediafile ID: @mediafile_id, got result @result (@result_description)',
        array(
          '@mediafile_id' => $mediafile_id,
          '@result' => (string) $response['xml']->header->request_result_id,
          '@result_description' => (string) $response['xml']->header->request_result_description,
        )
      )
    );

    $job_id = empty($response['xml']->items->item[0]->job_id) ? 0 : $response['xml']->items->item[0]->job_id;
    if (in_array(mediamosa_error::ERRORCODE_OKAY, $expected_result_ids)) {
      // Get the job_id.
      $job_id = $response['xml']->items->item[0]->job_id;

      // Must have job id.
      $this->assertTrue($job_id, strtr('Got valid job ID @job_id.', array('@job_id' => $job_id)));
    }

    // Return the job_id.
    return array('job_id' => $job_id);
  }

  /**
   * Create retranscode.
   *
   * Based on REST call /mediafile/$mediafile_id/retranscode (POST)
   */
  protected function createMediafileRetranscode($mediafile_id, array $parameters = array(), array $expected_result_ids = array(mediamosa_error::ERRORCODE_OKAY), $do_internal = FALSE) {
    // Parameters asset.
    $parameters += array(
      mediamosa_rest_call_job_create_still_for_mediafile::USER_ID => self::SIMPLETEST_USER_ID,
    );

    // Set the URI.
    $uri = strtr('mediafile/@mediafile_id/retranscode', array('@mediafile_id' => $mediafile_id));

    // Do Post call.
    $response = $this->restCallPost($uri, $parameters, array(), $expected_result_ids, $do_internal);

    // Check response.
    $this->assertTrue(
      in_array((string) $response['xml']->header->request_result_id, $expected_result_ids),
      strtr(
        "Started retranscode job for mediafile ID: @mediafile_id, got result @result (@result_description)",
        array(
          '@mediafile_id' => $mediafile_id,
          '@result' => (string) $response['xml']->header->request_result_id,
          '@result_description' => (string) $response['xml']->header->request_result_description,
        )
      )
    );

    // Get the job_id.
    $job_id = $response['xml']->items->item[0]->job_id;

    // Must have job id.
    $this->assertTrue($job_id, strtr('Got valid job ID @job_id.', array('@job_id' => $job_id)));

    // Return the job_id.
    return array('job_id' => $job_id);
  }

  /**
   * Move a mediafile to another location.
   *
   * Based on REST call /storage/$mediafile_id/move (POST)
   */
  protected function doMediafileMove($mediafile_id, $storage_profile_id, $path, array $parameters = array(), array $expected_result_ids = array(mediamosa_error::ERRORCODE_OKAY), $do_internal = FALSE) {
    // Parameters asset.
    $parameters += array(
      mediamosa_rest_call_storage_move::USER_ID => self::SIMPLETEST_USER_ID,
    );
    $parameters[mediamosa_rest_call_storage_move::STORAGE_PROFILE_ID] = $storage_profile_id;
    $parameters[mediamosa_rest_call_storage_move::PATH] = $path;

    // Set the URI.
    $uri = strtr('storage/@mediafile_id/move', array('@mediafile_id' => $mediafile_id));

    // Do Post call.
    $response = $this->restCallPost($uri, $parameters, array(), $expected_result_ids, $do_internal);

    // Check response.
    $this->assertTrue(
      in_array((string) $response['xml']->header->request_result_id, $expected_result_ids),
      strtr(
        'Started storage move job for mediafile ID: @mediafile_id, got result @result (@result_description)',
        array(
          '@mediafile_id' => $mediafile_id,
          '@result' => (string) $response['xml']->header->request_result_id,
          '@result_description' => (string) $response['xml']->header->request_result_description,
        )
      )
    );

    // Get the job_id.
    $job_id = $response['xml']->items->item[0]->job_id;

    // Must have job id.
    $this->assertTrue($job_id, strtr('Got valid job ID @job_id.', array('@job_id' => $job_id)));

    // Return the job_id.
    return $job_id;
  }


  /**
   * Run and execute the current jobs (one cycle).
   *
   * @param bool $trigger_scheduler
   *   Trigger the scheduler, to handout and start new jobs.
   */
  public function doQueueCycle($trigger_scheduler = true) {

    try {
      mediamosa_server::trigger();

      if ($trigger_scheduler) {
        mediamosa_job_scheduler::trigger();
      }
      else {
        // Do update the server jobs.
        mediamosa_job_scheduler::check_status_all_servers();
      }
    }
    catch (Exception $e) {
      $this->var_export_fail('Exception caught; ' . $e->getMessage() . ', trace; ' . $e->getTraceAsString(), 'Job Cycle');
      throw $e;
    }
  }

  /**
   * Run and execute the current jobs (one cycle).
   *
   * @param bool $trigger_scheduler
   *   Trigger the scheduler, to handout and start new jobs.
   */
  protected function doQueueCycleAll($trigger_scheduler = true, $do_queue_cycle = true) {

    // Keep in mind that with less slots, this needs to be 18.
    $max_runs = 20;
    $running_jobs = 1;
    while ($max_runs-- && $running_jobs) {
      // Run one job cycle.
      if ($do_queue_cycle) {
        self::doQueueCycle($trigger_scheduler);
      }

      // Set flag to know if all jobs are finished or failed.
      $running_jobs = 0;
      $waiting_jobs = 0;

      // Get all jobs.
      $jobs = mediamosa_job::get_job_list(0, '', TRUE);

      // Check if the jobs are all done.
      foreach ($jobs as $job) {
        if (!in_array($job[mediamosa_job_db::JOB_STATUS],
          array(
            mediamosa_job_db::JOB_STATUS_FINISHED,
            mediamosa_job_db::JOB_STATUS_FAILED,
            mediamosa_job_db::JOB_STATUS_CANCELLED,
          )
        )) {
          $running_jobs++;

          if ($job[mediamosa_job_db::JOB_STATUS] === mediamosa_job_db::JOB_STATUS_WAITING) {
            // Waiting jobs stay waiting when trigger scheduler is off.
            if (!$trigger_scheduler) {
              $running_jobs--;
            }
            $waiting_jobs++;
          }
        }
      }

      if ($running_jobs) {
        $this->pass(strtr('Found @running running jobs of which where @waiting waiting jobs, waiting for next cycle.', array('@running' => $running_jobs, '@waiting' => $waiting_jobs)));
        // Wait 5 seconds.
        Sleep(5);
      }
      else {
        $this->pass('No more running jobs found, exiting job cycle.');
      }
    }

    // If we are dead locked, we need to know.
    if ($max_runs < 0) {
      $this->fail(strtr('Job scheduler deadlock; Failed to complete all jobs. Jobs waiting for: @running', array('@running' => $running_jobs)));
    }
  }
}
