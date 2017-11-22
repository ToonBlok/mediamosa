<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class scheduler_guardian extends mediamosa_rest_call
{
  const QUEUE_GUARDIAN = 'guardian';
  const SERVER_TYPE = 'server_type';
  /*
   * Implements get_var_setup().
   */
  public function get_var_setup()
  {
    $var_setup = array(
      self::VARS => array(
        self::SERVER_TYPE=> array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => "",
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
    mediamosa_debug::log(__CLASS__ . ' iniated.', array(), 'T - GUARDIAN');
    $server_type = $this->get_param_value(self::SERVER_TYPE);
    mediamosa_debug::log('server type: ' . $server_type, array(), 'T - GUARDIAN');

    if ($server_type == 'scheduler') {
      $queue = scheduler::QUEUE_KEEPALIVE;
    } elseif ($server_type == 'jobcore') {
      $queue = jobcore::QUEUE_KEEPALIVE;
    } else {
      mediamosa_debug::log('Something went wrong with passing along the $server_type', array(), 'T - GUARDIAN');
      exit();
    }

    $this->send($queue);
    $this->peek();

    $mediamosa->add_item(
      array(
        'version' => mediamosa_version::get_current_version_str(FALSE)
      )
    );
  }

  function call($path, $value = '') {
    // Test REST CALL
    $mediamosa_jobscheduler_uri = variable_get('mediamosa_jobscheduler_uri', NULL);
    if (isset($mediamosa_jobscheduler_uri)) {

      $url = mediamosa_http::uri2url($mediamosa_jobscheduler_uri) . $path . $value;

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

  function send($queue) {
    $this->open_connection();

    $this->channel->exchange_declare(jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
//    $this->channel->queue_declare('PROGRESSION', false, true, false, false);
    $this->channel->queue_declare($queue, false, true, false, false);
    $this->channel->queue_bind($queue, jobcore::EXCHANGE_JOBS, $queue);

    $alive_message = [
      "server_action" => "keepalive"
    ];

    $msg = new AMQPMessage(
      serialize($alive_message),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    mediamosa_debug::log('sent keepalive message to queue ' . $queue, array(), 'T - SCHEDULER');
    // IF YOU ADD THE EXCHANGE IT DOESN'T WORK ANYMORE...
    $this->channel->basic_publish($msg, jobcore::EXCHANGE_JOBS, $queue);

    $this->close_connection();

  }

  function peek() {
    $this->open_connection();

    $this->channel->exchange_declare(jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare(self::QUEUE_GUARDIAN, false, true, false, false);
    $this->channel->queue_bind(self::QUEUE_GUARDIAN, jobcore::EXCHANGE_JOBS, self::QUEUE_GUARDIAN);

    $time_now = microtime(true);

//    $var = unserialize($this->channel->basic_get(self::QUEUE_GUARDIAN)->body);
//    mediamosa_debug::log_export($var, WATCHDOG_NOTICE, 'T - GUARDIAN' );

    // If the variable has not been set yet, scheduler has never started before.
    if ($this->variable_get('time_last_update') == NULL){
      mediamosa_debug::log('Scheduler has never started yet', array(), 'T - GUARDIAN');
      $this->variable_set('time_last_update', $time_now);
      $this->start_scheduler();
    } else {
      // Loop trough all the messages available in order to get the most recent message.

      $testvar = 0;

      while ($this->channel->basic_get(self::QUEUE_GUARDIAN) != NULL) {
        $testvar += 1;
        $msg = $this->channel->basic_get(self::QUEUE_GUARDIAN, 1);
        mediamosa_debug::log_export($msg, WATCHDOG_NOTICE, 'T - GUARDIAN' );

        $update_msg = unserialize($msg->body);

        //mediamosa_debug::log('Received message with timestamp: ' . $update_msg['timestamp'], array(), 'T - GUARDIAN');


        // Receive a message and remove it from RabbitMQ.
//        $msg = $this->channel->basic_get(self::QUEUE_GUARDIAN, 1);

        $time_last_update = variable_get('time_last_update');

        // if: The timestamp in the message is more recent, update time_last_update.
        // else: Prevent updating of time_last_update with a value older than it is already.
        mediamosa_debug::log('If ' . $time_last_update . ' < ' . $update_msg['timestamp'], array(), 'T - GUARDIAN');
        if ($time_last_update < $update_msg['timestamp']) {
//          mediamosa_debug::log('New time has been set. Old: ' . $time_last_update . ', new: ' . $update_msg['timestamp'], array(), 'T - GUARDIAN');
          mediamosa_debug::log('New time received and set (' . $time_last_update . ' became ' . $update_msg['timestamp'] . ').', array(), 'T - GUARDIAN');
          $this->variable_set('time_last_update', $update_msg['timestamp']);
        }

      }
      mediamosa_debug::log($testvar, array(), 'T - GUARDIAN');

      $time_last_update = variable_get('time_last_update');

      // Replace 300 with drush value with default 300

      // Scheduler has not responded in the last 5 minutes
      if (($time_last_update + 300) < ($time_now)) {
        mediamosa_debug::log('Scheduler is offline. Iniating scheduler.', array(), 'T - GUARDIAN');
        $this->start_scheduler();
      } else { // Scheduler has responded in the last 5 minutes
        mediamosa_debug::log('Scheduler is online. No further action undertaken.', array(), 'T - GUARDIAN');
      }

    }


    $this->close_connection();
  }


//
//  function listen() {
//    $server_type = 'PROGRESSION';
//    $this->open_connection();
//    $this->channel->exchange_declare('jobs', 'direct', false, false, false);
//    $this->channel->queue_declare('keep_alive', false, true, false, false);
//    $this->channel->queue_bind('keep_alive', 'jobs', 'keep_alive');
//
//    $callback = function($msg){
//      $job = unserialize($msg->body);
////      echo ' [!] Message received: job_id: ' . $job['job_id'] . ', job_type: ' . $job['job_type'] . ' asset_id: ' . $job['asset_id'] . ' mediafile_id: ' . $job['mediafile_id'] . "\n";
//      mediamosa_debug::log(' [!] Job ' . $job['job_id'] . ' = ' . $job['new_job_status']['Progress'] . ' done.', array(), 'T - Scheduler');
//
//      echo ' [x] Done', "\n";
//      $msg->delivery_info['channel']->basic_ack($msg->delivery_info['delivery_tag']);
//    };
//
//    // basic_qos: Tells RabbitMQ not to give more than one message to a worker at a time.
//    $this->channel->basic_qos(null, 1, null);
//    $this->channel->basic_consume($server_type, '', false, false, false, false, $callback);
//
//    while(count($this->channel->callbacks)) {
//      $this->channel->wait();
//    }
//
//    $this->close_connection();
//  }
//
//  function open_connection() {
//
//    $host = $this->variable_get('mediamosa_scheduler_host', 'localhost');
//    $port = $this->variable_get('mediamosa_scheduler_port', 5672);
//    $username = $this->variable_get('mediamosa_scheduler_username', 'guest');
//    $password = $this->variable_get('mediamosa_scheduler_password', 'guest');
//
//    $this->connection = new AMQPStreamConnection($host, $port, $username, $password);
//    $this->channel = $this->connection->channel();
//  }
//
//  function close_connection() {
//    $this->channel->close();
//    $this->connection->close();
//  }
//
//  function variable_get($name, $default = NULL) {
//    return variable_get($name, $default);
//  }
//
//  function variable_set($name, $value) {
//    variable_set($name, $value);
//  }

  function start_scheduler() {
    mediamosa_debug::log('start_scheduler() iniated', array(), 'T - GUARDIAN');
    $this->call('/jobscheduler/roundupjobs?server_action=', 'listen');
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
