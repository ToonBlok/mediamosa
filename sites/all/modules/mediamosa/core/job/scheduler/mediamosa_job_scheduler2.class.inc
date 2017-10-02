<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

//# If called trough terminal
//$type = htmlspecialchars($_GET["type"]);
//
//if($type == 'PROGRESSION') {
//    $jobscheduler= new mediamosa_job_scheduler2();
//    $jobscheduler->listen();
//}

class mediamosa_job_scheduler2 {

  function __construct() {
    //mediamosa_debug::log('CLASS: ' . __CLASS__ . ', METHOD: ' . __METHOD__, array(), 'Toon');
    //$this->log_debug('!!!!!!!!!!!!!!!!!!!!!' . __CLASS__ . ' and ' . __METHOD__ . " started");
    $this->server_type = 'PROGRESSION';
  }

  function log_debug($message) {
    mediamosa_debug::log($message);
  }

  function round_up_jobs(){
    //mediamosa_debug::log('CLASS: ' . __CLASS__ . ', METHOD: ' . __METHOD__, array(), 'Toon');

    $server_jobs = $this->fetch_single_available_job(1);

    // Toon: Take a look at each server.
    //foreach ($server_jobs as $server_job) {
    //  // Try to start as many jobs as there are free slots.
    //  for ($i = $server_job['jobcount']; $i < $server_job['slots']; $i++) {
    //    $this->call($server_job);

    //  }
    //}
  }
    //$this->log_debug('!!!!!!!!!!!!!!!!!!!!!' . __CLASS__ . ' and ' . __METHOD__ . " started");

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
//
//  function listen() {
//    $this->openConnection();
//
//    $this->channel->exchange_declare('jobs', 'direct', false, false, false);
//    $this->channel->queue_declare($this->server_type, false, true, false, false);
//    $this->channel->queue_bind($this->server_type, 'jobs', $this->server_type);
//
//    echo ' [*] Waiting for messages. To exit press CTRL+C', "\n";
//
//    $callback = function($msg){
//      $msgContent = unserialize($msg->body);
//      echo " [!] Received: ", 'job_id: ' . $msgContent['job_id'] . ', duration: ' . substr_count($msgContent['duration'], '.') . ', Progression: ' . $msgContent['progression'] . "\n";
//
//      echo " [x] Done", "\n";
//      $msg->delivery_info['channel']->basic_ack($msg->delivery_info['delivery_tag']);
//      if ($msgContent['finished'] == true){
//        echo "Finished!";
//      }
//    };
//
//    // basic_qos: Tells RabbitMQ not to give more than one message to a worker at a time.
//    $this->channel->basic_qos(null, 1, null);
//    $this->channel->basic_consume($this->server_type, '', false, false, false, false, $callback);
//
//    while(count($this->channel->callbacks)) {
//      $this->channel->wait();
//    }
//
//    $this->closeConnection();
//
//  }
//
//  function openConnection() {
//    $this->connection = new AMQPStreamConnection('localhost', 5672, 'guest', 'guest');
//    $this->channel = $this->connection->channel();
//  }
//
//  function closeConnection() {
//    $this->channel->close();
//    $this->connection->close();
//  }

