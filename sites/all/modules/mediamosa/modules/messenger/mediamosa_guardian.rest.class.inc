<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class scheduler_guardian extends mediamosa_rest_call
{
  const CORE_TYPE = 'core_type';
  const KEEPALIVE_QUEUE_NAME = 'keepalive_queue_name';

  /*
   * Implements get_var_setup().
   */
  public function get_var_setup()
  {
    $var_setup = array(
      self::VARS => array(
        self::CORE_TYPE=> array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => "The jobcore type.",
        ),
        self::KEEPALIVE_QUEUE_NAME => array(
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
  public function do_call()
  {
    $mediamosa = mediamosa::get();

    $core_type = $this->get_param_value(self::CORE_TYPE);
    $queue_keepalive_server = $this->get_param_value(self::KEEPALIVE_QUEUE_NAME);

    mediamosa_debug::log('** Start guardian', array(), 'Tg - ' . $queue_keepalive_server);

    $queue_keepalive_server_guardian = $queue_keepalive_server . '_guardian';

    $this->send($queue_keepalive_server);
    sleep(3);
    $this->peek($queue_keepalive_server, $queue_keepalive_server_guardian, $core_type);

    $mediamosa->add_item(
      array(
        'version' => mediamosa_version::get_current_version_str(FALSE)
      )
    );
    mediamosa_debug::log('** End guardian', array(), 'Tg - ' . $queue_keepalive_server);
  }


  function send($queue_keepalive_server) {
    $this->open_connection();

    $this->channel->exchange_declare(jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($queue_keepalive_server, false, true, false, false);
    $this->channel->queue_bind($queue_keepalive_server, jobcore::EXCHANGE_JOBS, $queue_keepalive_server);

    $alive_message = [
      "MSG_ID" => $this->variable_get("alive_message_unique_id", 1),
      "STATUS_REQUEST" => "pending",
      "STATUS" => "undetermined"
    ];

    $this->variable_set("alive_message_unique_id", $alive_message['MSG_ID'] + 1);

    $msg = new AMQPMessage(
      serialize($alive_message),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    $this->channel->basic_publish($msg, jobcore::EXCHANGE_JOBS, $queue_keepalive_server);
    mediamosa_debug::log('[' . $alive_message['MSG_ID'] . '] Guardian sent a message to ' . $queue_keepalive_server . ' with message content: ' . serialize($alive_message) . ' at ' . microtime(TRUE), array(), 'T - RMQ');

    $this->close_connection();
  }

  function peek($queue_keepalive_server, $queue_keepalive_server_guardian, $core_type = '') {

    $callback = function($rmq_msg) {
      mediamosa_debug::log('STRANGE CALLBACK WORKED', array(), 'T - MISC');
    };

    $drush_var = $queue_keepalive_server . '_time_last_update';

    $this->open_connection();

    $this->channel->exchange_declare(jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($queue_keepalive_server_guardian, false, true, false, false);
    $this->channel->queue_bind($queue_keepalive_server_guardian, jobcore::EXCHANGE_JOBS, $queue_keepalive_server_guardian);

    $time_now = microtime(true);

    // If the variable has not been set yet, scheduler has never started before.
    if ($this->variable_get($drush_var) == NULL){
      mediamosa_debug::log($queue_keepalive_server . ' has never started yet', array(), 'Tg - ' . $queue_keepalive_server);
      $this->variable_set($drush_var, $time_now);

      if ($core_type == 'SCHEDULER') {
        mediamosa_debug::log('Booting up scheduler.', array(), 'Tg - ' . $queue_keepalive_server);
        $this->start_scheduler($queue_keepalive_server, $queue_keepalive_server_guardian);
      } else {
        mediamosa_debug::log('Booting up jobcore.', array(), 'Tg - ' . $queue_keepalive_server);
        $this->start_jobcore($core_type, $queue_keepalive_server, $queue_keepalive_server_guardian);
      }

    } else {

      $msg = $this->channel->basic_get($queue_keepalive_server_guardian);
      while (!is_null($msg)) {
        $this->channel->basic_ack($msg->delivery_info['delivery_tag']);

        $update_msg = unserialize($msg->body);
        mediamosa_debug::log('[' . $update_msg['MSG_ID'] . '] Guardian received a msg with content: ' . serialize($update_msg) . ' at ' . microtime(TRUE), array(), 'T - RMQ');
        mediamosa_debug::log_export($msg, WATCHDOG_NOTICE, 'T - RMQ');

        $this->update_time($drush_var, $update_msg['timestamp'], $queue_keepalive_server);

        $msg = $this->channel->basic_get($queue_keepalive_server_guardian);

        //$this->close_connection(); // Why was this here??????
      }

      $this->close_connection(); // Why was this here??????

      $time_last_update = variable_get($drush_var);

      // Replace 300 with drush value with default 300

      $max_wait_time = $this->variable_get('max_wait_time', 300);

      // Scheduler has not responded in the last 5 minutes
      if (($time_last_update + $max_wait_time) < ($time_now)) {
        mediamosa_debug::log($queue_keepalive_server . ' has been silent for 5 minutes. Booting up ' . $queue_keepalive_server, array(), 'Tg - ' . $queue_keepalive_server);
        $this->update_time($drush_var, $time_now, $queue_keepalive_server);

        if (is_null($core_type)) {
          $this->start_scheduler($queue_keepalive_server, $queue_keepalive_server_guardian);
        } else {
          $this->start_jobcore($core_type, $queue_keepalive_server, $queue_keepalive_server_guardian);
        }

      } else { // Scheduler has responded in the last 5 minutes
        mediamosa_debug::log($queue_keepalive_server . ' is online. No further action undertaken.', array(), 'Tg - ' . $queue_keepalive_server);
      }

    }


  }

  function update_time($drush_var, $time_stamp, $queue_keepalive_server) {
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

  function start_scheduler($queue_keepalive_server, $queue_keepalive_server_guardian) {
    $uri = '/jobcore/start';

    $query_data = [
      jobcore::QUEUE_KEEPALIVE_SERVER_GUARDIAN => $queue_keepalive_server_guardian, // The scheduler needs this to know where to send keepalive messages back to
      jobcore::QUEUE_KEEPALIVE_SERVER => $queue_keepalive_server // The scheduler needs this to know what to listen to for keepalive messages
    ];


    $this->call($uri, $query_data);
  }

  function start_jobcore($core_type, $queue_keepalive_server, $queue_keepalive_server_guardian) {
    $uri = '/jobcore/start';

    $query_data = [
      jobcore::CORE_TYPE => $core_type,
      jobcore::QUEUE_KEEPALIVE_SERVER_GUARDIAN => $queue_keepalive_server_guardian, // The jobcore needs this to know where to send keepalive messages back to
      jobcore::QUEUE_KEEPALIVE_SERVER => $queue_keepalive_server // The jobcore needs this to know what to listen to for keepalive messages
    ];

    $this->call($uri, $query_data);
  }

  function call($uri, $query_data) {
    $uri = $uri . '?' . http_build_query($query_data);

    // Test REST CALL
    $mediamosa_jobscheduler_uri = variable_get('mediamosa_jobscheduler_uri', NULL);
    if (isset($mediamosa_jobscheduler_uri)) {

      $url = mediamosa_http::uri2url($mediamosa_jobscheduler_uri) . $uri;

      // Log it.
      mediamosa_job_scheduler::log_debug('Triggering job scheduler @ @uri', array('@uri' => $url));

      // Dont trigger in sandbox.
      if (mediamosa::in_simpletest_sandbox()) {
        // So we wait till finished.
        MediaMosaTestCase::staticInternalRestCallGet($url);
      } else {
        // Trigger.
        mediamosa_http::do_head_internal_call($url);
      }
    } else {
      mediamosa_job_scheduler::log('Jobscheduler URL not set, please setup jobscheduler server in the @link.', array('@link' => l(t('MediaMosa configuration'), 'admin/mediamosa/config/global')), WATCHDOG_ALERT, 'job_cron');
    }

  }

  function open_connection() {

    $host = $this->variable_get('mediamosa_scheduler_host', 'localhost');
    $port = $this->variable_get('mediamosa_scheduler_port', 5672);
    $username = $this->variable_get('mediamosa_scheduler_username', 'guest');
    $password = $this->variable_get('mediamosa_scheduler_password', 'guest');

    $this->connection = new AMQPStreamConnection($host, $port, $username, $password);
    $this->channel = $this->connection->channel();
  }

  function close_connection() {
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
