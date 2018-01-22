<?php

class mediamosa_messenger_helper_transcode
{
  /**
   * Start a transcode job
   *
   * @param array $job
   * The job to start.
   */
  function start($job){
    $execution_string = $this->generate_transcode_exec($job[mediamosa_job_db::ID], $job[mediamosa_job_db::MEDIAFILE_ID], $job['transcode_parameters']);
    $exec_output = mediamosa_io::exec($execution_string);

    mediamosa_job_server::log_mediafile($job['mediafile_id'], 'Job @job_type (Job ID: @job_id) returned output: @output<br /><br />@link',
      array(
        '@job_type' => $job['job_type'],
        '@job_id' => $job['job_id'],
        '@output' => implode("\n", $exec_output),
        '@link' => $job['asset_link'],
      )
    );
  }

  /**
   * Generate the executable string for this job
   *
   * @param string $job_id
   * @param int $mediafile_id
   * @param array $job_parameters
   * We need the job parameters to start a job.
   *
   * @return string
   * Executable string.
   */
  function generate_transcode_exec($job_id, $mediafile_id, $job_parameters) {
    $tool = $job_parameters[mediamosa_job_server_transcode_db::TOOL];
    $file_extension = $job_parameters[mediamosa_job_server_transcode_db::FILE_EXTENSION];
    $command = $job_parameters[mediamosa_job_server_transcode_db::COMMAND];

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

    $status_file = mediamosa_storage::get_realpath_status_file($job_id);

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
        //$varA = mediamosa_storage::get_local_mediafile_path($mediafile_id);
//        $execution_string = sprintf('%s %s %s %s %s %s %s > /dev/null &', mediamosa_settings::lua_transcode_script(), $mediafile_path, mediamosa_storage::get_realpath_temporary(), $mediafile_id, $job_id, $file_extension, $parameter_string);
        $execution_string = sprintf('%s %s %s %s %s %s %s > /dev/null &', mediamosa_settings::lua_transcode_script(), mediamosa_storage::get_local_mediafile_path($mediafile_id), mediamosa_storage::get_realpath_temporary(), $mediafile_id, $job_id, $file_extension, $parameter_string);
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
            'job_id' => $job_id,
            // ID of mediafile to transcode.
            'mediafile_id' => $mediafile_id,
            // File extension of dest.
            'file_extension' => $file_extension,
            // Parameter string for cmd.
            'parameter_string' => $parameter_string,
            // the data dir in sannas (extra).
            'path_mount_point_data' => mediamosa_storage::get_local_mediafile_path($mediafile_id),
//            'path_mount_point_data' => $mediafile_path,
            // Path to the transcode file.
            'location_dest_file' => mediamosa_storage::get_realpath_temporary_file($job_id),
            // Location of source mediafile.
            'location_source_file' => mediamosa_storage::get_realpath_mediafile($mediafile_id),
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
  }

}









