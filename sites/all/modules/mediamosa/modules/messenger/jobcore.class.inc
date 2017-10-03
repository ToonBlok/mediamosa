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

// Only for testing
//$jobcore = new jobcore();

class jobcore{

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
      $msg_data = unserialize($msg->body);
      $debug_message = 'job_id: ' . $msg_data['job_id'] . ', job_type: ' . $msg_data['job_type'] . ' asset_id: ' . $msg_data['asset_id'] . ' mediafile_id: ' . $msg_data['mediafile_id'];
      echo ' [!] Message received: ' . $debug_message . "\n";
      //$msgContent = unserialize($msg->body);
      mediamosa_debug::log('Message received in Listen() in class: ' . __CLASS__, array(), 'Toon');
      //mediamosa_debug::log($debug_message, array(), 'Toon');

      $job_data = [];
      $job_data['job_id'] = $msg_data['job_id'];
      $job_data['job_type'] = $msg_data['job_type'];
      $job_data['asset_id'] = $msg_data['asset_id'];
      $job_data['mediafile_id'] = $msg_data['mediafile_id'];

      if ($msg_data['job_type'] == 'ANALYSE') {
        $this->analyze($job_data);
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

  // Did not protect againts mysql injection yet, necessary?
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

    // Check if its started.
    //$jobserver_job = $this->get_with_jobid($job_id);
//    if (!$jobserver_job) {
//      //self::log('Fatal: trying to update job with ID; @job_id', array('@job_id' => $job_id));
//      assert(0);
//      return;
//    }

//    // Invalidate technical metadata if analyse fails.
//    if ($job_status == mediamosa_job_server_db::JOB_STATUS_FAILED && $jobserver_job[mediamosa_job_server_db::JOB_TYPE] == mediamosa_job_server_db::JOB_TYPE_ANALYSE) {
//      $mediafile_id = $jobserver_job[mediamosa_job_server_db::MEDIAFILE_ID_SRC];
//
//      // Delete metadata, its no longer valid when analyse fails.
//      mediamosa_asset_mediafile_metadata::delete_by_mediafileid($mediafile_id);
//    }
//
//    // Set status.
//    if ($jobserver_job[mediamosa_job_server_db::JOB_STATUS] == mediamosa_job_server_db::JOB_STATUS_WAITING && $job_status == mediamosa_job_server_db::JOB_STATUS_INPROGRESS) {
//      $fields[mediamosa_job_server_db::STARTED] = mediamosa_datetime::utc_current_timestamp_now(TRUE);
//    }
//
//    if (!empty($error_description)) {
//      $fields[mediamosa_job_server_db::ERROR_DESCRIPTION] = $error_description;
//    }
//
//   // mediamosa_debug::log('reached update statement', array(), 'Toon');
//    // Update.
//    mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
//      ->fields($fields)
//      ->condition(mediamosa_job_db::ID, (string) $job_id)
//      ->execute();
  }

