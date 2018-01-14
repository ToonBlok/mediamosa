<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

/**
 * Class jobcore
 *
 * REMINDERS
 * -Dont forget to add teardown() later
 *
 */

class jobcore extends mediamosa_rest_call {

  const QUEUE = 'queue';
  const QUEUE_KEEPALIVE_SERVER = 'queue_keepalive_server';
  const QUEUE_KEEPALIVE_SERVER_GUARDIAN = 'queue_keepalive_server_guardian';
  const CORE_TYPE = 'core_type';

  const EXCHANGE_JOBS = 'jobs';
  const QUEUE_KEEPALIVE = 'jobcore_keepalive';
  const QUEUE_TESTS = 'TESTS';

  private $queue_keepalive_server;
  private $queue_keepalive_server_guardian;
  private $core_type;
  // ------------------------------------------------------------------ Methods.
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
        ),
      )
    );

    // Enrich with required REST vars.
    return self::get_var_setup_default($var_setup, FALSE);
  }

  /**
   * URI: /jobcore/start
   * Method: GET
   *
   * Start a jobcore
   */
  /**
   * Implements do_call().
   */
  public function do_call() {
    $mediamosa = mediamosa::get();

    $queue = $this->get_param_value(self::QUEUE);
    $this->core_type = $this->get_param_value(self::CORE_TYPE);
    $this->queue_keepalive_server = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER);
    $this->queue_keepalive_server_guardian = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER_GUARDIAN); // Queue that scheduler needs to send messages BACK to guardian.

    $this->analyze = new analyze();
    $this->still = new still();
    $this->transcode = new transcode();


    mediamosa_debug::log('Jobcore with core type ' . $this->core_type . ' was created', array(), 'T - ' . $this->core_type);
    $this->listen($queue);

    // Geef array terug van response
    // Geef steeds een regel terug die je in xml output terug krijgt
    $mediamosa->set_result(mediamosa_response::ERROR, mediamosa_error::ERRORCODE_UNEXPECTED_ERROR, $this->core_type . ' listen failed.');

    $mediamosa->add_item(
      array(
        'server_type' => $this->core_type,
      )
    );
  }

