<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class jobcore{

  const ID = 'job_id';

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
      mediamosa_debug::log('Message received in Listen() in class: ' . __CLASS__, array(), 'Toon');

      $msg_body = unserialize($msg->body);

      $debug_message = 'job_id: ' . $msg_body[$this->ID] . ', job_type: ' . $msg_body['job_type'] . ' asset_id: ' . $msg_body['asset_id'] . ' mediafile_id: ' . $msg_body['mediafile_id'];
      echo ' [!] Message received: ' . $debug_message . "\n";

      if ($msg_body['job_type'] == 'ANALYSE') {
        $this->analyze($msg_body);
      }

      //const MEDIAFILE_ID_SRC = 'mediafile_src';

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

  // Toon: Confirmed all functions are working
  // modelled after mediamosa_job_server.class.inc line 440
  function analyze($job){
    mediamosa_debug::log('Entered method: ' . __METHOD__, array(), 'Toon');

    $analyse_result = array();

    $mediafile_path = mediamosa_storage::get_realpath_mediafile($job['mediafile_id']);
    $mime_type = mediamosa_mimetype::get_mime_type($mediafile_path);

    $analyse_result[mediamosa_asset_mediafile_metadata::MIME_TYPE] = array(
      'type' => mediamosa_asset_mediafile_metadata_property_db::TYPE_CHAR,
      'value' => $mime_type,
    );

    // Based on the mime_type there might be a tool that can analyse.
    // Call the mediamosa_tool_can_analyse hook.
    // Toon: There are 4 modules apparently, and there is one that triggers the if in this statement
    foreach (module_implements('mediamosa_tool_can_analyse') as $module) {
      if (module_invoke($module, 'mediamosa_tool_can_analyse', $mime_type)) {
        $analyse_result += module_invoke($module, 'mediamosa_tool_analyse', $job['mediafile_id']);
      }
    }


    // Make an informative log entry.
    $analyse_output = array();
    foreach ($analyse_result as $key => $value) {
      $analyse_output[] = $key . ' [' . $value['type'] . '] ' . $value['value'];
    }

    $link_asset = self::get_asset_link($job['job_id']);

    mediamosa_job_scheduler::log_mediafile
    (
      $job['mediafile_id'], 'TOON IT WORKS Job analyse (Job ID: @job_id) returned output: @output<br /><br />@link',
      array(
        '@job_id' => $job['job_id'],
        '@output' => implode("\n", $analyse_output) . "\n",
        '@link' => $link_asset,
      )
    );

    if (!empty($analyse_result)) {
      mediamosa_debug::log('FINISHED!', array(), 'Toon');
      $this->set_job_status($job['job_id'], mediamosa_job_server_db::JOB_STATUS_FINISHED, '1.000');
    }
    else {
      mediamosa_debug::log('FAILED!', array(), 'Toon');
      $this->set_job_status($job['job_id'], mediamosa_job_server_db::JOB_STATUS_FAILED, '1.000', 'Empty result, analyse failed.');
    }

    // Generate event analyse finished.
    //mediamosa::rules_invoke_event('mediamosa_event_analyse_finished', $job['mediafile_id']);

  }

  /**
   * Create a link to the parent asset belonging to a given job id.
   *
   * @param int $job_id
   *
   * @return string
   *  Link to an asset.
   */
  public static function get_asset_link($job_id) {

    // Get the job.
    $jobserver_job = mediamosa_job::get($job_id);

    // Get asset ID from job.
    $asset_id = $jobserver_job[mediamosa_job_db::ASSET_ID];

    // Return link.
    return l(mediamosa::t('Go to asset @asset_id', array('@asset_id' => $asset_id)), mediamosa_settings::get_url_asset($asset_id));
  }

  // Did not protect against mysql injection yet, necessary?
  function set_job_status($job_id, $job_status, $progress, $error_description = '', $error_description_args = array()) {

    // Set args in description.
    if (!empty($error_description_args)) {
      $error_description = strtr($error_description, $error_description_args);
    }

    $fields = array(
      mediamosa_job_server_db::JOB_STATUS => $job_status,
      mediamosa_job_server_db::PROGRESS => is_null($progress) ? '0.000' : $progress
    );

    switch ($job_status) {
      case mediamosa_job_server_db::JOB_STATUS_FINISHED:
      case mediamosa_job_server_db::JOB_STATUS_FAILED:
      case mediamosa_job_server_db::JOB_STATUS_CANCELLED:
        $fields[mediamosa_job_server_db::FINISHED] = mediamosa_datetime::utc_current_timestamp_now(TRUE); // finished = curent tiem
        break;
    }

    $query = mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
    ->fields($fields)
    ->condition(mediamosa_job_db::ID, (string) $job_id);

    mediamosa_debug::log((string) $query, array(), 'Toon');
    mediamosa_debug::log((string) $fields['finished']. __METHOD__, array(), 'Toon');

    mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
      ->fields($fields)
      ->condition(mediamosa_job_db::ID, (string) $job_id)
      ->execute();
  }

}
