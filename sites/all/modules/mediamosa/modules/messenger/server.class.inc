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

//class server {

//  function __construct() {
//    include_once('/home/tony/projects/mediamosa/sites/all/modules/mediamosa/modules/messenger/jobcore.class.inc');
////    $ch = curl_init();
////    curl_setopt($ch, CURLOPT_URL, 'https://email.us-east-1.amazonaws.com');
////    curl_exec($ch);
////    $ssl_result = !(curl_getinfo($ch, CURLINFO_SSL_VERIFYRESULT) === 0);
////    curl_close($ch);
//
//  }

//
//  protected function get_var_setup_default(array $var_setup, $app_id_required = TRUE) {
//    $var_setup[mediamosa_rest_call::VARS][mediamosa_rest_call::APP_ID] = array(
//      // Once set, dont change.
//      self::VAR_IS_READ_ONLY => self::VAR_IS_READ_ONLY_YES,
//      self::VAR_TYPE => mediamosa_sdk::TYPE_APP_ID,
//      self::VAR_IS_REQUIRED => ($app_id_required ? ($this->is_internal() ? self::VAR_IS_REQUIRED_NO : self::VAR_IS_REQUIRED_YES) : self::VAR_IS_REQUIRED_NO),
//      self::VAR_IS_ARRAY => self::VAR_IS_ARRAY_YES,
//      self::VAR_DEFAULT_VALUE => 0,
//      self::VAR_DESCRIPTION => mediamosa::t('The application ID.'),
//    );
//
//    // @todo: check every rest call to see if is_app_admin is used and
//    // specified in var_setup.
//    if (!isset($var_setup[mediamosa_rest_call::VARS][mediamosa_rest_call::IS_APP_ADMIN])) {
//      $var_setup[mediamosa_rest_call::VARS][mediamosa_rest_call::IS_APP_ADMIN] = array(
//        self::VAR_TYPE => mediamosa_sdk::TYPE_BOOL,
//        self::VAR_IS_REQUIRED => self::VAR_IS_REQUIRED_NO,
//        self::VAR_DESCRIPTION => mediamosa::t('NOT USED.'),
//        self::VAR_DEFAULT_VALUE => 'false',
//      );
//    }
//
//    $var_setup[mediamosa_rest_call::VARS][mediamosa_rest_call::MEDIAMOSA_VERSION] = array(
//      self::VAR_TYPE => mediamosa_sdk::TYPE_MEDIAMOSA_VERSION,
//      self::VAR_IS_REQUIRED => self::VAR_IS_REQUIRED_NO,
//      self::VAR_DESCRIPTION => mediamosa::t("Provide the version to get the REST call returned in the format of the specified MediaMosa version. Keep empty or skip to retrieve in current (latest) version. Provide '2' to retrieve the REST call in 2.x format. Or be more specific by providing up to 3 digits; '2.3.0'. This setting will override any setting used by client application or the default setting in MediaMosa."),
//      self::VAR_DEFAULT_VALUE => '',
//    );
//
//    // Add default parameter descriptions
//    foreach ($var_setup[mediamosa_rest_call::VARS] as $key => $value) {
//      if (!isset($value[self::VAR_DESCRIPTION])) {
//        if (isset($this->mediamosa_parameter_docs[$key])) {
//          $var_setup[mediamosa_rest_call::VARS][$key][self::VAR_DESCRIPTION] = $this->mediamosa_parameter_docs[$key];
//        }
//      }
//    }
//
//    return $var_setup;
//  }


//}
