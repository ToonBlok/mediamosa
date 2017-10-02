<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class scheduler {

  function __construct()
  {
    mediamosa_debug::log('Construct class ' . __CLASS__, array(), 'Toon');
  }

  function send($job) {
    $this->open_connection();

    $this->channel->exchange_declare('jobs', 'direct', false, false, false);
    $this->channel->queue_declare('ANALYSE', false, true, false, false);
    $this->channel->queue_bind($this->server_type, 'jobs', $this->server_type);

    $msg = new AMQPMessage(
      serialize($job),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    # Send the message.
    //echo 'sent with routing_key: ' . $job['job_type'];
    $this->channel->basic_publish($msg, 'jobs', $job['job_type']);

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

  function round_up_jobs(){
    mediamosa_debug::log('Start method ' . __METHOD__, array(), 'Toon');

    // for loop here

    $sql = 'SELECT job_id, job_type, asset_id, mediafile_id 
            FROM {#mediamosa_job} AS mj
            WHERE mj.#job_status = :status';

    $sql = strtr($sql, array(
      '#job_status' => mediamosa_job_db::JOB_STATUS,
      '#mediamosa_job' => mediamosa_job_db::TABLE_NAME
    ));

    $jobs = mediamosa_db::db_query($sql,
      array(
        ':status' => mediamosa_job_db::JOB_STATUS_WAITING
      )
    )->fetchAll();

    foreach ($jobs as $job) {
      $this->send($job);
    }
    //$this->send($jobs[0]);
  }

}