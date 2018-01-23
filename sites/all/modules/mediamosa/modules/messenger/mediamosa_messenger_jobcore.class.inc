<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

/**
 * URI: /jobcore/start
 * Method: GET
 *
 * Start a jobcore.
 */
class mediamosa_messenger_jobcore extends mediamosa_messenger_rmq
{

  const QUEUE = 'queue';
  const QUEUE_KEEPALIVE_SERVER = 'queue_keepalive_server';
  const QUEUE_KEEPALIVE_SERVER_GUARDIAN = 'queue_keepalive_server_guardian';
  const CORE_TYPE = 'core_type';

  const EXCHANGE_JOBS = 'jobs';
  const QUEUE_KEEPALIVE = 'jobcore_keepalive';
  const QUEUE_TESTS = 'TESTS';

  protected $queue_keepalive_server;
  protected $queue_keepalive_server_guardian;
  protected $core_type;
  // ------------------------------------------------------------------ Methods.
  /**
   * Implements get_var_setup().
   */
  public function get_var_setup() {
    $var_setup = array(
      self::VARS => array(
        self::QUEUE => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => 'The queue to listen to to receive jobs',
        ),
        self::QUEUE_KEEPALIVE_SERVER => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => 'The queue to receive keepalive message on',
        ),
        self::QUEUE_KEEPALIVE_SERVER_GUARDIAN => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => 'The queue to send keepalive response message to',
        ),
        self::CORE_TYPE => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => 'This servers core types',
        ),
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
    $core_type = unserialize($this->get_param_value(self::CORE_TYPE)); // Purely used for debug statements
    $this->core_type = '';
    $this->queue_keepalive_server = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER);
    $this->queue_keepalive_server_guardian = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER_GUARDIAN); // Queue that scheduler needs to send messages BACK to guardian.

    // Give the core its helper classes.
    foreach ($queue as $core) {
      switch ($core) {
        case mediamosa_job_db::JOB_TYPE_ANALYSE:
          $this->analyze = new mediamosa_messenger_helper_analyze();
          break;

        case mediamosa_job_db::JOB_TYPE_STILL:
          $this->still = new mediamosa_messenger_helper_still();
          break;

        case mediamosa_job_db::JOB_TYPE_TRANSCODE:
          $this->transcode = new mediamosa_messenger_helper_transcode();
          break;
      }
    }

    // Construct a name to use in this cores logging statement. Value only used in logging statements.
    for ($i = 0; $i < count($core_type); $i++) {
      $this->core_type = $this->core_type . $core_type[$i];

      if ($i + 1 != count($core_type[$i])) {
        $this->core_type .= '_';
      }
    }

    try {
      $this->rmq_listen($queue);
    } catch (Exception $e) {
      // Listen failed
      mediamosa_debug::log($e, array(), 'T - ' . $this->core_type);
    }

    // Geef array terug van response
    // Geef steeds een regel terug die je in xml output terug krijgt
    $mediamosa->set_result(mediamosa_response::ERROR, mediamosa_error::ERRORCODE_UNEXPECTED_ERROR, $this->core_type . ' listen failed.');

    $mediamosa->add_item(
      array(
        'server_type' => $this->core_type
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
    $connection_success = $this->rmq_open_connection();
    $this->channel->exchange_declare(self::EXCHANGE_JOBS, 'direct', false, false, false);

    foreach ($queue as $core) {
      $this->channel->queue_declare($core, false, true, false, false);
      $this->channel->queue_bind($core, self::EXCHANGE_JOBS, $core);
    }

    $this->channel->queue_declare($this->queue_keepalive_server, false, true, false, false);
    $this->channel->queue_bind($this->queue_keepalive_server, self::EXCHANGE_JOBS, $this->queue_keepalive_server);

    /**
     * Callback used for the communication between this jobcore and the scheduler. Starts jobs specified in the messages it receives.
     *
     * @param $rmq_msg
     *   RabbitMQ message received from the server.
     *
     */
    $callback = function($rmq_msg){

      $this->rmq_ack($rmq_msg);

      $msg = unserialize($rmq_msg->body);

      if ($msg[mediamosa_messenger_scheduler::MSG_TYPE] == 'KILL') {
        $this->channel->basic_cancel($rmq_msg->delivery_info['consumer_tag']);
        exit(); // Don't know if belongs here
      } else {
        $job = $msg[mediamosa_messenger_scheduler::MSG_DATA];
        $this->mediamosa_messenger_start_job($job);
      }

    };

    /**
     * Callback used for the communication between the guardian class and this jobcore to keep this jobcore alive.
     *
     * @param $rmq_msg
     *   RabbitMQ message received from the server.
     */
    $keepalive_callback = function($rmq_msg) {

      try {
        $rmq_msg->delivery_info['channel']->basic_ack($rmq_msg->delivery_info['delivery_tag']);
      } catch (Exception $e) {
        // Called ack on empty message, something wrong with the messages being sent.
        mediamosa_debug::log($e, array(), 'T - ' . $this->core_type);
      }

      $msg = unserialize($rmq_msg->body);


      $msg[mediamosa_messenger_scheduler::MSG_STATUS] = mediamosa_messenger_scheduler::SERVER_STATUS_ONLINE;
      $msg[mediamosa_messenger_scheduler::MSG_TIMESTAMP] = microtime(TRUE);

      $this->rmq_send($msg, $this->queue_keepalive_server_guardian);
    };

    // basic_qos: Tells RabbitMQ not to give more than one message to a worker at a time.
    $this->channel->basic_qos(null, 1, null);
    foreach ($queue as $core) {
      $this->channel->basic_consume($core, '', false, false, false, false, $callback);
    }
    $this->channel->basic_consume($this->queue_keepalive_server, '', false, false, false, false, $keepalive_callback);

    while(count($this->channel->callbacks)) {
      $this->channel->wait();
    }

    $this->rmq_close_connection();
    return $connection_success;
  }

  /**
   * Start a job with the help of a helper class.
   *
   * @param $job
   * The job to start.
   */
  protected function mediamosa_messenger_start_job($job) {
    switch ($job[mediamosa_job_db::JOB_TYPE]) {
      case mediamosa_job_db::JOB_TYPE_ANALYSE:
        $this->analyze->start($job);
        break;

      case mediamosa_job_db::JOB_TYPE_STILL:
        $this->still->start($job);
        $this->mediamosa_messenger_loop($job);
        break;

      case mediamosa_job_db::JOB_TYPE_TRANSCODE:
        $this->transcode->start($job);
        $this->mediamosa_messenger_loop($job);
        break;
    }
  }

  /**
   * Loop indefinitely, send progression messages back to the scheduler.
   * Escape loop when done or when problems are detected.
   *
   * @param $job
   * The job to start the loop for.
   */
  protected function mediamosa_messenger_loop($job) {
    $time_loop_started = microtime(TRUE);
    $time_last_peeked = microtime(TRUE);

    $unit_test_prefix = '';
    if (!is_null($GLOBALS['drupal_test_info']['test_run_id'])) {
      $unit_test_prefix = $GLOBALS['drupal_test_info']['test_run_id'] . '_';
    }

    $msg = [
      mediamosa_messenger_scheduler::MSG_ID => mediamosa_job::mediamosa_messenger_get_unique_msg_id(),
      mediamosa_messenger_scheduler::MSG_TYPE => mediamosa_messenger_scheduler::MSG_PROGRESSION,
      mediamosa_messenger_scheduler::MSG_QUEUE => $unit_test_prefix . mediamosa_messenger_scheduler::QUEUE_NAME,
      mediamosa_messenger_scheduler::MSG_TIMESTAMP => microtime(TRUE),
      mediamosa_messenger_scheduler::MSG_UNIT_TEST_PREFIX => $unit_test_prefix,
      mediamosa_messenger_scheduler::MSG_DATA => $job
    ];

    while(TRUE) {
      $msg[mediamosa_messenger_scheduler::MSG_DATA]['new_job_status'] = mediamosa_job_server::get_status_contents($msg[mediamosa_messenger_scheduler::MSG_DATA][mediamosa_job_db::ID]);
      $this->rmq_send($msg, $msg[mediamosa_messenger_scheduler::MSG_QUEUE]);
      $time_now = microtime(TRUE);

      if (($time_loop_started + 172800) < ($time_now)) { // Set job on failed every two days.
        $msg[mediamosa_messenger_scheduler::MSG_DATA]['new_job_status'] = mediamosa_job_server::get_status_contents($job['job_id']);
        $msg[mediamosa_messenger_scheduler::MSG_DATA]['new_job_status']['Status'] = 'error';
        $this->rmq_send($msg, $msg[mediamosa_messenger_scheduler::MSG_QUEUE]);
        break;
      } else if (($time_last_peeked + 10) < ($time_now)) { // Peek every 10 seconds.
        $this->rmq_peek();
        $time_last_peeked = $time_now;
      }
      if ($msg[mediamosa_messenger_scheduler::MSG_DATA]['new_job_status']['Status'] == 'done') { // If job is done and we have sent the final progression message, break out of the while loop.
        break;
      } elseif ($msg[mediamosa_messenger_scheduler::MSG_DATA]['new_job_status']['Status'] == 'error') { // If job is done and we have sent the final progression message, break out of the while loop.
        break;
      }
    }
  }

  /*
   * If this jobcore is busy with a job it cannot respond to a keepalive message from the guardian
   * While the jobcore is busy, this method gets called every 10 second to take a quick peek into
   * this jobcores keepalive queue and to give a response.
   */
  protected function rmq_peek() {
    $this->channel->exchange_declare(mediamosa_messenger_jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($this->queue_keepalive_server, false, true, false, false);
    $this->channel->queue_bind($this->queue_keepalive_server, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $this->queue_keepalive_server);

    $rmq_msg = $this->channel->basic_get($this->queue_keepalive_server);

    // If there is a keepalive message it should have a value.
    while (!is_null($rmq_msg)) {

      try {
        $this->channel->basic_ack($rmq_msg->delivery_info['delivery_tag']);
      } catch (Exception $e) {
        // Called ack on empty message, something wrong with the messages being sent.
        mediamosa_debug::log($e, array(), 'T - ' . $this->core_type);
      }

      $msg = unserialize($rmq_msg->body);
      $msg[mediamosa_messenger_scheduler::MSG_STATUS] = mediamosa_messenger_scheduler::SERVER_STATUS_ONLINE;
      $msg[mediamosa_messenger_scheduler::MSG_TIMESTAMP] = microtime(true);

      $this->rmq_send($msg, $this->queue_keepalive_server_guardian);

      // Get keepalive message, and if there is one present the while loop will restart.
      $rmq_msg = $this->channel->basic_get($this->queue_keepalive_server);
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
