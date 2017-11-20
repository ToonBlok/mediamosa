<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

class scheduler_guardian extends mediamosa_rest_call
{
  /*
   * Implements get_var_setup().
   */
  public function get_var_setup()
  {
    $var_setup = array();

    // Enrich with required REST vars.
    return self::get_var_setup_default($var_setup, FALSE);
  }

  /**
   * Implements do_call().
   */
  public function do_call()
  {
    $mediamosa = mediamosa::get();

    $this->send();
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

  function send() {
    $this->open_connection();

    $this->channel->exchange_declare(jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
//    $this->channel->queue_declare('PROGRESSION', false, true, false, false);
    $this->channel->queue_declare(scheduler::QUEUE_KEEPALIVE, false, true, false, false);
    $this->channel->queue_bind(scheduler::QUEUE_KEEPALIVE, jobcore::EXCHANGE_JOBS, scheduler::QUEUE_KEEPALIVE);

    $alive_message = [
      "server_action" => "keepalive"
    ];

    $msg = new AMQPMessage(
      serialize($alive_message),
      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
    );

    // IF YOU ADD THE EXCHANGE IT DOESN'T WORK ANYMORE...
    $this->channel->basic_publish($msg, jobcore::EXCHANGE_JOBS, scheduler::QUEUE_KEEPALIVE);

    mediamosa_debug::log('Message sent', array(), 'T - GUARDIAN');

    $this->close_connection();

  }

  function peek() {
    $this->open_connection();

    $this->channel->exchange_declare(jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare(scheduler::QUEUE_KEEPALIVE, false, true, false, false);

    $time_now = microtime(true);

    // If the variable has not been set yet, scheduler has never started before.
    if ($this->variable_get('time_last_update') == NULL){
      mediamosa_debug::log('Scheduler has never started yet', array(), 'T - GUARDIAN');
      $this->variable_set('time_last_update', $time_now);
      $this->start_scheduler();
    } else {
      // Loop trough all the messages available in order to get the most recent message.
//      $varOne = $this->channel->basic_get(scheduler::QUEUE_KEEPALIVE);
//      if ($varOne == NULL) {
//        $varTwo = true;
//      } else {
//        $varTwo = false;
//      }
//      mediamosa_debug::log('Task queue is empty: ' . $varTwo, array(), 'T - GUARDIAN');

      while ($this->channel->basic_get(scheduler::QUEUE_KEEPALIVE) != NULL) {
        $var = $this->channel->basic_get(scheduler::QUEUE_KEEPALIVE);
        mediamosa_debug::log('Guardian received keepalive message back with time: ' . $var->body, array(), 'T - GUARDIAN');


        // Receive a message and remove it from RabbitMQ.
        $msg = $this->channel->basic_get(scheduler::QUEUE_KEEPALIVE, 1);

        $time_last_update = variable_get('time_last_update');

        // if: The timestamp in the message is more recent, update time_last_update.
        // else: Prevent updating of time_last_update with a value older than it is already.
        if ($time_last_update < $msg['timestamp']) {
          mediamosa_debug::log('New time has been set. Old: ' . $time_last_update . ', new: ' . $msg['timestamp'], 'T - GUARDIAN');
          $this->variable_set('time_last_update', $msg['timestamp']);
        }

      }

      $time_last_update = variable_get('time_last_update');

      // Replace 300 with drush value with default 300
      if (($time_last_update + 300) < ($time_now)) {
        mediamosa_debug::log('Scheduler has not responded for the last 5 minutes', array(), 'T - GUARDIAN');
        $this->start_scheduler();
      } else {
        mediamosa_debug::log('Scheduler has responded in the last 5 minutes', array(), 'T - GUARDIAN');
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
