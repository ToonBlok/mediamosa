<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

//$type = implode(' ', array_slice($argv, 1));
//
//if(empty($type)) {
//  $type = "TRANSCODE";
//}

//$jobserver = new Jobserver($type);

class server {

  function __construct(/*$server_type*/) {
    //$this->server_type = $server_type;
    mediamosa_debug::log('Construct class ' . __CLASS__, array(), 'Toon');
    $this->server_type = 'ANALYSE';
    $this->listen();
  }

  function listen() {
    $this->open_connection();

    $this->channel->exchange_declare('jobs', 'direct', false, false, false);
    $this->channel->queue_declare($this->server_type, false, true, false, false);
    $this->channel->queue_bind($this->server_type, 'jobs', $this->server_type);

    echo ' [*] Waiting for messages. To exit press CTRL+C', "\n";

    $callback = function($msg){
      echo ' [!] Message received!', "\n";
      //$debug_message = 'job_id: ' . $msg['job_id'] . ', job_type: ' . $msg['job_type'] . ' asset_id: ' . $msg['asset_id'] . ' mediafile_id: ' . $msg['mediafile_id'];
      //$msgContent = unserialize($msg->body);
      mediamosa_debug::log('Message received in Listen() in class: ' . __CLASS__, array(), 'Toon');
      //mediamosa_debug::log($debug_message, array(), 'Toon');

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

  function open_connection() {
    $this->connection = new AMQPStreamConnection('localhost', 5672, 'guest', 'guest');
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
