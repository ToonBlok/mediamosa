<?php

class analyze {

  // modelled after mediamosa_job_server.class.inc line 440
  function start($job){
    mediamosa_debug::log('Entered method: ' . __METHOD__, array(), 'Toon');

    $analyse_result = array();

    $mediafile_path = mediamosa_storage::get_realpath_mediafile($job['mediafile_id']);
    $mime_type = mediamosa_mimetype::get_mime_type($mediafile_path);

    $analyse_result[mediamosa_asset_mediafile_metadata::MIME_TYPE] = array(
      'type' => mediamosa_asset_mediafile_metadata_property_db::TYPE_CHAR,
      'value' => $mime_type,
    );

    // Based on the mime_type there might be a tool that can analyse.
    // Call the mediamosa_tool_can_analyse hook.
    // Toon: There are 4 modules apparently, and there is one that triggers the if in this statement
    foreach (module_implements('mediamosa_tool_can_analyse') as $module) {
      if (module_invoke($module, 'mediamosa_tool_can_analyse', $mime_type)) {
        $analyse_result += module_invoke($module, 'mediamosa_tool_analyse', $job['mediafile_id']);
      }
    }


    // Make an informative log entry.
    $analyse_output = array();
    foreach ($analyse_result as $key => $value) {
      $analyse_output[] = $key . ' [' . $value['type'] . '] ' . $value['value'];
    }

    $link_asset = self::get_asset_link($job['job_id']);

    mediamosa_job_scheduler::log_mediafile
    (
      $job['mediafile_id'], 'TOON IT WORKS Job analyse (Job ID: @job_id) returned output: @output<br /><br />@link',
      array(
        '@job_id' => $job['job_id'],
        '@output' => implode("\n", $analyse_output) . "\n",
        '@link' => $link_asset,
      )
    );

    if (!empty($analyse_result)) {
      mediamosa_debug::log('FINISHED!', array(), 'Toon');
      $this->set_job_status($job['job_id'], $job['mediafile_id'], mediamosa_job_server_db::JOB_STATUS_FINISHED, '1.000');
    }
    else {
      mediamosa_debug::log('FAILED!', array(), 'Toon');
      $this->set_job_status($job['job_id'], $job['mediafile_id'], mediamosa_job_server_db::JOB_STATUS_FAILED, '1.000', 'Empty result, analyse failed.');
    }

    // Generate event analyse finished.
    //mediamosa::rules_invoke_event('mediamosa_event_analyse_finished', $job['mediafile_id']);

  }

  /**
   * Create a link to the parent asset belonging to a given job id.
   *
   * @param int $job_id
   *
   * @return string
   *  Link to an asset.
   */
  public static function get_asset_link($job_id) {

    // Get the job.
    $jobserver_job = mediamosa_job::get($job_id);

    // Get asset ID from job.
    $asset_id = $jobserver_job[mediamosa_job_db::ASSET_ID];

    // Return link.
    return l(mediamosa::t('Go to asset @asset_id', array('@asset_id' => $asset_id)), mediamosa_settings::get_url_asset($asset_id));
  }

  // Did not protect against mysql injection yet, necessary?
  function set_job_status($job_id, $mediafile_id, $job_status, $progress, $error_description = '', $error_description_args = array()) {

    // Set args in description.
    if (!empty($error_description_args)) {
      $error_description = strtr($error_description, $error_description_args);
    }

    $fields = array(
      mediamosa_job_db::JOB_STATUS => $job_status,
      mediamosa_job_db::PROGRESS => is_null($progress) ? '0.000' : $progress
    );

    // Invalidate technical metadata if analyse fails.
    if ($job_status == mediamosa_job_db::JOB_STATUS_FAILED) {
      mediamosa_asset_mediafile_metadata::delete_by_mediafileid($mediafile_id);
    }

    // Because of there being no break, all cases will be executed. This code was chosen over a large if statement
    switch ($job_status) {
      case mediamosa_job_server_db::JOB_STATUS_FINISHED:
      case mediamosa_job_server_db::JOB_STATUS_FAILED:
      case mediamosa_job_server_db::JOB_STATUS_CANCELLED:
        $fields[mediamosa_job_server_db::FINISHED] = mediamosa_datetime::utc_current_timestamp_now(TRUE); // finished = curent time
        break;
    }

    mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
      ->fields($fields)
      ->condition(mediamosa_job_db::ID, (string) $job_id)
      ->execute();
  }
}
