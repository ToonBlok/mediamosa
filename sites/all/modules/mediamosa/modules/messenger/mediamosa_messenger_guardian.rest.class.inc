<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class mediamosa_messenger_guardian extends mediamosa_rest_call
{
  const CORE_TYPE = 'core_type';
  const QUEUE = 'queue';
  const QUEUE_KEEPALIVE_SERVER = 'queue_keepalive_server';

  const CORE_SCHEDULER = 'SCHEDULER';

  /**
   * Implements get_var_setup().
   */
  public function get_var_setup() {
    $var_setup = array(
      self::VARS => array(
        self::CORE_TYPE=> array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => 'The core type that gets passed along to servers when they need to start. Servers use it to determine what kind of behavior to exhibit.',
        ),
        self::QUEUE => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => 'The queue name that gets passed along to servers when they need to start. Servers start listening to this queue.',
        ),
        self::QUEUE_KEEPALIVE_SERVER => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => 'The queue name that we send to the servers. Servers will listen for keepalive message on this queue name.',
        )
      )
    );

    // Enrich with required REST vars.
    return self::get_var_setup_default($var_setup, FALSE);
  }

  /**
   * Implements do_call().
   */
  public function do_call() {
    $mediamosa = mediamosa::get();

    $queue = $this->get_param_value(self::QUEUE);
    $queue_keepalive_server = $this->get_param_value(self::QUEUE_KEEPALIVE_SERVER);
    $core_type = $this->get_param_value(self::CORE_TYPE);
    //The keepalive queue on the guardian side for the specific core type for which this guardian has been triggered. The guardian class checks this queue for keepalive responses from the servers it watches.
    $queue_keepalive_server_guardian = $queue_keepalive_server . '_guardian';

    mediamosa_debug::log('** Start guardian', array(), 'Tg - ' . $queue_keepalive_server);

    // Send a keepalive message to each server.
    $this->rmq_send($queue_keepalive_server);
    sleep(3);
    $this->mediamosa_messenger_cycle($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type);

    mediamosa_debug::log('** End guardian', array(), 'Tg - ' . $queue_keepalive_server);

    $mediamosa->add_item(
      array(
        'server_type' => $core_type,
      )
    );
  }

  function rmq_send($queue_keepalive_server) {
    $this->rmq_open_connection();

    $this->channel->exchange_declare(mediamosa_messenger_jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($queue_keepalive_server, false, true, false, false);
    $this->channel->queue_bind($queue_keepalive_server, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $queue_keepalive_server);

    $msg = [
      mediamosa_messenger_scheduler::MSG_ID => $this->variable_get('UNIQUE_MSG_ID', 1),
      mediamosa_messenger_scheduler::MSG_STATUS => mediamosa_messenger_scheduler::SERVER_STATUS_PENDING
    ];

    $this->variable_set('UNIQUE_MSG_ID', $msg[mediamosa_messenger_scheduler::MSG_ID]++);

    $rmq_msg = new AMQPMessage(
      serialize($msg),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    $this->channel->basic_publish($rmq_msg, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $queue_keepalive_server);
    mediamosa_debug::log('[' . $msg[mediamosa_messenger_scheduler::MSG_ID] . '] Guardian sent a message to ' . $queue_keepalive_server . ' with message content: ' . serialize($msg) . ' at ' . microtime(TRUE), array(), 'T - RMQ');

    $this->rmq_close_connection();
  }

  function mediamosa_messenger_cycle($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type) {
    $drush_time_last_update = $queue_keepalive_server . '_time_last_update';
    $time_now = microtime(TRUE);

    // If the variable has not been set yet, scheduler has never started before.
    if (is_null($this->variable_get($drush_time_last_update))){
      $this->variable_set($drush_time_last_update, $time_now);

      mediamosa_debug::log($queue_keepalive_server . ' has never started yet', array(), 'Tg - ' . $queue_keepalive_server);
      mediamosa_debug::log('Start time is: ' . $time_now, array(), 'Tg - ' . $queue_keepalive_server);

      $this->mediamosa_messenger_start_core($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type);
    } else {
      // See if the server for which this instance guardian was started has sent a keepalive response. If so, update time.
      $this->rmq_peek($queue_keepalive_server, $queue_keepalive_server_guardian, $drush_time_last_update);
      $time_last_update = variable_get($drush_time_last_update);
      $max_wait_time = $this->variable_get('max_wait_time', 300);


      // If the last response of the server was longer ago than the max_wait_time, the server is determined to have crashed.
      if (($time_last_update + $max_wait_time) < ($time_now)) { // Scheduler has not responded in the last 5 minutes
        $this->mediamosa_messenger_update_last_response_time($drush_time_last_update, $time_now, $queue_keepalive_server);
        $this->mediamosa_messenger_start_core($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type);
        
        mediamosa_debug::log($queue_keepalive_server . ' has been silent for 5 minutes. Booting up ' . $queue_keepalive_server, array(), 'Tg - ' . $queue_keepalive_server);
      } else { // Scheduler has responded in the last 5 minutes
        mediamosa_debug::log($queue_keepalive_server . ' is online. No further action undertaken.', array(), 'Tg - ' . $queue_keepalive_server);
      }
    }
  }

  function rmq_peek($queue_keepalive_server, $queue_keepalive_server_guardian, $drush_time_last_update) {
    $this->rmq_open_connection();

    $this->channel->exchange_declare(mediamosa_messenger_jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($queue_keepalive_server_guardian, false, true, false, false);
    $this->channel->queue_bind($queue_keepalive_server_guardian, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $queue_keepalive_server_guardian);

    // Get the first message from this guardians queue to jumpstart the while loop.
    $rmq_msg = $this->channel->basic_get($queue_keepalive_server_guardian);

    // As long as there are messages in this guardians queue we process them.
    while (!is_null($rmq_msg)) {

      try {
        $this->channel->basic_ack($rmq_msg->delivery_info['delivery_tag']);
      } catch (Exception $e) {
        // Called ack on empty message, something wrong with the messages being sent.
        throw new mediamosa_exception_error(mediamosa_error::ERRORCODE_STARTING_JOB_FAILED);
      }

      $msg = unserialize($rmq_msg->body);
      $this->mediamosa_messenger_update_last_response_time($drush_time_last_update, $msg[mediamosa_messenger_scheduler::MSG_TIMESTAMP], $queue_keepalive_server);
      // Get next message, if NULL end while, if message exist process the message by going trough the while loop again.
      $rmq_msg = $this->channel->basic_get($queue_keepalive_server_guardian);

      mediamosa_debug::log('[' . $msg[mediamosa_messenger_scheduler::MSG_ID] . '] Guardian received a msg with content: ' . serialize($msg[mediamosa_messenger_scheduler::MSG_DATA]) . ' at ' . microtime(TRUE), array(), 'T - RMQ');
      mediamosa_debug::log_export($msg, WATCHDOG_NOTICE, 'T - RMQ');
    }

    $this->rmq_close_connection();
  }

  function mediamosa_messenger_update_last_response_time($drush_time_last_update, $server_response_time, $queue_keepalive_server) {
    $time_last_update = variable_get($drush_time_last_update);

    // if The timestamp in the message is more recent, update time_last_update.
    if ($time_last_update < $server_response_time) {
      $this->variable_set($drush_time_last_update, $server_response_time);
      mediamosa_debug::log($time_last_update . ' became ' . $server_response_time . '. This action occurred at ' . microtime(TRUE), array(), 'Tg - ' . $queue_keepalive_server);
    }
  }

  function mediamosa_messenger_start_core($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type) {
    $uri = '';

    if ($core_type == self::CORE_SCHEDULER) {
      $uri = '/scheduler/start?';
      mediamosa_debug::log('Booting up scheduler.', array(), 'Tg - ' . $queue_keepalive_server);
    } else {
      $uri = '/jobcore/start?';
      mediamosa_debug::log('Booting up jobcore.', array(), 'Tg - ' . $queue_keepalive_server);
    }

    $query_data = [
      mediamosa_messenger_jobcore::QUEUE => $queue,
      mediamosa_messenger_jobcore::QUEUE_KEEPALIVE_SERVER => $queue_keepalive_server, // The scheduler needs this to know what to listen to for keepalive message
      mediamosa_messenger_jobcore::QUEUE_KEEPALIVE_SERVER_GUARDIAN => $queue_keepalive_server_guardian, // The scheduler needs this to know where to send keepalive messages back to
      mediamosa_messenger_jobcore::CORE_TYPE => $core_type,
    ];

    $this->mediamosa_messenger_call($uri, $query_data);
  }

  function mediamosa_messenger_call($uri, array $query_data = array()) {
    $mediamosa_jobscheduler_uri = variable_get('mediamosa_jobscheduler_uri', NULL);

    if (isset($mediamosa_jobscheduler_uri)) {
      $uri = $uri . http_build_query($query_data);
      $url = mediamosa_http::uri2url($mediamosa_jobscheduler_uri) . $uri;

      mediamosa_http::do_head_internal_call($url);
      mediamosa_debug::log('Did REST call to: ' . $url, array(), 'T - REST');
    } else {
      mediamosa_job_scheduler::log('Jobscheduler URL not set, please setup jobscheduler server in the @link.', array('@link' => l(t('MediaMosa configuration'), 'admin/mediamosa/config/global')), WATCHDOG_ALERT, 'job_cron');
    }
  }

  function rmq_open_connection() {
    $host = $this->variable_get('mediamosa_scheduler_host', 'localhost');
    $port = $this->variable_get('mediamosa_scheduler_port', 5672);
    $username = $this->variable_get('mediamosa_scheduler_username', 'guest');
    $password = $this->variable_get('mediamosa_scheduler_password', 'guest');

    $this->connection = new AMQPStreamConnection($host, $port, $username, $password);
    $this->channel = $this->connection->channel();
  }

  function rmq_close_connection() {
    $this->channel->close();
    $this->connection->close();
  }

  /**
   * Returns a persistent variable.
   *
   * Case-sensitivity of the variable_* functions depends on the database
   * collation used. To avoid problems, always use lower case for persistent
   * variable names.
   *
   * @param $name
   *   The name of the variable to return.
   * @param $default
   *   The default value to use if this variable has never been set.
   *
   * @return
   *   The value of the variable. Unserialization is taken care of as necessary.
   *
   * @see variable_del()
   * @see variable_set()
   */
  function variable_get($name, $default = NULL) {
    return variable_get($name, $default);
  }

  function variable_set($name, $value) {
    variable_set($name, $value);
  }

}
