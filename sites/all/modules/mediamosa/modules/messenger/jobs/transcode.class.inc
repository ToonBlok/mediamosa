<?php

class transcode
{
  function __construct()
  {
    $this->debug = false;
  }

  function start($job){
    $job_id = $job['job_id'];
    $job_type = $job['job_type'];
    $mediafile_id = $job['mediafile_id'];

    mediamosa_debug::log('Transcode started.', array(), 'Toon - TRANSCODE');
    $execution_string = $this->get_transcode_exec($job_id, $mediafile_id, unserialize($job['transcode_parameters']));

    $exec_output = mediamosa_io::exec($execution_string);
    mediamosa_debug::log_export($exec_output, WATCHDOG_NOTICE, 'Toon - TRANSCODE' );

    $link_asset = mediamosa_job_server::get_asset_link($job_id); // THIS GOES INTO DB!!!!!!!!!!!!!!!!!!
    mediamosa_job_server::log_mediafile($mediafile_id, 'Job @job_type (Job ID: @job_id) returned output: @output<br /><br />@link',
      array(
        '@job_type' => $job_type,
        '@job_id' => $job_id,
        '@output' => implode("\n", $exec_output),
        '@link' => $link_asset,
      )
    );
  }

  /**
   * Generate the string that is used for the vpx_transcode script.
   *
   * @param string $jobserver_job_id
   * @param string $mediafile_id
   */
  function get_transcode_exec($jobserver_job_id, $mediafile_id, $job_parameters) {

    // Get it.
//    $job_server_transcode = mediamosa_job_server_transcode::get($jobserver_job_id);
//    mediamosa_debug::log_export($job_server_transcode, WATCHDOG_NOTICE, 'Toon - TRANSCODE' );

//    if (empty($job_server_transcode)) {
//      mediamosa_job_server::log_mediafile($mediafile_id, 'Transcode job not found, jobserver_id: @jobserver_id', array('@jobserver_id' => $jobserver_job_id));
//      return '';
//    }

    $tool = $job_parameters[mediamosa_job_server_transcode_db::TOOL];
    $file_extension = $job_parameters[mediamosa_job_server_transcode_db::FILE_EXTENSION];
    $command = $job_parameters[mediamosa_job_server_transcode_db::COMMAND];

    // Get the mediafile.
    $mediafile = mediamosa_asset_mediafile::must_exists($mediafile_id);

    // Create parameter string from the command.
    $commands = mediamosa_transcode_profile::commandToArray($command);

    // Build the parameter string.
    $parameters = array();
    foreach ($commands as $name => $value) {
      if ($value == mediamosa_tool_params_db::ALLOWED_VALUE_FOR_SWITCH) {
        $value = '';
      }
      $parameters[] = $name . ' ' . $value;
    }

    // Rebuild.
    $parameter_string = implode(' ', $parameters);

    // ### OLD
    // The name of the status file is '<job_id>.status'.
//    $job_server = mediamosa_job_server::get($jobserver_job_id);
//    $status_file = mediamosa_storage::get_realpath_status_file($job_server[mediamosa_job_server_db::JOB_ID]);
    // ### OLD

    // ### NEW
    $status_file = mediamosa_storage::get_realpath_status_file($jobserver_job_id);
    // ### NEW

    // Empty.
    $execution_string = '';

    // Combine based on selection.
    switch ($tool) {
      // TODO: move to tool.
      case 'ffmpeg':
        $parameter_string = trim($parameter_string);
        if (!empty($parameter_string)) {
          $parameter_string = escapeshellarg($parameter_string);
        }

        // ORIGINAL
        $execution_string = sprintf('%s %s %s %s %s %s %s > /dev/null &', mediamosa_settings::lua_transcode_script(), mediamosa_storage::get_local_mediafile_path($mediafile), mediamosa_storage::get_realpath_temporary(), $mediafile_id, $jobserver_job_id, $file_extension, $parameter_string);
        // MODIFIED
//        $execution_string = sprintf('%s %s %s %s %s %s %s', mediamosa_settings::lua_transcode_script(), mediamosa_storage::get_local_mediafile_path($mediafile), mediamosa_storage::get_realpath_temporary(), $mediafile_id, $jobserver_job_id, $file_extension, $parameter_string);
        break;

      default:
        // Now check if for this tool the hook exists.
        $class_name = 'mediamosa_tool_' . $tool;

        // FIXME:
        // This code here is first attempt to rewrite the jobs module in more
        // flexible one. In future ::generate_transcode() is called directly and
        // jobs will no longer worry about exec strings.
        // Will start moving all ffmpeg code out of core into ffmpeg tool very
        // soon.

        // Now see if transcode function is found.
        if (class_exists($class_name) && method_exists($class_name, 'get_transcode_exec')) {
          $args = array(
            // Job ID.
            'job_id' => $jobserver_job_id,
            // ID of mediafile to transcode.
            'mediafile_id' => $mediafile_id,
            // File extension of dest.
            'file_extension' => $file_extension,
            // Parameter string for cmd.
            'parameter_string' => $parameter_string,
            // the data dir in sannas (extra).
            'path_mount_point_data' => mediamosa_storage::get_local_mediafile_path($mediafile),
            // Path to the transcode file.
            'location_dest_file' => mediamosa_storage::get_realpath_temporary_file($job_server[mediamosa_job_server_db::JOB_ID]),
            // Location of source mediafile.
            'location_source_file' => mediamosa_storage::get_realpath_mediafile($mediafile),
            // Location of the status_file.
            'status_file' => $status_file,
          );

          // PHP 5.2.3 or higher.
          $execution_string = call_user_func($class_name . '::get_transcode_exec', $args);
        }
    }

    // Unknown.
    if (empty($execution_string)) {
      return strtr('{ echo "Status: error"; echo "Errors: Error"; } > @status', array(
        '@status' => $status_file,
      ));

      // One tool has been found. Process started. Return now.
    }


    return $execution_string;
  } // End get_transcode_exec()

