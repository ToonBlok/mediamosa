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

  $items['mediamosa-messenger'] = array(
    'description' => dt('Returns the current MediaMosa version.'),
    //'aliases' => array('mmv'),
    'examples' => array(
      'mediamosa-version' => '',
    ),
  );

  return $items;
}

/**
 * Callback function for mm-version command.
 */
function drush_mediamosa_messenger() {
  //drush_print('Hello world');
  $args = func_get_args();
  $type = $args[0];
  $jobcore = new jobcore($type);
}
