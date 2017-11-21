<?php
/**
 * @file
 * Drush integration for MediaMosa.
 *
 * Contains common drush functions for MediaMosa.
 */

/**
 * Implements hook_drush_help().
 */
function mediamosa_messenger_drush_help($section) {
  switch ($section) {
    case 'drush:mediamosa-version':
      return dt('Returns the current MediaMosa version.');
  }
}

/**
 * Implements hook_drush_command().
 */
function mediamosa_messenger_drush_command() {
  
  $items = array();

  $items['mm-messenger'] = array(
    'description' => dt('Returns the current MediaMosa version.'),
    'aliases' => array('mm-mess'),
    'examples' => array(
      'mediamosa-version' => '',
    ),
  );

  return $items;
}

/**
 * Callback function for mm-version command.
 */
function drush_mediamosa_messenger_mm_messenger() {
  drush_print('Starting analyse, still and transcode server.');
//  $args = func_get_args();
//  $type = $args[0];
  drush_mediamosa_messenger('/jobcore/parsequeue?core_type=', 'ANALYSE');
  drush_mediamosa_messenger('/jobcore/parsequeue?core_type=', 'STILL');
  drush_mediamosa_messenger('/jobcore/parsequeue?core_type=', 'TRANSCODE');
//  $jobcore = new jobcore($type);
}

function drush_mediamosa_messenger($path, $value = '') {
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
