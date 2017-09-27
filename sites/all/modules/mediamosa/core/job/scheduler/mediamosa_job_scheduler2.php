<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

# If called trough terminal
$type = htmlspecialchars($_GET["type"]);

if($type == 'PROGRESSION') {
    $jobscheduler= new mediamosa_job_scheduler2();
    $jobscheduler->listen();
}

class mediamosa_job_scheduler2 {

    function __construct() {

        $this->server_type = 'PROGRESSION';
    }

    function log_debug($message, array $variables = array()) {
        mediamosa_debug::log($message, $variables, 'job_scheduler');
    }

    function call($job) {
        $connection = new AMQPStreamConnection('localhost', 5672, 'guest', 'guest');
        $channel = $connection->channel();

        $channel->exchange_declare('jobs', 'direct', false, false, false);

        $msg = new AMQPMessage(
            serialize($job),
            array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
        );

        # Send the message.
        //echo 'sent with routing_key: ' . $job['job_type'];
        $channel->basic_publish($msg, 'jobs', $job['job_type']);

        $channel->close();
        $connection->close();
    }

    function listen() {
        $this->openConnection();

        $this->channel->exchange_declare('jobs', 'direct', false, false, false);
        $this->channel->queue_declare($this->server_type, false, true, false, false);
        $this->channel->queue_bind($this->server_type, 'jobs', $this->server_type);

        echo ' [*] Waiting for messages. To exit press CTRL+C', "\n";

        $callback = function($msg){
            $msgContent = unserialize($msg->body);
            echo " [!] Received: ", 'job_id: ' . $msgContent['job_id'] . ', duration: ' . substr_count($msgContent['duration'], '.') . ', Progression: ' . $msgContent['progression'] . "\n";

            echo " [x] Done", "\n";
            $msg->delivery_info['channel']->basic_ack($msg->delivery_info['delivery_tag']);
            if ($msgContent['finished'] == true){
                echo "Finished!";
            }
        };

        // basic_qos: Tells RabbitMQ not to give more than one message to a worker at a time.
        $this->channel->basic_qos(null, 1, null);
        $this->channel->basic_consume($this->server_type, '', false, false, false, false, $callback);

        while(count($this->channel->callbacks)) {
            $this->channel->wait();
        }

        $this->closeConnection();

    }

    function openConnection() {
        $this->connection = new AMQPStreamConnection('localhost', 5672, 'guest', 'guest');
        $this->channel = $this->connection->channel();
    }

    function closeConnection() {
        $this->channel->close();
        $this->connection->close();
    }

}
