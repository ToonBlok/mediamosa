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
          self::VAR_DESCRIPTION => "The jobcore type.",
        ),
        self::QUEUE => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => "The queue to listen for.",
        ),
        self::QUEUE_KEEPALIVE_SERVER => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => "The name of the keepalive queue for this instance that guardian will check.",
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
    $queue_keepalive_server_guardian = $queue_keepalive_server . '_guardian';

    mediamosa_debug::log('** Start guardian', array(), 'Tg - ' . $queue_keepalive_server);

    // Send a keepalive message to each server.
    $this->rmq_send($queue_keepalive_server);
    sleep(3);
    // Take a look inside the guardian queue for this server to check for a response.
    $this->rmq_peek($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type);

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

    $alive_message = [
      mediamosa_messenger_scheduler::MSG_ID => $this->variable_get('UNIQUE_MSG_ID', 1),
      mediamosa_messenger_scheduler::MSG_STATUS => mediamosa_messenger_scheduler::SERVER_STATUS_PENDING
    ];

    $this->variable_set('UNIQUE_MSG_ID', $alive_message[mediamosa_messenger_scheduler::MSG_ID]++);

    $msg = new AMQPMessage(
      serialize($alive_message),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    $this->channel->basic_publish($msg, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $queue_keepalive_server);
    mediamosa_debug::log('[' . $alive_message[mediamosa_messenger_scheduler::MSG_ID] . '] Guardian sent a message to ' . $queue_keepalive_server . ' with message content: ' . serialize($alive_message) . ' at ' . microtime(TRUE), array(), 'T - RMQ');

    $this->rmq_close_connection();
  }

  function rmq_peek($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type) {
    $drush_var = $queue_keepalive_server . '_time_last_update';

    $this->rmq_open_connection();

    $this->channel->exchange_declare(mediamosa_messenger_jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($queue_keepalive_server_guardian, false, true, false, false);
    $this->channel->queue_bind($queue_keepalive_server_guardian, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $queue_keepalive_server_guardian);

    $time_now = microtime(true);

    // If the variable has not been set yet, scheduler has never started before.
    if ($this->variable_get($drush_var) == NULL){
      mediamosa_debug::log($queue_keepalive_server . ' has never started yet', array(), 'Tg - ' . $queue_keepalive_server);
      $this->variable_set($drush_var, $time_now);


      if ($core_type == self::CORE_SCHEDULER) {
        mediamosa_debug::log('Booting up scheduler.', array(), 'Tg - ' . $queue_keepalive_server);
        $this->mediamosa_messenger_start_scheduler($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type);
      } else {
        mediamosa_debug::log('Booting up jobcore.', array(), 'Tg - ' . $queue_keepalive_server);
        $this->mediamosa_messenger_start_jobcore($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type);
      }

    } else {

      $rmq_msg = $this->channel->basic_get($queue_keepalive_server_guardian);
      while (!is_null($rmq_msg)) {

        try {
          $this->channel->basic_ack($rmq_msg->delivery_info['delivery_tag']);
        } catch (Exception $e) {
          // Called ack on empty message, something wrong with the messages being sent.
          throw new mediamosa_exception_error(mediamosa_error::ERRORCODE_STARTING_JOB_FAILED);
        }

        $msg = unserialize($rmq_msg->body);
        mediamosa_debug::log('[' . $msg[mediamosa_messenger_scheduler::MSG_ID] . '] Guardian received a msg with content: ' . serialize($msg[mediamosa_messenger_scheduler::MSG_DATA]) . ' at ' . microtime(TRUE), array(), 'T - RMQ');
        mediamosa_debug::log_export($msg, WATCHDOG_NOTICE, 'T - RMQ');

        $this->mediamosa_messenger_update_time($drush_var, $msg[mediamosa_messenger_scheduler::MSG_TIMESTAMP], $queue_keepalive_server);

        $rmq_msg = $this->channel->basic_get($queue_keepalive_server_guardian);

        //$this->close_connection(); // Why was this here??????
      }

      $this->rmq_close_connection(); // Why was this here??????

      // If in unit test, there is no drupal value and will lead to intant restarts
      $time_last_update = variable_get($drush_var);

      // Replace 300 with drush value with default 300

      $max_wait_time = $this->variable_get('max_wait_time', 300);

      // Scheduler has not responded in the last 5 minutes
      if (($time_last_update + $max_wait_time) < ($time_now)) {
        mediamosa_debug::log($queue_keepalive_server . ' has been silent for 5 minutes. Booting up ' . $queue_keepalive_server, array(), 'Tg - ' . $queue_keepalive_server);
        $this->mediamosa_messenger_update_time($drush_var, $time_now, $queue_keepalive_server);

        if ($core_type == self::CORE_SCHEDULER) {
          exec('Scheduler boot command given with following var >> /home/tony/test.txt');
          exec('echo ' .  $GLOBALS['drupal_test_info']['test_run_id'] . ' >> /home/tony/test.txt');
          mediamosa_debug::log('Booting up scheduler.', array(), 'Tg - ' . $queue_keepalive_server);
          $this->mediamosa_messenger_start_scheduler($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type);
        } else {
          exec('Jobcore boot command given with following var >> /home/tony/test.txt');
          exec('echo ' .  $GLOBALS['drupal_test_info']['test_run_id'] . ' >> /home/tony/test.txt');
          mediamosa_debug::log('Booting up jobcore.', array(), 'Tg - ' . $queue_keepalive_server);
          $this->mediamosa_messenger_start_jobcore($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type);
        }

      } else { // Scheduler has responded in the last 5 minutes
        mediamosa_debug::log($queue_keepalive_server . ' is online. No further action undertaken.', array(), 'Tg - ' . $queue_keepalive_server);
      }

    }


  }

  function mediamosa_messenger_update_time($drush_var, $time_stamp, $queue_keepalive_server) {
    $time_last_update = variable_get($drush_var);

    // if: The timestamp in the message is more recent, update time_last_update.
    // else: Prevent updating of time_last_update with a value older than it is already.
//        mediamosa_debug::log('If ' . $time_last_update . ' < ' . $update_msg['timestamp'], array(), 'Tg - ' . $queue_keepalive_server);
    if ($time_last_update < $time_stamp) {
//          mediamosa_debug::log('New time has been set. Old: ' . $time_last_update . ', new: ' . $update_msg['timestamp'], array(), 'Tg - ' . $queue_keepalive_server);
      mediamosa_debug::log($time_last_update . ' became ' . $time_stamp . '. This action occurred at ' . microtime(TRUE), array(), 'Tg - ' . $queue_keepalive_server);
      $this->variable_set($drush_var, $time_stamp);
    }

  }

  function mediamosa_messenger_start_scheduler($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type) {
    $uri = '/scheduler/start?';

    $query_data = [
      mediamosa_scheduler_start::QUEUE => $queue,
      mediamosa_scheduler_start::QUEUE_KEEPALIVE_SERVER => $queue_keepalive_server, // The scheduler needs this to know what to listen to for keepalive message
      mediamosa_scheduler_start::QUEUE_KEEPALIVE_SERVER_GUARDIAN => $queue_keepalive_server_guardian, // The scheduler needs this to know where to send keepalive messages back to
      mediamosa_scheduler_start::CORE_TYPE => $core_type,
    ];


    $this->mediamosa_messenger_call($uri, $query_data);
  }

  function mediamosa_messenger_start_jobcore($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type) {
    $uri = '/jobcore/start?';

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
