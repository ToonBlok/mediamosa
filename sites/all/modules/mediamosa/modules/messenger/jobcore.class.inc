<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class jobcore extends mediamosa_rest_call {

  const CORE_TYPE = 'core_type';
  const EXCHANGE_JOBS = 'jobs';
  const QUEUE_KEEPALIVE = 'jobcore_keepalive';
  const QUEUE_KEEPALIVE_SERVER_GUARDIAN = 'queue_keepalive_server_guardian';
  const QUEUE_KEEPALIVE_SERVER = 'queue_keepalive_server';
  // ------------------------------------------------------------------ Methods.
  /**
   * Implements get_var_setup().
   */
  public function get_var_setup() {
    $var_setup = array(
      self::VARS => array(
        self::CORE_TYPE => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => "",
        ),
        self::QUEUE_KEEPALIVE_SERVER_GUARDIAN => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => "",
        ),
        self::QUEUE_KEEPALIVE_SERVER => array(
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

    $core_type = $this->get_param_value(self::CORE_TYPE);
    $queue_keepalive_server = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER);
    $this->queue_keepalive_server_guardian = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER_GUARDIAN); // Queue that scheduler needs to send messages BACK to guardian.

    $this->analyze = new analyze();
    $this->still = new still();
    $this->transcode = new transcode();
    $this->core_type = $core_type;
    mediamosa_debug::log('Jobcore with core type ' . $core_type . ' was created', array(), 'T - ' . $core_type);
    $this->listen($core_type, $queue_keepalive_server);

    // Geef array terug van response
    // Geef steeds een regel terug die je in xml output terug krijgt
    $mediamosa->add_item(
      array(
        'version' => mediamosa_version::get_current_version_str(FALSE),
        'server_type' => $core_type,
      )
    );
  }

//  function __construct($core_type) {
//    mediamosa_debug::log('Construct class ' . __CLASS__, array(), 'Toon');
//  }

  // Jobcore <- Scheduler, guardian
  function listen($core_type, $keep_alive_queue) {
    $this->open_connection();
    $this->channel->exchange_declare(self::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($core_type, false, true, false, false);
    $this->channel->queue_declare($keep_alive_queue, false, true, false, false);
    $this->channel->queue_bind($core_type, self::EXCHANGE_JOBS, $core_type);
    $this->channel->queue_bind($keep_alive_queue, self::EXCHANGE_JOBS, $keep_alive_queue);

    if (mediamosa::in_simpletest_sandbox()) {
      mediamosa_debug::log('In sandbox...', array(), 'T - UNIT TESTS');
      return;
    }

    echo ' [*] Waiting for ' . $core_type . ' messages. To exit press CTRL+C', "\n";
    mediamosa_debug::log('Ready and waiting for messages.', array(), 'T - ' . $core_type);

    $callback = function($msg){
      $job = unserialize($msg->body);
//      echo ' [!] Message received: job_id: ' . $job['job_id'] . ', job_type: ' . $job['job_type'] . ' asset_id: ' . $job['asset_id'] . ' mediafile_id: ' . $job['mediafile_id'] . "\n";
//      mediamosa_debug::log(' [!] Message received: job_id: ' . $job['job_id'] . ', job_type: ' . $job['job_type'] . ' asset_id: ' . $job['asset_id'] . ' mediafile_id: ' . $job['mediafile_id'], array(), 'T - ' . $this->core_type);

      $this->start_job($job);

      echo ' [x] Done', "\n";
      $msg->delivery_info['channel']->basic_ack($msg->delivery_info['delivery_tag']);
    };

    $keepalive_callback = function($msg) {

      $msg->delivery_info['channel']->basic_ack($msg->delivery_info['delivery_tag']);

      $update_message['timestamp'] = microtime(true);
      $update_message['job_type'] = $this->queue_keepalive_server_guardian;

      $this->send($update_message, $this->queue_keepalive_server_guardian);

//      mediamosa_debug::log('Sent 1 message at ' . microtime(true), array(), 'T - GUARDIAN');
    };

    // basic_qos: Tells RabbitMQ not to give more than one message to a worker at a time.
    $this->channel->basic_qos(null, 1, null);
    $this->channel->basic_consume($core_type, '', false, false, false, false, $callback);
    $this->channel->basic_consume($keep_alive_queue, '', false, false, false, false, $keepalive_callback);

    while(count($this->channel->callbacks)) {
      $this->channel->wait();
    }

    $this->close_connection();
  }

  function start_job($job){

    switch ($job['job_type']) {
      case mediamosa_job_db::JOB_TYPE_ANALYSE:
        mediamosa_debug::log('ANALYZE started.', array(), 'T - ANALYZE');
        $this->analyze->start($job);
//        $this->loop($job);
        break;
      case mediamosa_job_db::JOB_TYPE_STILL:
        mediamosa_debug::log('STILL started.', array(), 'T - STILL');
        $this->still->start($job);
        $this->loop($job);
//        $this->still->update($job);
        break;
      case mediamosa_job_db::JOB_TYPE_TRANSCODE:
        $this->transcode->start($job);
        $this->loop($job);
        break;
    }

  }

  function loop($job){
    // Toon: 6x per minuut, elke 10sec proberen parse_queue te doen.
    for ($x = 0; $x < 18; $x++) {
      $start_at = microtime(TRUE);
      // Now take the time it took, and subtrac that from 10.

      $job['new_job_status'] = mediamosa_job_server::get_status_contents($job['job_id']);
      $this->send($job, 'PROGRESSION');

      if ($job['new_job_status']['Status'] == 'done') {
        break;
      }

      $time_to_sleep = 10 - round(microtime(TRUE) - $start_at);
      if ($time_to_sleep > 0) {
        sleep($time_to_sleep);
      }

      // And repeat for 6 times = 1 minute = 1 cron run.
    }

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

  // Jobcore --> scheduler, guardian
  function send($job, $job_type = '') {
    //$this->open_connection();

    $this->channel->exchange_declare(self::EXCHANGE_JOBS, 'direct', false, false, false);

    $msg = new AMQPMessage(
      serialize($job),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );


    mediamosa_debug::log('Jobcore sent to queue: ' . $job_type . ' this job: ', array(), 'T - SCHEDULER');
    mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - SCHEDULER');
    $this->channel->basic_publish($msg, self::EXCHANGE_JOBS, $job_type);

    //$this->close_connection();
  }

}
