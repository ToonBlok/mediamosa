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

    $this->check();
//    $this->send();
//    $this->listen();


    $mediamosa->add_item(
      array(
        'version' => mediamosa_version::get_current_version_str(FALSE)
      )
    );
  }

  function check(){
    $this->open_connection();
    // This code will declare a queue and then return the amount of active queues, therefore if the return value is 1, the scheduler is not open
    $scheduler_consumer_count = $this->channel->queue_declare('PROGRESSION', false, true, false, false);
    $this->close_connection();

    if ($scheduler_consumer_count[2]) {
      mediamosa_debug::log('online', array(), 'T - 1349');
    } else {
      mediamosa_debug::log('offline, creating...', array(), 'T - 1349');
      $this->call('/jobscheduler/roundupjobs?server_action=listen');
    }

  }

  function call($path) {
    // Test REST CALL
    $mediamosa_jobscheduler_uri = variable_get('mediamosa_jobscheduler_uri', NULL);
    if (isset($mediamosa_jobscheduler_uri)) {

      $url = mediamosa_http::uri2url($mediamosa_jobscheduler_uri) . $path;

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

//  function send() {
//    $this->open_connection();
//
//    $this->channel->exchange_declare('jobs', 'direct', false, false, false);
//    $this->channel->queue_declare('ANALYSE', false, true, false, false);
//
//    $msg = new AMQPMessage(
//      'art thou perished',
//      array('delivery_mode' => AMQPMessage::DELIVERY_MODE_PERSISTENT)
//    );
//
//    // Temporarily named PROGRESSION, but its start core
//    $this->channel->basic_publish($msg, 'jobs', 'PROGRESSION');
//
//    $this->close_connection();
//  }
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

  function variable_get($name, $default = NULL) {
    return variable_get($name, $default);
  }

  function variable_set($name, $value) {
    variable_set($name, $value);
  }

}
