<?php

/**
 * URI: /server/parsequeue
 * Method: GET
 */
class server extends mediamosa_rest_call
{
  const SERVER_TYPE = 'server_type';
  // ------------------------------------------------------------------ Methods.
  /**
   * Implements get_var_setup().
   */
  public function get_var_setup() {
    $var_setup = array();

    $var_setup = array(
      self::VARS => array(
        self::SERVER_TYPE => array(
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
  public function do_call() {
    $mediamosa = mediamosa::get();

    $server_type = $this->get_param_value(self::SERVER_TYPE);
    mediamosa_debug::log('Started ' . $server_type, array(), 'T - DEBUG');

    // Geef array terug van response
    // Geef steeds een regel terug die je in xml output terug krijgt
    $mediamosa->add_item(
      array(
        'version' => mediamosa_version::get_current_version_str(FALSE),
        'server_type' => $server_type,
      )
    );
  }

}
