<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class scheduler {

  function __construct()
  {
    mediamosa_debug::log('Construct class ' . __CLASS__, array(), 'Toon');

    // Who exactly calls scheduler doesn't matter for now
    // Boot the servers here

    // if
    $this->boot_servers();
  }

  // één stap te ver
  function boot_servers(){
    // 1: Check if servers are already booted
    // 1.1: Check which servers should be online

    // ...
    // 1.2: Check if those servers are online
    // If servers online
    //   skip
    // else:
    //   start servers (set their value on ON)







    $servers = $this->get_enabled_job_processor();
  }

  function get_enabled_job_processor(array $enabled_status = array(mediamosa_server_db::SERVER_STATUS_ON, mediamosa_server_db::SERVER_STATUS_CLOSE)) {
    return $this->get_enabled(array(mediamosa_server_db::SERVER_TYPE_JOB_PROCESSOR), $enabled_status);
  }

  function get_enabled(array $server_types = array(), array $enabled_status = array(mediamosa_server_db::SERVER_STATUS_ON, mediamosa_server_db::SERVER_STATUS_CLOSE)) {
    $query = mediamosa_db::db_select(mediamosa_server_db::TABLE_NAME, 'ms')
      ->fields('ms');

    // Any specific types?
    if (!empty($server_types)) {
      $query->condition(mediamosa_server_db::SERVER_TYPE, $server_types, 'IN');
    }

    if (!empty($enabled_status)) {
      $query->condition(mediamosa_server_db::SERVER_STATUS, $enabled_status, 'IN');
    }

    return $query
      ->orderBy(mediamosa_server_db::SERVER_TYPE)
      ->execute();
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