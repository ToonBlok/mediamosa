<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class jobcore{

  function __construct($server_type) {
//    mediamosa_debug::log('Construct class ' . __CLASS__, array(), 'Toon');
    $this->analyze = new analyze();
    $this->still = new still();
    $this->transcode = new transcode();
    $this->server_type = $server_type;
    $this->listen();
  }

  function listen() {
    $this->open_connection();
    $this->channel->exchange_declare('jobs', 'direct', false, false, false);
    $this->channel->queue_declare($this->server_type, false, true, false, false);
    $this->channel->queue_bind($this->server_type, 'jobs', $this->server_type);

    echo ' [*] Waiting for ' . $this->server_type . ' messages. To exit press CTRL+C', "\n";

    $callback = function($msg){
      mediamosa_debug::log('Message received in Listen() in class: ' . __CLASS__, array(), 'Toon');
      $job = unserialize($msg->body);
      echo ' [!] Message received: job_id: ' . $job['job_id'] . ', job_type: ' . $job['job_type'] . ' asset_id: ' . $job['asset_id'] . ' mediafile_id: ' . $job['mediafile_id'] . "\n";

      $this->start_job($job);

      echo ' [x] Done', "\n";
      $msg->delivery_info['channel']->basic_ack($msg->delivery_info['delivery_tag']);
    };

    // basic_qos: Tells RabbitMQ not to give more than one message to a worker at a time.
    $this->channel->basic_qos(null, 1, null);
    $this->channel->basic_consume($this->server_type, '', false, false, false, false, $callback);

    while(count($this->channel->callbacks)) {
      $this->channel->wait();
    }

    $this->close_connection();
  }

  function start_job($job){

    switch ($job['job_type']) {
      case mediamosa_job_db::JOB_TYPE_ANALYSE:
        mediamosa_debug::log('ANALYZE started.', array(), 'Toon - ANALYZE');
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