  function update_server_job_status($uri) {

    // Get response.
    // Toon: Returns XML with waiting jobs from mediamosa_server_job
    $response = mediamosa_http::do_internal_call($uri, 'server/joblist');
    if ($response->code != '200') {
      //self::log('Call to server/joblist did not return a 200 result (header: @header)', array('@header' => var_export($response, TRUE)));
      return;
    }

    try {
      // Toon: Parse the raw XML in $response to $response_data, a object that contains all values from the XML data in $response.
      $response_data = new mediamosa_connector_response($response->data);
    }
    catch (Exception $e) {
      //mediamosa_debug::log_export($response->data);
      //self::log(
      //  'Parse XML: @msg (@resp)',
      //  array(
      //    '@msg' => $e->getMessage(),
      //    '@resp' => print_r($response->data, TRUE),
      //  ),
      //  WATCHDOG_ERROR
      //);
      return;
    }

    // Toon: The jobs here come from the mediamosa_server_job table
    for ($i = 0; $i < $response_data->header->item_count; $i++) {
      // Get current status.
      // Toon: Retrieve the actual job
      // de job in de loop waar hij nu is.
      $job = mediamosa_job::get((string) $response_data->items->item[$i]->job_id);

      // Toon: Get some fields of this job, store them in the $fiels array.
      // Toon: Is verzameling alle wijzingen
      $fields = array(
        mediamosa_job_db::JOB_STATUS => $response_data->items->item[$i]->status,
        mediamosa_job_db::PROGRESS => $response_data->items->item[$i]->progress,
        mediamosa_job_db::ERROR_DESCRIPTION => $response_data->items->item[$i]->error_description,
      );

      // Add started time.
      if ($response_data->items->item[$i]->started != '') {
        $fields[mediamosa_job_db::STARTED] = $response_data->items->item[$i]->started;
      }

      // Add finished time.
      if ($response_data->items->item[$i]->finished != '') {
        $fields[mediamosa_job_db::FINISHED] = $response_data->items->item[$i]->finished;
      }

      // Add changed.
      $fields = mediamosa_db::db_update_enrich($fields);

      // Update.
      // Toon: mediamosa_job en mediamosa_server_job both contain the same job, however the row in
      // mediamosa_server_job table is more up-to-date and the row in mediamosa_job has to be updated.
      mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
        ->fields($fields)
        ->condition(mediamosa_job_db::ID, (string) $response_data->items->item[$i]->job_id)
        ->execute();

      // Trigger url.
      // Toon: Deze methode notified the frontend wanneer een transcoding job voltooid is. $job is de oude waarde, $responsedate is net geupdate in de db statement hierboven.
      // Toon: recording 20170913_004
      mediamosa_job::notify_transcoding($job[mediamosa_job_db::JOB_STATUS], (string) $response_data->items->item[$i]->status, (string) $response_data->items->item[$i]->job_id);

      if ($response_data->items->item[$i]->status == mediamosa_job_db::JOB_STATUS_FINISHED ||
        $response_data->items->item[$i]->status == mediamosa_job_db::JOB_STATUS_FAILED ||
        $response_data->items->item[$i]->status == mediamosa_job_db::JOB_STATUS_CANCELLED) {

        // Remove link between server and job.
        // Toon: Server is klaar met job, dus moet uit server_job tabel weggehaald moeten worden. de server_job tabel is de tabel die aangeeft welke jobs er uit gedeeld zijn aan deze server
        mediamosa_server_job::delete_by_jobid((string) $response_data->items->item[$i]->job_id);

        // Remove job on jobserver.
        //self::remove_job_on_server($uri, (string) $response_data->items->item[$i]->job_id);
      }

      if ($response_data->items->item[$i]->status == mediamosa_job_db::JOB_STATUS_FINISHED) {
        switch ($response_data->items->item[$i]->job_type) {
          case mediamosa_job_db::JOB_TYPE_TRANSCODE:
            // Statistics.
            // statistiek bij houden hoeveel jobs er zijn gestart
            mediamosa_statistics::insert_job_transcode($response_data->items->item[$i]->mediafile_dest, $job[mediamosa_job_db::ID]);
            //
            // If the transcode job is completed, add it to the mediafile.
            self::parse_finished_transcode((string) $response_data->items->item[$i]->job_id, (string) $response_data->items->item[$i]->mediafile_src, (string) $response_data->items->item[$i]->mediafile_dest);
            break;

          case mediamosa_job_db::JOB_TYPE_ANALYSE:
            // If the analyse job is completed, add it to the technical
            // metadata.
            mediamosa_asset_mediafile_metadata::store_analyse(
              (string) $response_data->items->item[$i]->job_id,
              unserialize((string) $response_data->items->item[$i]->analyse_result)
            );
            break;

          case mediamosa_job_db::JOB_TYPE_STILL:
            // If the still job is completed, add the still to db.
            // We serialize it, because of the multiple stills.
            self::add_still_to_db((string) $response_data->items->item[$i]->job_id, unserialize((string) $response_data->items->item[$i]->mediafile_dest));
            break;
        }
      }
      elseif ($response_data->items->item[$i]->status == mediamosa_job_db::JOB_STATUS_FAILED ||
        $response_data->items->item[$i]->status == mediamosa_job_db::JOB_STATUS_CANCELLED) {

        switch ($response_data->items->item[$i]->job_type) {
          case mediamosa_job_db::JOB_TYPE_TRANSCODE:
            // When transcode has failed.
            self::parse_failed_transcode((string) $response_data->items->item[$i]->job_id);
            break;
        }
      }
    }
  }

  function get_with_jobid($job_id, $server_only = true) {
    $query = mediamosa_db::db_select(mediamosa_job_server_db::TABLE_NAME, 'js')
      ->fields('js')
      ->condition(mediamosa_job_server_db::JOB_ID, $job_id);

    if ($server_only) {
      $query->condition(mediamosa_job_server_db::INSTALL_ID, mediamosa::get_server_id());
    }
    return $query->execute()->fetchAssoc();
  }

}
