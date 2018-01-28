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

  /**
   * send a message.
   *
   * @param array $msg
   * the message content.
   * @param string $queue
   * the queue to send the message to.
   */
  protected function rmq_send($msg, $queue) {
    $this->channel->exchange_declare(mediamosa_messenger_jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($queue, false, true, false, false);
    $this->channel->queue_bind($queue, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $queue);

    $rmq_msg = new AMQPMessage(
      json_encode($msg),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    mediamosa_debug::log('Sent: ', array(), 'T - 28');
    mediamosa_debug::log_export(json_encode($msg), WATCHDOG_NOTICE, 'T - 28');

    $this->channel->basic_publish($rmq_msg, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $queue);
  }

  /**
   * Acknowledge a message. Lets the RabbitMQ server know we have received the message successfully
   *
   * @param AMQPMessage $rmq_msg
   * The original RabbitMQ message. Message contents is in its body.
   */
  protected function rmq_ack($rmq_msg) {
    try {
      $rmq_msg->delivery_info['channel']->basic_ack($rmq_msg->delivery_info['delivery_tag']);
    } catch (Exception $e) {
      // Called ack on empty message, something wrong with the messages being sent.
      mediamosa_debug::log($e, array(), 'T - SCHEDULER');
    }
  }


  /**
   * Start listening to a queue.
   *
   * @param array $queue
   * The queue we are going to listen to.
   */
  abstract protected function rmq_listen($queue);

  /**
   * Start listening to a queue.
   * Alternative to rmq_listen.
   * Different in that we only check a queues content for a few messages then move on (hence "peek")
   */
  abstract protected function rmq_peek();
}