//  function __construct($core_type) {
//    mediamosa_debug::log('Construct class ' . __CLASS__, array(), 'Toon');
//  }

  // Jobcore <- Scheduler, guardian
  function listen($queue) {
    $connection_success = $this->open_connection();
    $this->channel->exchange_declare(self::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($queue, false, true, false, false);
    $this->channel->queue_declare($this->queue_keepalive_server, false, true, false, false);
    $this->channel->queue_bind($queue, self::EXCHANGE_JOBS, $queue);
    $this->channel->queue_bind($this->queue_keepalive_server, self::EXCHANGE_JOBS, $this->queue_keepalive_server);

    echo ' [*] Waiting for ' . $queue. ' messages. To exit press CTRL+C', "\n";
    mediamosa_debug::log('Ready and waiting for messages.', array(), 'T - ' . $queue);

    /**
     * Callback used for the communication between this jobcore and the scheduler. Starts jobs specified in the messages it receives.
     *
     * @param $rmq_msg
     *   RabbitMQ message received from the server.
     *
     */
    $callback = function($msg){
      $job = unserialize($msg->body);

      // Kill
      if ((isset($job['msg_type']) and $job['msg_type'] == 'KILL')) {
        $this->channel->basic_cancel($msg->delivery_info['consumer_tag']);
      } else {
//      echo ' [!] Message received: job_id: ' . $job['job_id'] . ', job_type: ' . $job['job_type'] . ' asset_id: ' . $job['asset_id'] . ' mediafile_id: ' . $job['mediafile_id'] . "\n";
        mediamosa_debug::log(' [!] Message received: job_id: ' . $job['job_id'] . ', job_type: ' . $job['job_type'] . ' asset_id: ' . $job['asset_id'] . ' mediafile_id: ' . $job['mediafile_id'], array(), 'T - ' . $this->core_type);

        $checkvar = $GLOBALS['drupal_test_info']['test_run_id'];
        mediamosa_debug::log('[STEP4] Received in jobcore with: ' . $checkvar . ' and int the message it is: ' . $job['test_run_id'], array(), 'T - MISC2');


        if ($job[mediamosa_job_db::JOB_STATUS] == mediamosa_job_db::JOB_STATUS_INPROGRESS) {
          mediamosa_debug::log('[STEP5] Goes into if it shouldnt', array(), 'T - MISC2');
          // Resume job
          mediamosa_debug::log('[' . $job[mediamosa_job_db::ID] . '] resuming.', array(), 'T - SCHEDULER');
          $this->loop($job);
        } else {
//        if (isset($job['test_run_id'])) {
//          // forgot to switch back too after this
////          try {
////            $this->switch_db($job['test_run_id']);
////          } catch (Exception $e) {
////            mediamosa_debug::log('.', array(), 'T - ERROR');
////          }
////          unset($job['test_run_id']);
//        }
          mediamosa_debug::log('[STEP5] Goes into the right else', array(), 'T - MISC2');
          $this->start_job($job);
        }

      }


      echo ' [x] Done', "\n";
      $msg->delivery_info['channel']->basic_ack($msg->delivery_info['delivery_tag']);

    };

    /**
     * Callback used for the communication between the guardian class and this jobcore to keep this jobcore alive.
     *
     * @param $rmq_msg
     *   RabbitMQ message received from the server.
     *
     */
    $keepalive_callback = function($rmq_msg) {
      $rmq_msg->delivery_info['channel']->basic_ack($rmq_msg->delivery_info['delivery_tag']);
      $msg = unserialize($rmq_msg->body);

      mediamosa_debug::log('[' . $msg['MSG_ID'] . '] ' . $this->core_type . ' jobcore received a msg with content: ' . serialize($msg) . ' at ' . microtime(TRUE), array(), 'T - RMQ');

      if ((isset($msg['sender'])) and ($msg['sender'] == 'unit_tests')) {
        //mediamosa_debug::log('Alternative send in $keepalive triggered', array(), 'T - MISC');
        $this->send([], self::QUEUE_TESTS);
      } else {

        $msg["STATUS_REQUEST"] = "received";
        $msg["STATUS"] = "online";
        $msg['timestamp'] = microtime(true);
        $msg['job_type'] = $this->queue_keepalive_server_guardian;

        mediamosa_debug::log('Received message on ' . $this->queue_keepalive_server, array(), 'T - GUARDIAN');
//        mediamosa_debug::log('Received message and am now sending back ' . $update_message['timestamp'] . '. This is the message and secondly the job: ', array(), 'T - ');
//        mediamosa_debug::log_export($msg, WATCHDOG_NOTICE, 'T - ');
//        mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - ');

        $this->send($msg, $this->queue_keepalive_server_guardian);
      }

//      mediamosa_debug::log('Sent 1 message at ' . microtime(true), array(), 'T - GUARDIAN');
    };

    // basic_qos: Tells RabbitMQ not to give more than one message to a worker at a time.
    $this->channel->basic_qos(null, 1, null);
    $this->channel->basic_consume($queue, '', false, false, false, false, $callback);
    $this->channel->basic_consume($this->queue_keepalive_server, '', false, false, false, false, $keepalive_callback);

    while(count($this->channel->callbacks)) {
      $this->channel->wait();
    }

    $this->close_connection();
    return $connection_success;
  }

  function switch_db($test_run_id) {
    mediamosa_debug::log('1 DB check!', array(), 'T - JAN10');

    $databases_old = &drupal_static('databases_old');
    $databases_old = $GLOBALS['databases'];


//    mediamosa_debug::log('Entered switch_db', array(), 'T - UT');

//    $test_info = &$GLOBALS['drupal_test_info'];
    $GLOBALS['drupal_test_info']['test_run_id'] = $test_run_id;
    $GLOBALS['drupal_test_info']['in_child_site'] = TRUE;

//    $test_info['test_run_id'] = $test_run_id;
//    $test_info['in_child_site'] = TRUE;

    foreach ($GLOBALS['databases']['default'] as &$value) {

      // Extract the current default database prefix.
      if (!isset($value['prefix'])) {
        $current_prefix = '';
      }
      elseif (is_array($value['prefix'])) {
        $current_prefix = $value['prefix']['default'];
      }
      else {
        $current_prefix = $value['prefix'];
      }

      mediamosa_debug::log('Value changed to: ' . $current_prefix . $test_run_id, array(), 'T - UT');
      // Remove the current database prefix and replace it by our own.
      $value['prefix'] = array(
        'default' => $current_prefix . $test_run_id,
      );
    }

    global $conf;
    $conf_backup = &drupal_static('conf_backup');
    $conf_backup = $conf;

    mediamosa_debug::log('.', array(), 'T - JAN10');

    $conf = variable_initialize();

    Database::parseConnectionInfo();

    mediamosa_debug::log('2 DB check!', array(), 'T - JAN10');

  }

//  function unswitch_db() {
//    // Switch database back
//    $GLOBALS['databases'] = &drupal_static('databases_old');
//
//    // Switch variables back
//    global $conf;
//    $conf = &drupal_static('conf_backup');
//    $conf = variable_initialize();
//
//    Database::parseConnectionInfo();
//  }

  function start_job($job){
    switch ($this->core_type) {
      case mediamosa_job_db::JOB_TYPE_ANALYSE:
        mediamosa_debug::log('ANALYSE started.', array(), 'T - ANALYZE');
        $this->analyze->start($job);
        break;
      case mediamosa_job_db::JOB_TYPE_STILL:
        mediamosa_debug::log('STILL started.', array(), 'T - STILL');
        $this->still->start($job);
        $this->loop($job);
        break;
      case mediamosa_job_db::JOB_TYPE_TRANSCODE:
        mediamosa_debug::log('TRANSCODE started.', array(), 'T - TRANSCODE');
        $this->transcode->start($job);
        $this->loop($job);
        break;
    }
  }

  function resume_job($job){

    switch ($job['job_type']) {
      case mediamosa_job_db::JOB_TYPE_ANALYSE:
        mediamosa_debug::log('ANALYZE started.', array(), 'T - ANALYZE');
        $this->analyze->start($job);
//        $this->loop($job);
        break;
      case mediamosa_job_db::JOB_TYPE_STILL:
        mediamosa_debug::log('STILL started.', array(), 'T - STILL');
        $this->loop($job);
        break;
      case mediamosa_job_db::JOB_TYPE_TRANSCODE:
        mediamosa_debug::log('TRANSCODE started.', array(), 'T - STILL');
        $this->loop($job);
        break;
    }

  }

  function get_scheduler_queue(){
    $scheduler_queue_name = mediamosa_scheduler::QUEUE_NAME;

    if (mediamosa::in_simpletest_sandbox()) {
      $unit_test_prefix = $GLOBALS['drupal_test_info']['test_run_id'];
      $scheduler_queue_name = $unit_test_prefix . '_' . $scheduler_queue_name;
    }

    return $scheduler_queue_name;
  }

  function loop($job){
    // Toon: 6x per minuut, elke 10sec proberen parse_queue te doen.

    $time_loop_started = microtime(TRUE);
    $time_last_peeked = microtime(TRUE);

    while(TRUE) {
      mediamosa_debug::log('get status content with ' . $job['job_id'], array(), 'T - JAN10');
      $job['new_job_status'] = mediamosa_job_server::get_status_contents($job['job_id']);
      mediamosa_debug::log('new_job_status', array(), 'T - JAN10');
      mediamosa_debug::log_export($job['new_job_status'], WATCHDOG_NOTICE, 'T - JAN10');

      mediamosa_debug::log_export('[' . $job[mediamosa_job_db::ID] . '] '. $job['new_job_status']['Progress'] . ' at ' . microtime(TRUE), WATCHDOG_NOTICE, 'T - LOOP');

      $this->send($job, $this->get_scheduler_queue());

      $time_now = microtime(TRUE);

      if (($time_loop_started + 172800) < ($time_now)) { // Set job on failed every two days.
        mediamosa_debug::log_export('Two day timeout triggered', WATCHDOG_NOTICE, 'T - JAN10');
        $job['new_job_status'] = mediamosa_job_server::get_status_contents($job['job_id']);
        $job['new_job_status']['Status'] = 'error';
        $this->send($job, mediamosa_scheduler::QUEUE_NAME); /// WRONG QUEUE
//        $this->unswitch_db();
        break;
      } else if (($time_last_peeked + 10) < ($time_now)) { // Peek every 10 seconds.
        $this->peek();
        mediamosa_debug::log('10 seconds went by. Peek() now.', array(), 'T - LOOP');
        // reset
        $time_last_peeked = $time_now;
      }
      if ($job['new_job_status']['Status'] == 'done') { // If job is done and we have sent the final progression message, break out of the while loop.

        mediamosa_debug::log_export('[' . $job[mediamosa_job_db::ID] . '] done at ' . microtime(TRUE), WATCHDOG_NOTICE, 'T - LOOP');
        mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - LOOP');
        break;
      }

    }

  }

  /*
   * Peek in it
   */
  function peek() {
    $this->channel->exchange_declare(jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($this->queue_keepalive_server, false, true, false, false);
    $this->channel->queue_bind($this->queue_keepalive_server, jobcore::EXCHANGE_JOBS, $this->queue_keepalive_server);

    $rmq_msg = $this->channel->basic_get($this->queue_keepalive_server);

    // If there is a keepalive message it should have a value.
    while (!is_null($rmq_msg)) {
      $this->channel->basic_ack($rmq_msg->delivery_info['delivery_tag']);

      $msg = unserialize($rmq_msg->body);
      mediamosa_debug::log('[' . $msg['MSG_ID'] . '] ' . $this->core_type . ' jobcore received a msg with content: ' . serialize($msg) . ' at ' . microtime(TRUE), array(), 'T - RMQ');

      if ((isset($msg['sender'])) and ($msg['sender'] == 'unit_tests')) {
        $this->send([], self::QUEUE_TESTS);
      } else {

        $msg["STATUS_REQUEST"] = "received";
        $msg["STATUS"] = "online";
        $msg['timestamp'] = microtime(true);
        $msg['job_type'] = $this->queue_keepalive_server_guardian;

        mediamosa_debug::log('Received message on ' . $this->queue_keepalive_server, array(), 'T - GUARDIAN');

        $this->send($msg, $this->queue_keepalive_server_guardian);
      }

      // Get keepalive message, and if there is one present the while loop will restart.
      $rmq_msg = $this->channel->basic_get($this->queue_keepalive_server);

    }

  }

  // *************** NEW LOOP
//  function loop($job){
//    // Toon: 6x per minuut, elke 10sec proberen parse_queue te doen.
//    $mediamosa_jobcore_timeout = $this->variable_get('mediamosa_jobcore_timeout', 300);
//
//    // Get the time since this script has started
//    if (isset($_SERVER['REQUEST_TIME'])) {
//      $time_started = $_SERVER['REQUEST_TIME'];
//    }
//
//    while(TRUE) {
//
//      $job['new_job_status'] = mediamosa_job_server::get_status_contents($job['job_id']);
//      $this->send($job, 'PROGRESSION');
//
//
//      $time_now = microtime(TRUE);
//
//      if (($time_started + 172800) < ($time_now)) {
//        // 2 days have passed
//        break;
//      }
//
//    }
//
//    for ($x = 0; $x < $mediamosa_jobcore_timeout; $x++) {
//      $start_at = microtime(TRUE);
//      // Now take the time it took, and subtrac that from 10.
//
//
//      // Run for 60 seconds but sleep every 10 seconds. Sleep 6 times.
//      // Sleep every 10 seconds unless time
//      $time_to_sleep = 10 - round(microtime(TRUE) - $start_at);
//      if ($time_to_sleep > 0) {
//        sleep($time_to_sleep);
//      }
//
//      // And repeat for 6 times = 1 minute = 1 cron run.
//    }
//
//  }

  function variable_get($name, $default = NULL) {
    return variable_get($name, $default);
  }

  function variable_set($name, $value) {
    variable_set($name, $value);
  }

  function open_connection() {
    $host = variable_get('mediamosa_scheduler_host', 'localhost');
    $port = variable_get('mediamosa_scheduler_port', 5672);
    $username = variable_get('mediamosa_scheduler_username', 'guest');
    $password = variable_get('mediamosa_scheduler_password', 'guest');

    mediamosa_debug::log('Tried to open connection to ' . variable_get('mediamosa_scheduler_host', 'localhost') . ' with credentials: ' . variable_get('mediamosa_scheduler_username', 'guest') . ', ' . variable_get('mediamosa_scheduler_password', 'guest')  , array(), 'T - MISC');

    $this->connection = new AMQPStreamConnection($host, $port, $username, $password);
    $this->channel = $this->connection->channel();
//    $var = $this->channel->open();
//    mediamosa_debug::log('$var: ' . $var, array(), 'T - MISC11');
  }

  function close_connection() {
    $this->channel->close();
    $this->connection->close();
  }

  // Jobcore --> scheduler, guardian
  function send($job, $job_type = '') {
    //$this->open_connection();

    $this->channel->exchange_declare(self::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($job_type, false, true, false, false);
    $this->channel->queue_bind($job_type, jobcore::EXCHANGE_JOBS, $job_type);

    $msg = new AMQPMessage(
      serialize($job),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );


    mediamosa_debug::log('Jobcore sent to queue: ' . $job_type . ' this job: ', array(), 'T - SCHEDULER');
    mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - SCHEDULER');
    $this->channel->basic_publish($msg, self::EXCHANGE_JOBS, $job_type);

    mediamosa_debug::log( '[' . $job['MSG_ID'] . '] ' . $this->core_type . ' jobcore sent a msg to ' . $job_type . ' with content: ' . serialize($job) . ' at ' . microtime(TRUE), array(), 'T - RMQ');
    //$this->close_connection();
  }

}