  /**
   * Fetch an single available job.
   *
   * Choose a job that applies to the following rules;
   *  [?] - Job status is not WAITING,
   *  [x] - There is no upload job for the same asset,
   *  [no] - Job must not already have started on another server,
   *  [no] - Tool servers have slot left,
   *  [?] - Priority goes first.
   *
   * @param string $server_id
   *   The server ID.
   *
   * @return array
   *   Array containing job that matched the server.
   *   - 'job_id': The job ID.
   *   - 'job_type': Type of job, @see mediamosa_job_db::JOB_TYPE_*.
   *   - 'asset_id': Asset ID.
   *   - 'mediamosa_id': MediaMosa ID.
   */
  function fetch_single_available_job($server_id) {

    // Search a job from the mediamosa_job table with status 'WAITING'o
    // { } is prefixed door drupal.
    $sql = 'SELECT job_id, job_type, asset_id, mediafile_id FROM {#mediamosa_job} AS mj
       WHERE mj.#job_status = :status AND ';

    $sql_ands = array();

    // Don't start new jobs from the same asset if the asset has a cancelling
    // job in progress.
    $sql_ands[] = 'mj.asset_id NOT IN
      (SELECT asset_id FROM {#mediamosa_job} AS mj1
       JOIN {#mediamosa_server_job} AS msj USING(job_id)
       WHERE mj1.#job_status = :JOBSTATUS_CANCELLING)';

    // Prevent parallel processing of the same asset when its a MEDIA job, these
    // need to run one by one. Others may run parallel, but only if its the same
    // job type that is already running.
    $sql_ands[] = '
      (
        (
          mj.job_type NOT IN (:JOBTYPE_MEDIA_DOWNLOAD, :JOBTYPE_MEDIA_UPLOAD, :JOBTYPE_MEDIA_MOVE)
        AND
          (
            mj.asset_id NOT IN
              (SELECT asset_id FROM {#mediamosa_job} AS mj1
                JOIN {#mediamosa_server_job} AS msj USING(job_id)
          )
          OR
            mj.job_type IN (
              SELECT mj2.job_type FROM {#mediamosa_job} AS mj2
              JOIN {#mediamosa_server_job} AS msj USING(job_id)
            )
          )
        )
      OR
        mj.asset_id NOT IN (
          SELECT asset_id FROM {#mediamosa_job} AS mj2
          JOIN {#mediamosa_server_job} AS msj USING(job_id)
        )
      )
      ';
    // dit wordt door messageQ ^

    // Don't start jobs if an upload in the same asset is in progress.
    $sql_ands[] = 'mj.asset_id NOT IN
                (SELECT asset_id FROM {#mediamosa_job} AS mj3
                 WHERE mj3.job_type = :JOBTYPE_UPLOAD AND mj3.#job_status IN (:JOBSTATUS_INPROGRESS, :JOBSTATUS_CANCELLING))';

    // The job may not yet be the server_job table.
    $sql_ands[] = 'mj.job_id NOT IN (SELECT job_id FROM {#mediamosa_server_job})';

    // Don't do upload jobs.
    $sql_ands[] = 'mj.job_type != :JOBTYPE_UPLOAD';

    // Toon: ?????????????????????????
    // Here are jobservers matched with job_types.
    $sql_ands[] = '
      (
        (
          mj.job_type = :JOBTYPE_TRANSCODE
          AND
          (
            SELECT COUNT(*) FROM {#mediamosa_job_transcode} AS mjt
              JOIN {#mediamosa_server_tool} USING(tool)
              WHERE nid = :server_id AND mjt.job_id = mj.job_id LIMIT 1
          ) = 1
        )
        OR
          (
            mj.job_type = :JOBTYPE_ANALYSE AND
            (
              SELECT COUNT(*) FROM {#mediamosa_server_tool} AS mstt1
              WHERE mstt1.tool = :TOOL_ANALYSE AND mstt1.nid = :server_id LIMIT 1
            ) = 1
          )
        OR
          (
            mj.job_type = :JOBTYPE_STILL AND
            (
              SELECT COUNT(*) FROM {#mediamosa_server_tool} AS mstt2
              WHERE mstt2.tool = :TOOL_STILL AND mstt2.nid = :server_id LIMIT 1
            ) = 1
          )
        OR
          (
            mj.job_type IN (:JOBTYPE_MEDIA_DOWNLOAD, :JOBTYPE_MEDIA_UPLOAD, :JOBTYPE_MEDIA_MOVE) AND
            (
              SELECT COUNT(*) FROM {#mediamosa_server_tool} AS mstt3
              WHERE mstt3.tool = :TOOL_TRANSFER AND mstt3.nid = :server_id LIMIT 1
            ) = 1
          )
        OR
          (
            mj.job_type IN (:JOBTYPE_DELETE_MEDIAFILE)
          )
      )';

    $sql .= implode($sql_ands, "\n AND ") . "\n" . ' ORDER BY priority ASC, job_id ASC LIMIT 0,1';

    // First strtr tablenames as these are not allowed as named parameters.
    $sql = strtr($sql, array(
      '#job_status' => mediamosa_job_db::JOB_STATUS,
      '#mediamosa_job' => mediamosa_job_db::TABLE_NAME,
      '#mediamosa_job_transcode' => mediamosa_job_transcode_db::TABLE_NAME,
      '#mediamosa_server_job' => mediamosa_server_job_db::TABLE_NAME,
      '#mediamosa_server_tool' => mediamosa_server_tool_db::TABLE_NAME,
    ));

    // SQL query meegeven met parameters
    // : escaped de variabele
    // # vervang

    return mediamosa_db::db_query($sql,
      array(
        ':JOBSTATUS_CANCELLING' => mediamosa_job_db::JOB_STATUS_CANCELLING,
        ':JOBSTATUS_INPROGRESS' => mediamosa_job_db::JOB_STATUS_INPROGRESS,
        ':JOBTYPE_MEDIA_DOWNLOAD' => mediamosa_job_db::JOB_TYPE_TRANSFER_MEDIA_DOWNLOAD,
        ':JOBTYPE_MEDIA_MOVE' => mediamosa_job_db::JOB_TYPE_TRANSFER_MEDIA_MOVE,
        ':JOBTYPE_MEDIA_UPLOAD' => mediamosa_job_db::JOB_TYPE_TRANSFER_MEDIA_UPLOAD,
        ':JOBTYPE_UPLOAD' => mediamosa_job_db::JOB_TYPE_UPLOAD,
        ':JOBTYPE_DELETE_MEDIAFILE' => mediamosa_job_db::JOB_TYPE_DELETE_MEDIAFILE,
        ':TOOL_ANALYSE' => mediamosa_job_server::MEDIAMOSA_JOB_SERVER_TOOL_ANALYSE,
        ':TOOL_STILL' => mediamosa_job_server::MEDIAMOSA_JOB_SERVER_TOOL_STILL,
        ':TOOL_TRANSFER' => mediamosa_job_server::MEDIAMOSA_JOB_SERVER_TOOL_TRANSFER,
        ':server_id' => $server_id,
        ':JOBTYPE_ANALYSE' => mediamosa_job_db::JOB_TYPE_ANALYSE,
        ':JOBTYPE_STILL' => mediamosa_job_db::JOB_TYPE_STILL,
        ':JOBTYPE_TRANSCODE' => mediamosa_job_db::JOB_TYPE_TRANSCODE,
        ':status' => mediamosa_job_db::JOB_STATUS_WAITING,
      )
    )->fetchAssoc();
  }
}