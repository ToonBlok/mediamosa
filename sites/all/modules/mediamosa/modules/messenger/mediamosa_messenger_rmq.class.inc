<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

abstract class mediamosa_messenger_rmq extends mediamosa_rest_call
{
// The AMQP connection object.
  protected $connection;
// The channel on which to open a connection.
  protected $channel;

  /**
   * Open a RabbitMQ connection.
   */
  protected function rmq_open_connection() {
    $host = $this->variable_get('mediamosa_scheduler_host', 'localhost');
    $port = $this->variable_get('mediamosa_scheduler_port', 5672);
    $username = $this->variable_get('mediamosa_scheduler_username', 'guest');
    $password = $this->variable_get('mediamosa_scheduler_password', 'guest');

    $this->connection = new AMQPStreamConnection($host, $port, $username, $password);
    $this->channel = $this->connection->channel();
  }

  /**
   * Close a currently open RabbitMQ connection.
   */
  protected function rmq_close_connection() {
    $this->channel->close();
    $this->connection->close();
  }

  // Get the rest input info.
  protected function rmq_send($msg, $queue) {
    $this->channel->exchange_declare(mediamosa_messenger_jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($queue, false, true, false, false);
    $this->channel->queue_bind($queue, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $queue);

    $msg = new AMQPMessage(
      serialize($msg),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    $this->channel->basic_publish($msg, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $queue);
  }

  protected function rmq_ack($rmq_msg) {
    try {
      $rmq_msg->delivery_info['channel']->basic_ack($rmq_msg->delivery_info['delivery_tag']);
    } catch (Exception $e) {
      // Called ack on empty message, something wrong with the messages being sent.
      mediamosa_debug::log($e, array(), 'T - SCHEDULER');
    }
  }

  abstract protected function rmq_listen($queue);
  abstract protected function rmq_peek();
}
