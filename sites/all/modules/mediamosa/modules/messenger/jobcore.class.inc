<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class jobcore extends mediamosa_rest_call {

  const CORE_TYPE = 'core_type';
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
    $this->analyze = new analyze();
    $this->still = new still();
    $this->transcode = new transcode();
    $this->core_type = $core_type;
    mediamosa_debug::log('Started ' . $core_type, array(), 'T - DEBUG');
    $this->listen($core_type);

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

  function listen($core_type) {
    $this->open_connection();
    $this->channel->exchange_declare('jobs', 'direct', false, false, false);
    $this->channel->queue_declare($core_type, false, true, false, false);
    $this->channel->queue_bind($core_type, 'jobs', $core_type);

    echo ' [*] Waiting for ' . $core_type . ' messages. To exit press CTRL+C', "\n";

    $callback = function($msg){
      mediamosa_debug::log('Message received in Listen() in class: ' . __CLASS__, array(), 'Toon');
      $job = unserialize($msg->body);
//      echo ' [!] Message received: job_id: ' . $job['job_id'] . ', job_type: ' . $job['job_type'] . ' asset_id: ' . $job['asset_id'] . ' mediafile_id: ' . $job['mediafile_id'] . "\n";
      mediamosa_debug::log(' [!][' . $this->core_type . '] Message received: job_id: ' . $job['job_id'] . ', job_type: ' . $job['job_type'] . ' asset_id: ' . $job['asset_id'] . ' mediafile_id: ' . $job['mediafile_id'], array(), 'T - ' . $this->core_type);

      $this->start_job($job);
      $this->loop();

      echo ' [x] Done', "\n";
      $msg->delivery_info['channel']->basic_ack($msg->delivery_info['delivery_tag']);
    };

    // basic_qos: Tells RabbitMQ not to give more than one message to a worker at a time.
    $this->channel->basic_qos(null, 1, null);
    $this->channel->basic_consume($core_type, '', false, false, false, false, $callback);

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
        break;
      case mediamosa_job_db::JOB_TYPE_STILL:
        mediamosa_debug::log('STILL started.', array(), 'T - STILL');
        $this->still->start($job);
        $this->still->update($job);
        break;
      case mediamosa_job_db::JOB_TYPE_TRANSCODE:
        $this->transcode->start($job);
        $this->transcode->update($job);
        break;
    }

  }

  function update_job($job){

  }

  function loop($job){
    // Toon: 6x per minuut, elke 10sec proberen parse_queue te doen.
    for ($x = 0; $x < 6; $x++) {
      $start_at = microtime(TRUE);
      // Now take the time it took, and subtrac that from 10.
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

}