  // ################################################ REMOVE LATER
  /**
   * Update the job.
   *
   * Updates running jobs.
   *
   * 1. Get possible status file and parse it.
   * 2. When transcode or still job is done, files will be moved to the
   *    correct location.
   */
  function update($job) {
    $job_type = $job[mediamosa_job_server_db::JOB_TYPE];
    $job_id = $job[mediamosa_job_server_db::JOB_ID];
    $mediafile_id_src = $job[mediamosa_job_db::MEDIAFILE_ID]; // hmmmmmmm
    $status = mediamosa_job_db::JOB_STATUS_INPROGRESS; // Default status

    // Read the contents of the status file in an array.
    $job_status['Status'] = 'aa';
    while ($job_status['Status'] != 'done') { // Is this a good temporary solution????????
      $job_status = mediamosa_job_server::get_status_contents($job_id); // Is this even kept up-to-date?
      mediamosa_debug::log('Job ' . $job_id . ', status: ' . $job_status['Progress'] . ', errors: ' . $job_status['Errors'], array(), 'Toon - TRANSCODE');
    }

    // No status file found, we just have to wait.
    if (empty($job_status['Status'])) {
      mediamosa_job_server::log_debug_mediafile($mediafile_id_src, "No status file found with @statusfile for job @job_id, maybe next run.", array('@statusfile' => mediamosa_storage::get_realpath_status_file($job_id), '@job_id' => $job_id));
      return;
    }

    if ($job_status['Status'] == 'done' && $job_status['Errors'] == 'none') {
      mediamosa_debug::log('Transcode done and errors are none.', array(), 'Toon - TRANSCODE');
      // Status to finished.
      $status = mediamosa_job_db::JOB_STATUS_FINISHED;

      // Store the transcode.
      $this->store_new_mediafile($job);

      // Log it.
//          mediamosa_job_server::log_mediafile($mediafile_id_src, 'End job @job_type, Job ID: @job_id, status: @status', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status));

      // Set job status.
      if ($this->debug == false) {
        $this->set_job_status($job, $status, $job_status['Progress']);
      }

      // Generate event transcode finished.
      mediamosa::rules_invoke_event('mediamosa_event_transcode_finished', $mediafile_id_src);
//      mediamosa_debug::log('MY CODE: Invoked rules_invoke_event with mediafile_id_src: ' . $mediafile_id_src, array(), 'Toon - TRANSCODE');
    }
    elseif ($job_status['Status'] == 'error' && (empty($job_status['Errors']) || $job_status['Errors'] != 'none')) {
      $status = mediamosa_job_server_db::JOB_STATUS_FAILED;
      $link_asset = mediamosa_job_server::get_asset_link($job_id);

      mediamosa_job_server::log_mediafile($mediafile_id_src, "End @job_type job, Job ID @job_id, with status: @status<br /><br />@link", array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status, '@link' => $link_asset));
      mediamosa_job_server::log_mediafile($mediafile_id_src, "Info @job_type job, Job ID @job_id, status file '@statusfile'", array('@job_type' => $job_type, '@job_id' => $job_id, '@statusfile' => mediamosa_job_server::get_status_contents($job_id, TRUE)));

      // Set status to failed.
      if ($this->debug == false) {
        $this->set_job_status($job, $status, $job_status['Progress'], isset($job_status["ffmpeg-output"]) ? ($job_status["Errors"] != "" ? $job_status["Errors"] . "-\n" : '') . $job_status["ffmpeg-output"] : $job_status["Errors"]);
      }

      // Generate event transcode failed.
      mediamosa::rules_invoke_event('mediamosa_event_transcode_failed', $mediafile_id_src);
    }
    else {
      // Set job status.
      if ($this->debug == false) {
        $this->set_job_status($job, $status, $job_status['Progress']);
      }
    }
  }



  function set_job_status($job, $job_status, $progress, $error_description = '', $error_description_args = array()) {
    $job_id = $job['job_id'];
    $mediafile_id = $job['mediafile_id'];

    // Set args in description.
    if (!empty($error_description_args)) {
      $error_description = strtr($error_description, $error_description_args);
    }

    $fields = array(
      mediamosa_job_db::JOB_STATUS => $job_status,
      mediamosa_job_db::PROGRESS => is_null($progress) ? '0.000' : $progress,
    );

    switch ($job_status) {
      case mediamosa_job_db::JOB_STATUS_FINISHED:
      case mediamosa_job_db::JOB_STATUS_FAILED:
      case mediamosa_job_db::JOB_STATUS_CANCELLED:
        $fields[mediamosa_job_db::FINISHED] = mediamosa_datetime::utc_current_timestamp_now(TRUE);
        break;
    }

    // Check if its started.
//    $jobserver_job = self::get_with_jobid($job_id);
//    if (!$jobserver_job) {
//      self::log('Fatal: trying to update job with ID; @job_id', array('@job_id' => $job_id));
//      assert(0);
//      return;
//    }

    // Invalidate technical metadata if analyse fails.
    if ($job_status == mediamosa_job_server_db::JOB_STATUS_FAILED && $job[mediamosa_job_db::JOB_TYPE] == mediamosa_job_db::JOB_TYPE_ANALYSE) {
      // Delete metadata, its no longer valid when analyse fails.
      mediamosa_asset_mediafile_metadata::delete_by_mediafileid($mediafile_id);
    }

    // Set status.
    if ($job[mediamosa_job_db::JOB_STATUS] == mediamosa_job_db::JOB_STATUS_WAITING && $job_status == mediamosa_job_db::JOB_STATUS_INPROGRESS) {
      $fields[mediamosa_job_db::STARTED] = mediamosa_datetime::utc_current_timestamp_now(TRUE);
    }

    if (!empty($error_description)) {
      $fields[mediamosa_job_db::ERROR_DESCRIPTION] = $error_description;
    }

    mediamosa_debug::log('Set job ' . $job_id . ' status to ' . $job_status, array(), 'Toon - TRANSCODE');
    // Update.
    mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
      ->fields($fields)
      ->condition(mediamosa_job_db::ID, $job_id)
      ->execute();
  }

  function store_new_mediafile($job) {
    $job_id = $job['job_id'];
    $transcode_parameters = unserialize($job['transcode_parameters']);
//    $job = mediamosa_job::get($job_id, array(mediamosa_job_db::APP_ID, mediamosa_job_db::HINT)); // Get the job.
    $app_id = $job[mediamosa_job_db::APP_ID];
    $mediafile_id = mediamosa_db::uuid($job_id); // Generate new mediafile ID.
    $file_extension = $transcode_parameters[mediamosa_job_server_transcode_db::FILE_EXTENSION];
    mediamosa_debug::log('File extension was determined as: ' . $file_extension, array(), 'Toon - TRANSCODE');

    // Cleared:
    // mediafile_id
    //
    // Get the filenames.
    $file_status_uri = mediamosa_storage::get_uri_status_file($job_id);
    $file_transcode_uri = mediamosa_storage::get_uri_temporary_file($job_id . '.' . $file_extension);
    $file_destination_uri = mediamosa_storage::create_local_mediafile_uri($app_id, $mediafile_id);

    // Rename transcoded file to new dest.
    mediamosa_io::rename($file_transcode_uri, $file_destination_uri);

    // Now remove the status file.
    mediamosa_io::unlink($file_status_uri);

    $fields = array(
      mediamosa_job_server_db::MEDIAFILE_DEST => $mediafile_id,
    );

    // Enrich with update date.
    $fields = mediamosa_db::db_update_enrich($fields);

    // Update the filename in mediafile_dest.
    mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
      ->fields($fields)
      ->condition(mediamosa_job_db::ID, $job_id)
      ->execute();

    // Log it.
//    mediamosa_job_server::log_mediafile($job_server[mediamosa_job_server_db::MEDIAFILE_ID_SRC], "Job with ID @job_id ready, new mediafile stored as '@uri' (@path).", array('@job_id' => $job_id, '@uri' => $file_destination_uri, '@path' => mediamosa_io::realpath($file_destination_uri)));
  }





}









