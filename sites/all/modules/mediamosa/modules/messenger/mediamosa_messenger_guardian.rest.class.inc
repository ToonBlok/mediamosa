<?php

require_once DRUPAL_ROOT . '/vendor/autoload.php';
use PhpAmqpLib\Connection\AMQPStreamConnection;
use PhpAmqpLib\Message\AMQPMessage;

/**
 * URI: guardian/check
 * Method: GET
 *
 * Start guardian activities for the server passed in the REST call. Start servers, check servers
 * availability, restart servers, and update the time they have last responded.
 */
class mediamosa_messenger_guardian extends mediamosa_messenger_rmq
{
  const CORE_TYPE = 'core_type';
  const QUEUE = 'queue';
  const QUEUE_KEEPALIVE_SERVER = 'queue_keepalive_server';

  // Queue name of the scheduler.
  const CORE_SCHEDULER = 'SCHEDULER';

  protected $queue_to_peek;

  /**
   * Implements get_var_setup().
   */
  public function get_var_setup() {
    $var_setup = array(
      self::VARS => array(
        self::CORE_TYPE=> array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => 'The core type that gets passed along to servers when they need to start. Servers use it to determine what kind of behavior to exhibit',
        ),
        self::QUEUE => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => 'The queue name that gets passed along to servers when they need to start. Servers start listening to this queue',
        ),
        self::QUEUE_KEEPALIVE_SERVER => array(
          self::VAR_TYPE => mediamosa_sdk::TYPE_STRING,
          self::VAR_DESCRIPTION => 'The queue name that we send to the servers. Servers will listen for keepalive message on this queue name',
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
    $core_type = unserialize($this->get_param_value(self::CORE_TYPE));


    //The keepalive queue on the guardian side for the specific core type for which this guardian has been triggered. The guardian class checks this queue for keepalive responses from the servers it watches.
    $queue_keepalive_server_guardian = $queue_keepalive_server . '_guardian';
    $this->queue_to_peek = $queue_keepalive_server_guardian;

    // Send a keepalive message to each server.
    $msg = [
      mediamosa_messenger_scheduler::MSG_ID => mediamosa_job::mediamosa_messenger_get_unique_msg_id(),
      mediamosa_messenger_scheduler::MSG_STATUS => mediamosa_messenger_scheduler::SERVER_STATUS_PENDING
    ];
    $this->rmq_open_connection();
    $this->rmq_send($msg, $queue_keepalive_server);
    $this->rmq_close_connection();
    sleep(3);
    $this->mediamosa_messenger_cycle($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type);

    $mediamosa->add_item(
      array(
        'server_type' => $core_type,
      )
    );
  }


  /**
   * Determines the right action to take for the server this guardian is watching.
   *
   * @param string $queue
   * If a server needs to start, the new server needs to know what queue to start listening to.
   * @param string $queue_keepalive_server
   * If a server needs to start, the new server needs to know what keepalive queue to start listening to.
   * @param string $queue_keepalive_server_guardian
   * If a server needs to start, the new server needs to know what keepalive queue on the guardian side to send messages to.
   * Also necessary to peek into the queue of this guardian.
   * @param array $core_type
   * If a server needs to start, the new server needs to know what kind of core_type it is.
   */
  protected function mediamosa_messenger_cycle($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type) {
    $drush_time_last_update = $queue_keepalive_server . '_time_last_update';
    $time_now = microtime(TRUE);

    // If the variable has not been set yet, scheduler has never started before.
    if (is_null($this->variable_get($drush_time_last_update))){
      $this->variable_set($drush_time_last_update, $time_now);
      $this->mediamosa_messenger_start_core($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type);

    } else {
      // See if the server for which this instance guardian was started has sent a keepalive response. If so, update time.

      do {
        $msg = $this->rmq_peek();
        if (!empty($msg)) {
          mediamosa_debug::log('Start update for:' . $queue_keepalive_server, array(), 'T - 23');
          $this->mediamosa_messenger_update_last_response_time($drush_time_last_update, $msg[mediamosa_messenger_scheduler::MSG_TIMESTAMP]);
        }
      } while (!empty($msg));

      $time_last_update = variable_get($drush_time_last_update);
      $max_wait_time = $this->variable_get('max_wait_time', 300);

      // If the last response of the server was longer ago than the max_wait_time, the server is determined to have crashed.
      if (($time_last_update + $max_wait_time) < ($time_now)) { // Scheduler has not responded in the last 5 minutes
        $this->mediamosa_messenger_update_last_response_time($drush_time_last_update, $time_now);
        $this->mediamosa_messenger_start_core($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type);
      }
    }
  }

  /**
   * Take a brief look at the queue the guardian receives server keepalive responses in.
   * If a response is present with a timestamp, update the drush keepalive variable of that server.
   *
   * @param string $queue_keepalive_server
   * The keepalive server of the server this guardian is watching. Necessary only for a debug statement.
   * @param string $queue_keepalive_server_guardian
   * The queue on the guardian side that accepts keepalive message back. Belongs to one specific server.
   * @param string $drush_time_last_update
   * The new timestamp retrieved from a new server response message.
   */
  protected function rmq_peek() {
    $this->rmq_open_connection();

    $this->channel->exchange_declare(mediamosa_messenger_jobcore::EXCHANGE_JOBS, 'direct', false, false, false);
    $this->channel->queue_declare($this->queue_to_peek, false, true, false, false);
    $this->channel->queue_bind($this->queue_to_peek, mediamosa_messenger_jobcore::EXCHANGE_JOBS, $this->queue_to_peek);

    $msg = array();
    // Get the first message from this guardians queue to jumpstart the while loop.
    $rmq_msg = $this->channel->basic_get($this->queue_to_peek);

    if (!is_null($rmq_msg)) {
      // Have to ack in a different way this time.
      $this->channel->basic_ack($rmq_msg->delivery_info['delivery_tag']);
      $msg = unserialize($rmq_msg->body);
    }

    $this->rmq_close_connection();

    return $msg;
  }

  protected function mediamosa_messenger_update_last_response_time($drush_time_last_update, $server_response_time) {
    $time_last_update = variable_get($drush_time_last_update);

    // if The timestamp in the message is more recent, update time_last_update.
    if ($time_last_update < $server_response_time) {
      $this->variable_set($drush_time_last_update, $server_response_time);
      mediamosa_debug::log('Updated time to:' . $server_response_time, array(), 'T - 23');
    }
  }

  /**
   * Start a core through a REST call.
   *
   * @param string $queue
   * The new server needs to know what queue to start listening to.
   * @param string $queue_keepalive_server
   * The new server needs to know what keepalive queue to start listening to.
   * @param string $queue_keepalive_server_guardian
   * The new server needs to know what keepalive queue on the guardian side to send messages to.
   * @param array $core_type
   * The new server needs to know what kind of core_type it is.
   */
  protected function mediamosa_messenger_start_core($queue, $queue_keepalive_server, $queue_keepalive_server_guardian, $core_type) {
    $uri = '';

    if ((count($core_type) == 1) and ($core_type[0] == self::CORE_SCHEDULER)) {
      $uri = '/scheduler/start?';
    } else {
      $uri = '/jobcore/start?';
    }

    $query_data = [
      mediamosa_messenger_jobcore::QUEUE => $queue,
      mediamosa_messenger_jobcore::QUEUE_KEEPALIVE_SERVER => $queue_keepalive_server, // The scheduler needs this to know what to listen to for keepalive message
      mediamosa_messenger_jobcore::QUEUE_KEEPALIVE_SERVER_GUARDIAN => $queue_keepalive_server_guardian, // The scheduler needs this to know where to send keepalive messages back to
      mediamosa_messenger_jobcore::CORE_TYPE => serialize($core_type),
    ];

    $this->mediamosa_messenger_call($uri, $query_data);
  }

  /**
   * Do a standard internal head call to the $uri.
   *
   * @param string $uri
   * The REST call uri.
   *
   * @param array $query_data
   * Extra data sent with the REST call.
   */
  protected function mediamosa_messenger_call($uri, array $query_data = array()) {
    $mediamosa_jobscheduler_uri = variable_get('mediamosa_jobscheduler_uri', NULL);

    if (isset($mediamosa_jobscheduler_uri)) {
      $uri = $uri . http_build_query($query_data);
      $url = mediamosa_http::uri2url($mediamosa_jobscheduler_uri) . $uri;

      mediamosa_http::do_head_internal_call($url);
    } else {
      mediamosa_job_scheduler::log('Jobscheduler URL not set, please setup jobscheduler server in the @link.', array('@link' => l(t('MediaMosa configuration'), 'admin/mediamosa/config/global')), WATCHDOG_ALERT, 'job_cron');
    }
  }

  /**
   * Wrapper around variable_get().
   */
  protected function variable_get($name, $default = NULL) {
    return variable_get($name, $default);
  }

  /**
   * Wrapper around variable_set().
   */
  protected function variable_set($name, $value) {
    variable_set($name, $value);
  }

  protected function rmq_listen($queue) {}
}
