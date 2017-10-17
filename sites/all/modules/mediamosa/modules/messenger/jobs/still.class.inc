<?php

class still
{

  // Can only handle video/mp4 at the moment due to the specific still exec
  function start($job) {
    $execution_string = $this->get_ffmpeg_generate_still_exec($job);
    mediamosa_debug::log('Execution string received: ' . $execution_string, array(), 'Toon - STILL');
    $exec_output = mediamosa_io::exec($execution_string);


    $link_asset = $this->get_asset_link($job['job_id']);

    mediamosa_job_server::log_mediafile($job['mediafile_id'], 'Job @job_type (Job ID: @job_id) returned output: @output<br /><br />@link',
      array(
        '@job_type' => $job['job_type'],
        '@job_id' => $job['job_id'],
        '@output' => implode("\n", $exec_output),
        '@link' => $link_asset,
      )
    );
    // Upload to DB

    // If the still job is completed, add the still to db.
    // We serialize it, because of the multiple stills.
    //self::add_still_to_db((string) $job['job_id'], unserialize((string) $response_data->items->item[$i]->mediafile_dest));

  }

  function update($job) {
    $this->update_job($job);
  }

  function update_job($job){
    //$job = unserialize($job);
    $job_type = $job[mediamosa_job_db::JOB_TYPE];
    $job_id = $job[mediamosa_job_db::ID];
    $mediafile_id = $job[mediamosa_job_db::MEDIAFILE_ID];

    // Toon: Als het een transcode of still job is, ga dan uit de switch statement (en ontwijk de return statement).
    switch ($job_type) {
      case mediamosa_job_server_db::JOB_TYPE_TRANSCODE:
      case mediamosa_job_server_db::JOB_TYPE_STILL:
        break;

      default:
        return;
    }

    // Read the contents of the status file in an array.
    $job_status = $this->get_status_contents($job_id);

    // Default status.
    $status = mediamosa_job_server_db::JOB_STATUS_INPROGRESS;

    // Scene still filename.
    $file_scene = mediamosa_storage::get_uri_scene_file($job_id);

    if (!mediamosa_io::file_exists($file_scene) && empty($job_status)) {
      // No status file found, we just have to wait.
      mediamosa_job_server::log_debug_mediafile($job['mediafile_id'], 'No status file found with name @name for job @job_id, maybe next run.', array('@name' => mediamosa_storage::get_realpath_status_file($job_id), '@job_id' => $job_id));
      return;
    }

    // Set defaults, to fix some possible notices.
    $job_status += array(
      'Status' => '',
      'Errors' => 'none',
      'Progress' => '0.000',
    );

    if (mediamosa_io::file_exists($file_scene) || ($job_status['Status'] == 'done' && $job_status['Errors'] == 'none')) {
      $status = $this->store_new_still($job);

      //mediamosa_debug::log('END RESULT OF STORE STILL: ' . $status, array(), 'Toon - STILL');

      if ($status == mediamosa_job_server_db::JOB_STATUS_INPROGRESS) {
        mediamosa_job_server::log_debug_mediafile($mediafile_id, 'Running @job_type job (storing file busy), Job ID @job_id, with status: @status', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status));
      }
      else {
        mediamosa_job_server::log_debug_mediafile($mediafile_id, 'End @job_type job, Job ID @job_id, with status: @status', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status));
      }
    }
    elseif ($job_status['Status'] == 'error' && $job_status['Errors'] != 'none') {
      $status = mediamosa_job_server_db::JOB_STATUS_FAILED;
      $link_asset = $this->get_asset_link($job_id);
      mediamosa_job_server::log_debug_mediafile($mediafile_id, 'End @job_type job, Job ID @job_id, with status: @status<br /><br />@link', array('@job_type' => $job_type, '@job_id' => $job_id, '@status' => $status, '@link' => $link_asset));
      mediamosa_job_server::log_debug_high_mediafile($mediafile_id, "Info @job_type job, Job ID @job_id, status file '@statusfile'", array('@job_type' => $job_type, '@job_id' => $job_id, '@statusfile' => $this->get_status_contents($job_id, TRUE)));
    }

    // Update the status.
    if (!mediamosa_io::file_exists($file_scene) && $job_status['Errors'] != 'none') {
      // Might be because there is no status file, dont bother to update.
      if (isset($job_status['Errors'])) {
        $this->set_job_status($job, $status, $job_status['Progress'], $job_status['Errors']);
      }
    }
    else {
      $this->set_job_status($job, $status, $job_status['Progress']);
    }
  }

  /**
   * Set the job status on a jobserver table.
   *
   * @param $job_id
   * @param $job_status
   * @param $progress
   * @param $error_description
   */
  public static function set_job_status($job, $job_status, $progress, $error_description = '', $error_description_args = array()) {
    $job_id = $job['job_id'];
    $job_type = $job['job_type'];
    $current_job_status = $job['job_status'];
    $mediafile_id = $job['mediafile_id'];

    // jobserver_job is info about the current job, which is $job in my case

    // Set args in description.
    if (!empty($error_description_args)) {
      $error_description = strtr($error_description, $error_description_args);
    }

    $fields = array(
      mediamosa_job_db::JOB_STATUS => $job_status,
      mediamosa_job_db::PROGRESS => is_null($progress) ? '0.000' : $progress,
    );

    switch ($job_status) {
      case mediamosa_job_server_db::JOB_STATUS_FINISHED:
      case mediamosa_job_server_db::JOB_STATUS_FAILED:
      case mediamosa_job_server_db::JOB_STATUS_CANCELLED:
        $fields[mediamosa_job_server_db::FINISHED] = mediamosa_datetime::utc_current_timestamp_now(TRUE);
        break;
    }

    // Invalidate technical metadata if analyse fails.
    if ($job_status == mediamosa_job_server_db::JOB_STATUS_FAILED && $job_type == mediamosa_job_server_db::JOB_TYPE_ANALYSE) {
      //$mediafile_id = $mediafile_id; # Toon: It asked for mediafile_id_src, I gave it mediafile_id. May be wrong

      // Delete metadata, its no longer valid when analyse fails.
      mediamosa_asset_mediafile_metadata::delete_by_mediafileid($mediafile_id);
    }

    // Set status.
    if ($current_job_status == mediamosa_job_server_db::JOB_STATUS_WAITING && $job_status == mediamosa_job_server_db::JOB_STATUS_INPROGRESS) {
      $fields[mediamosa_job_server_db::STARTED] = mediamosa_datetime::utc_current_timestamp_now(TRUE);
    }

    if (!empty($error_description)) {
      $fields[mediamosa_job_server_db::ERROR_DESCRIPTION] = $error_description;
    }

    // Update.
    mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
      ->fields($fields)
      ->condition(mediamosa_job_db::ID, $job_id)
      ->execute();
  }

  function get_ffmpeg_generate_still_exec($job) {
    $mediafile_id_source = $job['mediafile_id'];
    $execution_string = '';

    //$job_parameters = $this->get_job_parameters($job['job_id'], $job['mediafile_id']);
    // Wait with replacing until stills work, that way you can spot mistakes right away.
    $job_parameters = $job['still_parameters'];

    $job_info = [];
    $job_info['jobserver_job_id'] = 13;
    $job_info['size'] = $job['size'];
    $job_info['frametime'] = $job_parameters['frametime'];
    $job_info['h_padding'] = $job_parameters['h_padding'];
    $job_info['v_padding'] = $job_parameters['v_padding'];
    $job_info['blackstill_check'] = $job_parameters['h_padding'];
    $job_info['still_parameters'] = $job_parameters['still_parameters'];
    $job_info['created'] = 13;
    $job_info['changed'] = 13;
    $job_info['install_id'] = 13;
    $job_info['job_id'] = $job['job_id'];
    $job_info['status'] = 13;
    $job_info['progress'] = 13;
    $job_info['job_type'] = 13;
    $job_info['progress_id'] = 13;
    $job_info['mediafile_src'] = 13;
    $job_info['mediafile_dest'] = 13;
    $job_info['started'] = 13;
    $job_info['finished'] = 13;
    $job_info['error_description'] = 13;

    //mediamosa_debug::log('$job_info: ', array(), 'Toon');
    //mediamosa_debug::log_export($job_info, WATCHDOG_NOTICE, 'Toon' );
    //mediamosa_debug::log('$job_parameters[still_parameters]: ', array(), 'Toon');
    //mediamosa_debug::log_export($job_parameters['still_parameters'], WATCHDOG_NOTICE, 'Toon' );
    //echo '$job_info2: ';
    //var_dump($job_info);

    if ($job_info['job_id']) {
      $frametime = (int) $job_info[mediamosa_job_server_still_db::FRAMETIME];
      $size = $job_info[mediamosa_job_server_still_db::SIZE];
      $h_padding = $job_info[mediamosa_job_server_still_db::H_PADDING];
      $v_padding = $job_info[mediamosa_job_server_still_db::V_PADDING];

      // gives errors: $still_parameters = unserialize($job_info[mediamosa_job_server_still_db::STILL_PARAMETERS]);
      $still_parameters = $job_info[mediamosa_job_server_still_db::STILL_PARAMETERS];
      $fps = (int) $still_parameters['fps'];
      $video_duration = (int) $still_parameters['video_duration'];

      // Video duration check, if still_type = scene.
      if (!$video_duration && $still_parameters['still_type'] == mediamosa_asset_mediafile_metadata::STILL_TYPE_SCENE) {

        // Some videos don't have video durations according to FFMpeg. If we
        // want to create multiple stills, we usually need this information
        // calculating other data.  But in case of scene detection we have a
        // work around, we can put the video duration on a maximum value.
        mediamosa_job_server::log_debug_high_mediafile($mediafile_id_source, t('No video duration, when still_type = scene. Try to get the complete video. video_duration = @video_duration.', array(
          '@video_duration' => print_r($video_duration, TRUE),
        )));
        $video_duration = mediamosa_tool_ffmpeg::VIDEO_DURATION_MAX;
      }

      $start_frame = min(max(0, (int) $still_parameters['start_frame']), max(0, $video_duration - 1));
      if (isset($still_parameters['end_frame']) && (int) $still_parameters['end_frame']) {
        $end_frame = min(max(0, (int) $still_parameters['end_frame']), max(0, $video_duration - 1));
      }
      else {
        $end_frame = max(0, $video_duration - 1);
      }

      // Make sure start and end are in the correct order from low to high.
      $start_frame_cpy = $start_frame;
      $start_frame = min($start_frame, $end_frame);
      $end_frame = max($start_frame_cpy, $end_frame);

      // When video_duration is 0, then fall back to the original behaviour.
      if (!$video_duration || $start_frame == $end_frame) {
        $still_parameters['still_type'] = mediamosa_asset_mediafile_metadata::STILL_TYPE_NONE;
        mediamosa_job_server::log_debug_high_mediafile($mediafile_id_source, t('No video duration or start frame = end frame. Fall back to still_type = NONE. video_duration = @video_duration, start_frame = @start_frame, end_frame = @end_frame.', array(
          '@video_duration' => print_r($video_duration, TRUE),
          '@start_frame' => print_r($start_frame, TRUE),
          '@end_frame' => print_r($end_frame, TRUE),
        )));
      }

      // Make sure still_type is uppercase.
      $still_parameters['still_type'] = mediamosa_unicode::strtoupper($still_parameters['still_type']);

      // Data check.
      switch ($still_parameters['still_type']) {
        case 'NORMAL':
          if (!is_numeric($still_parameters['still_per_mediafile']) || $still_parameters['still_per_mediafile'] < 1) {
            $still_parameters['still_type'] = mediamosa_asset_mediafile_metadata::STILL_TYPE_NONE;
            $error_description = 'Still_per_mediafile parameter is missing. Still creation fall back to still_type = NONE';
            mediamosa_job_server::log_debug_high_mediafile($mediafile_id_source, t('Still_per_mediafile parameter is missing. Still creation fall back to still_type = NONE.'));
          }
          break;

        case 'SECOND':
          if (!is_numeric($still_parameters['still_every_second']) || $still_parameters['still_every_second'] < 0) {
            $still_parameters['still_type'] = mediamosa_asset_mediafile_metadata::STILL_TYPE_NONE;
            $error_description = 'Still_every_second parameter is missing. Still creation fall back to still_type = NONE';
            mediamosa_job_server::log_debug_high_mediafile($mediafile_id_source, t('Still_every_second parameter is missing. Still creation fall back to still_type = NONE.'));
          }
          break;

        case 'SCENE':
          if (!$fps) {
            $still_parameters['still_type'] = mediamosa_asset_mediafile_metadata::STILL_TYPE_NONE;
            $error_description = 'FPS parameter is 0. Still creation fall back to still_type = NONE';
            mediamosa_job_server::log_debug_high_mediafile($mediafile_id_source, t('FPS parameter is 0. Still creation fall back to still_type = NONE.'));
          }
          break;

        default:
          break;
      }

      // Store error_description.
      if (!empty($error_description)) {
        mediamosa_job_server::log_mediafile($mediafile_id_source, $error_description);
        if (isset($job_info['jobserver_job_id'])) {
          mediamosa_db::db_update(mediamosa_job_server_db::TABLE_NAME)
            ->fields(array(
              mediamosa_job_server_db::ERROR_DESCRIPTION => $error_description,
            ))
            ->condition(mediamosa_job_server_db::ID, $job_info['jobserver_job_id'])
            ->execute();
        }
      }

      // Frametime but be between start and end frame. Put it roughly 1 / 3 in
      // length.
      if ($frametime < $start_frame || $frametime > $end_frame) {
        $frametime = (($end_frame - $start_frame) / 3) + $start_frame;
      }

      $duration = $framerate = 1;

      switch (mediamosa_unicode::strtoupper($still_parameters['still_type'])) {

        case 'NONE':
          if ($job_info[mediamosa_job_server_still_db::BLACKSTILL_CHECK] == mediamosa_job_server_still_db::BLACKSTILL_CHECK_FALSE || $video_duration <= 1) {
            break;
          }

          $still_parameters['still_per_mediafile'] = 5;
          $start_frame = max($frametime - mediamosa_tool_ffmpeg::BLACK_STILL_INTERVAL, 0);
          $end_frame = min($frametime + mediamosa_tool_ffmpeg::BLACK_STILL_INTERVAL, $video_duration - 1);
        // Go to the NORMAL now.

        case 'NORMAL':
          $still_per_mediafile = max($still_parameters['still_per_mediafile'], 1);
          $frametime = $start_frame;
          $duration = ($end_frame - $start_frame);
          // |--------- video length -----------|
          // Stills per mediafile = 4
          // |------S------S------S------S------|
          // $framesecond = duration / (4 + 1)
          // .......|-- video length -----------|
          // frametime += $framesecond
          // duration -= $framesecond.
          $framesecond = $duration / ($still_per_mediafile + 1);
          $frametime += $framesecond;
          $duration -= $framesecond;
          // Frames per second.
          $framerate = 1 / max($framesecond, 1);
          // Safety check.
          if ($duration / max($framesecond, 1) > mediamosa_settings::STILL_MAXIMUM) {
            $duration = mediamosa_settings::STILL_MAXIMUM * $framesecond;
          }
          break;

        case 'SECOND':
          $still_every_second = $still_parameters['still_every_second'];
          $frametime = $start_frame;
          $duration = ($end_frame - $start_frame);
          $framerate = 1 / $still_every_second;
          // Safety check.
          if ($duration / $still_every_second > mediamosa_settings::STILL_MAXIMUM) {
            $duration = mediamosa_settings::STILL_MAXIMUM * $still_every_second;
          }
          break;

        case 'SCENE':
          mediamosa_job_server::log_mediafile($mediafile_id_source, 'Scene detection starts');
          $destination_path = mediamosa_storage::get_realpath_temporary();
          $input = mediamosa_storage::get_realpath_mediafile($mediafile_id_source);

          // Clean up I.
          mediamosa_io::unlink($destination_path . $mediafile_id_source . '_scene.avi');
          mediamosa_io::unlink($destination_path . $mediafile_id_source . '_list.el');

          // Hot fix. for what?
          $order = sprintf('ffmpeg -i %s %s', $input, $destination_path . $mediafile_id_source . '_scene.wmv');
          mediamosa_io::exec($order);

          $order = sprintf('ffmpeg -i %s -r 25 -an -vcodec mjpeg %s', $destination_path . $mediafile_id_source . '_scene.wmv', $destination_path . $mediafile_id_source . '_scene.avi');
          mediamosa_io::exec($order);

          mediamosa_io::unlink($destination_path . $mediafile_id_source . '_scene.wmv');
          // Hot fix - end.

          $order = sprintf('lav2yuv -T 1 -S %s %s', $destination_path . $mediafile_id_source . '_list.el', $destination_path . $mediafile_id_source . '_scene.avi');
          mediamosa_job_server::log_mediafile($mediafile_id_source, $order);
          mediamosa_io::exec($order);

          $order = sprintf('cat %s', $destination_path . $mediafile_id_source . '_list.el');
          $output = mediamosa_io::exec($order);

          // Clean up II.
          mediamosa_io::unlink($destination_path . $mediafile_id_source . '_list.el');

          // Analyze.
          if (!empty($output)) {

            // Tbr & map from the best stream.
            $map = NULL;
            $tbr = NULL;
            $kbs = NULL;
            $order = sprintf('ffmpeg -i %s 2>&1', $input);
            $details = mediamosa_io::exec($order);

            $regs = $reg_kbs = $reg_map = array();
            foreach ($details as $line) {
              if (stripos($line, ' Stream ') !== FALSE && stripos($line, ' Video: ') !== FALSE) {
                if (preg_match('/Stream #[0-9]*\.([0-9]*)/', $line, $reg_map) && preg_match('/ ([0-9]*) kb\/s/', $line, $reg_kbs)) {
                  if (!$map || !$kbs || $kbs < $reg_kbs[1]) {
                    $map = $reg_map[1];
                    $kbs = $reg_kbs[1];
                  }
                  if (preg_match('/ ([0-9]*(\.[0-9]*)+) tb/', $line, $regs)) {
                    $tbr = $regs[1];
                  }
                }
                elseif (!$map && preg_match('/ ([0-9]*(\.[0-9]*)+) tb/', $line, $regs)) {
                  $tbr = $regs[1];
                  if (preg_match('/Stream #[0-9]*\.([0-9]*)/', $line, $reg_map)) {
                    $map = $reg_map[1];
                  }
                }
              }
            }

            if (!$tbr) {
              // This is the base value.
              $tbr = 25;
            }
            // Go further, if we have tbr.
            if ($tbr) {

              $scene_frame = NULL;
              $i = 1;
              $scenes = array();
              foreach ($output as $line) {
                $line_args = explode(' ', $line);
                if ($line_args && is_array($line_args) && $line_args != array()) {
                  if (isset($line_args[0]) && isset($line_args[1]) && isset($line_args[2]) && !isset($line_args[3]) && is_numeric($line_args[0]) && is_numeric($line_args[1]) && is_numeric($line_args[2])) {
                    // $line_args[1] + num = a little bit after the change.
                    $change_frame = $line_args[1] + mediamosa_settings::STILL_SCENE_AFTER;
                    if ($change_frame >= ($start_frame * $fps) && $change_frame <= ($end_frame * $fps)) {
                      // $scene_frame + num = minimal distance with two changes.
                      if (!$scene_frame || $scene_frame + mediamosa_settings::STILL_SCENE_DISTANCE < $change_frame) {
                        $scene_frame = $change_frame;
                        // The avi has 25 frame/sec.
                        $scene_sec = (int) ($scene_frame / 25);
                        $scenes[] = $scene_sec;
                        $i++;
                        if ($i > mediamosa_settings::STILL_MAXIMUM) {
                          // Emergency break.
                          break;
                        }
                      }
                    }
                  }
                }
              }
            }
          }

          // Save the sec data (see: vpx_jobhandler.module)
          if (is_array($scenes) && $scenes != array()) {
            $my_file = $destination_path . $job_info['job_id'] . '_ffmpeg_scene.txt';
            $fh = fopen($my_file, 'w') or $fh = NULL;
            if ($fh) {
              foreach ($scenes as $scene) {
                fwrite($fh, $scene . "\n");

              }
              fclose($fh);

              // This version is creating stills for the original video.
              $size_explode = explode('x', $size);
              $size_width = ($v_padding > 0) ? $size_explode[0] + (2 * $v_padding) : 0;
              $size_height = ($h_padding > 0) ? $size_explode[1] + (2 * $h_padding) : 0;
              $order = sprintf(mediamosa_settings::STILL_SCENE_STRING_VFPAD, $input, $size, $size_width, $size_height, $v_padding, $h_padding, $my_file, ($map ? '-map 0:' . $map : ''), $destination_path . $mediafile_id_source . mediamosa_settings::STILL_EXTENSION . '.jpeg');

              // This version is creating stills for the analysed file.
              mediamosa_job_server::log_mediafile($mediafile_id_source, $order);
              $details = mediamosa_io::exec($order);

              mediamosa_io::unlink($my_file);
            }

            // Clean up III.
            mediamosa_io::unlink($destination_path . $mediafile_id_source . '_scene.avi');

            // It has to be the last, because it triggers the further processes.
            $my_file = $destination_path . $job_info['job_id'] . '_scene.txt';
            $fh = fopen($my_file, 'w') or $fh = NULL;
            if ($fh) {
              foreach ($scenes as $scene) {
                fwrite($fh, $scene . "\n");
              }
              fclose($fh);
            }

            // Do nothing after this.
            return 'echo "Scene stills are rock"';
          }

          // Clean up IV.
          mediamosa_io::unlink($destination_path . $mediafile_id_source . '_scene.avi');

          // We reached this point, so something went wrong in the creation of
          // scene stills.  So we are creating still type NONE (original
          // behaviour).
          break;

        default:
          break;
      }

    }
    else {
      mediamosa_job_server::log_mediafile($mediafile_id_source, 'Still job not found, jobserver_job_id: @jobserver_job_id', array('@jobserver_job_id' => $job_info['jobserver_job_id']));
      return $execution_string;
    }

    $still_parameters['frametime'] = $frametime;
    $still_parameters['duration'] = $duration;
    $still_parameters['framerate'] = $framerate;

    $fields = array(
      mediamosa_job_db::STILL_PARAMETERS => serialize($still_parameters),
    );

    // Enrich with changed/created.
    $fields = mediamosa_db::db_update_enrich($fields);
    mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
      ->fields($fields)
      ->condition(mediamosa_job_db::ID, $job_info['job_id'])
      ->execute();

    // Prepare with and heigt variations.
    $size_explode = explode('x', $size);
    // -vf scale takes "width:height" as parameter:
    $size_semicolon = $size_explode[0] . ':' . $size_explode[1];
    $size_width = ($v_padding > 0) ? $size_explode[0] + (2 * $v_padding) : 0;
    $size_height = ($h_padding > 0) ? $size_explode[1] + (2 * $h_padding) : 0;

    // Create execution string.
    // STILL_STRING_VFPAD = "' -s %s -vf
    // 'yadif,scale=%s,pad=%d:%d:%d:%d:black' -vframes %d -an -y -ss %s -t %s
    // -r %s -vcodec mjpeg -f image2'";
    $execution_string = sprintf('%s %s %s %s %s jpeg ' . mediamosa_settings::STILL_STRING_VFPAD,
      mediamosa_settings::lua_transcode_script(),
      mediamosa_storage::get_local_mediafile_path($mediafile_id_source),
      mediamosa_storage::get_realpath_temporary(),
      $mediafile_id_source,
      $job_info['job_id'],
      $size,
      $size_semicolon,
      $size_width,
      $size_height,
      $v_padding,
      $h_padding,
      round($duration * $framerate),
      $frametime,
      $duration,
      1);

    // Dit wou robert voorkomen
    //$execution_string .= ' > /dev/null &';
    return $execution_string;
  }

  /**
   * Based on job type, get more information.
   *
   * @param int $job_id
   *   The job ID.
   * @param string $job_type
   *   The job type, see mediamosa_job_db::JOB_TYPE_*.
   * @param string $mediafile_id
   *   The mediafile ID.
   */
  // Toon: Is modelled exactly after the real method
  function get_job_parameters($job_id, $mediafile_id) {
    $result = array();

    // Get something from mediamosa_still_db
    $query_job = mediamosa_job_still::get($job_id);
//    mediamosa_debug::log_export($query_job, WATCHDOG_NOTICE, 'Toon - STILL' );

    if ($query_job) {
      $result['blackstill_check'] = $query_job['blackstill_check'];
      $result['still_parameters'] = unserialize($query_job['still_parameters']);
      $result['frametime'] = $result['still_parameters']['frametime'];
      $result['h_padding'] = $result['still_parameters']['h_padding'];
      $result['v_padding'] = $result['still_parameters']['v_padding'];
      $result['tag'] = $result['still_parameters']['tag'];

      $mediafile = mediamosa_asset_mediafile::get($mediafile_id, NULL, array(mediamosa_asset_mediafile_db::APP_ID));
      $app_id = $mediafile[mediamosa_asset_mediafile_db::APP_ID];

      // Pre-defined ratios.
      $sizes = array(
        'sqcif' => '128x96', 'qcif' => '176x144', 'cif' => '352x288', '4cif' => '704x576',
        'qqvga' => '160x120', 'qvga' => '320x240', 'vga' => '640x480', 'svga' => '800x600',
        'xga' => '1024x768', 'uxga' => '1600x1200', 'qxga' => '2048x1536', 'sxga' => '1280x1024',
        'qsxga' => '2560x2048', 'hsxga' => '5120x4096', 'wvga' => '852x480', 'wxga' => '1366x768',
        'wsxga' => '1600x1024', 'wuxga' => '1920x1200', 'woxga' => '2560x1600',
        'wqsxga' => '3200x2048', 'wquxga' => '3840x2400', 'whsxga' => '6400x4096',
        'whuxga' => '7680x4800', 'cga' => '320x200', 'ega' => '640x350', 'hd360' => '640x360',
        'hd480' => '852x480', 'hd720' => '1280x720', 'hd1080' => '1920x1080',
      );

      // Find target size.
      if (isset($sizes[$query_job['size']])) {
        $target_size = $sizes[$query_job['size']];
      }
      else {
        // Use size of still parameters instead.
        $target_size = $result['still_parameters']['size'];

        // Check the size.
        if (!preg_match('/(\d+)x(\d+)/', $target_size)) {
          // If there is a still default size for the client app available,
          // then use that instead.
          $target_size = mediamosa_app::get_still_default_size($app_id);
          if (!$target_size) {
            // Get the video size.
            $target_size = mediamosa_asset_mediafile::get_size($mediafile_id);
          }
        }
      }

      // First get source width and height.
      $metadata = mediamosa_asset_mediafile_metadata::get_with_mediafileid($mediafile_id, array(
        array(
          'prop_name' => mediamosa_asset_mediafile_metadata::WIDTH,
          'type' => mediamosa_asset_mediafile_metadata_property_db::TYPE_INT,
        ),
        array(
          'prop_name' => mediamosa_asset_mediafile_metadata::HEIGHT,
          'type' => mediamosa_asset_mediafile_metadata_property_db::TYPE_INT,
        ),
      ));
      $width = $metadata[mediamosa_asset_mediafile_metadata::WIDTH];
      $height = $metadata[mediamosa_asset_mediafile_metadata::HEIGHT];
      $still_padding = mediamosa_app::get_still_padding_value($app_id);

      // Get the parameter settings.
      $calc_aspect_ratio = mediamosa_gd::calcAspectRatio($width, $height, $target_size, $result['h_padding'], $result['v_padding'], $still_padding == mediamosa_app_db::STILL_PADDING_YES);

      // Set result.
      if ($calc_aspect_ratio) {
        $result['size'] = $calc_aspect_ratio['width'] . 'x' . $calc_aspect_ratio['height'];
        $result['h_padding'] = $calc_aspect_ratio['h_padding'];
        $result['v_padding'] = $calc_aspect_ratio['v_padding'];
      }
      else {
        if ($width && $height) {
          $result['size'] = $width . 'x' . $height;
        }
        else {
          $result['size'] = '640x360';
        }
        $result['h_padding'] = 0;
        $result['v_padding'] = 0;
      }
    }
    else {
      echo 'Entered the else statement in ' . __METHOD__;
      // Something went wrong in the analyse script.
      // Fall back to the default values.
      $result['frametime'] = mediamosa_settings::STILL_DEFAULT_FRAME_TIME;
      $result['size'] = '640x360';
      $result['h_padding'] = 0;
      $result['v_padding'] = 0;
      $result['blackstill_check'] = 'FALSE';
      $result['tag'] = '';
    }

    return $result;
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

  /**
   * Check the created still and save it if everything is ok.
   *
   * @param string $job_id
   *   Current job id.
   * @param string $mediafile_id_src
   *   Contains a file path to the mediafile
   * @return string
   *   Contains the error message
   */
  function store_new_still($job) {
    $job_type = $job[mediamosa_job_db::JOB_TYPE];
    $job_id = $job[mediamosa_job_db::ID];
    $mediafile_id_src = $job[mediamosa_job_db::MEDIAFILE_ID];

    $append_fields = mediamosa_job::get($job_id, array(mediamosa_job_db::APP_ID, mediamosa_job_db::HINT, mediamosa_job_db::STILL_PARAMETERS, mediamosa_job_db::OWNER_ID));
    $job = array_merge($job, $append_fields);
    //mediamosa_debug::log('$job array was enriched in ' . __CLASS__ . ', ' . __METHOD__,  array(), 'Toon - STILL');
    //mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'Toon - STILL' );

    $app_id = $job[mediamosa_job_db::APP_ID];

    $base_filename = mediamosa_io::get_base_filename($job_id);

    // Check if there really is an image ($file_size > 0)
    $filename = mediamosa_storage::get_uri_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, 1) . '.jpeg');

    if (!mediamosa_io::file_exists($filename) || !mediamosa_io::filesize($filename)) {
      mediamosa_debug::log('STILL creation failed, file does not exist. ' . __CLASS__ . ', ' . __METHOD__ . ', on line: ' . __LINE__, array(), 'Toon - STILL');
      // Something failed. Remove the files and fail the job.

      $still_error = mediamosa_error::error_code_find_description(mediamosa_error::ERRORCODE_STILL_IS_NOT_CREATABLE, array('@mediafile_id' => $mediafile_id_src));

      // Update status.
      //$this->set_job_status($job_id, mediamosa_job_db::JOB_STATUS_FAILED, $job_type, $mediafile_id_src,'1.000', $still_error);
      // My edited version of update
      $this->set_job_status($job, mediamosa_job_db::JOB_STATUS_FAILED, $still_error);


      // Remove all of the still images.
      $i = 1;
      while (mediamosa_io::file_exists(mediamosa_storage::get_realpath_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, $i) . '.jpeg')) && $i <= mediamosa_settings::STILL_MAXIMUM) {
        mediamosa_io::unlink(mediamosa_storage::get_realpath_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, $i) . '.jpeg'));
        $i++;
      }
      mediamosa_io::unlink(mediamosa_storage::get_realpath_status_file($job_id));

      mediamosa_job_server::log_mediafile($mediafile_id_src, $still_error);
      return mediamosa_job_server_db::JOB_STATUS_FAILED;
    }
    // Check if the frame has any usefull content. We do this by checking the amount of dominant colors.
    mediamosa_job_server_still::still_validate($job_id, $base_filename);

    $i = 1;
    $mediafile_dest = array();
    while (mediamosa_io::file_exists(mediamosa_storage::get_uri_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, $i) . '.jpeg'))) {
      if ($i <= mediamosa_settings::STILL_MAXIMUM) {
        // Generate new hash./domio
        $mediafile_id = mediamosa_db::uuid($app_id);

        $source_uri = mediamosa_storage::get_uri_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, $i) . '.jpeg');
        $destination_uri = mediamosa_storage::create_local_mediafile_uri($app_id, $mediafile_id);

        // Make sure destination dir exists.
        mediamosa_io::mkdir(mediamosa_io::dirname($destination_uri));

        // Everything went ok, move the still and remove other files
        mediamosa_io::rename($source_uri, $destination_uri);
        $mediafile_dest[] = $mediafile_id;
        $myvar = 1;
//        mediamosa_debug::log('Stills made: ' . $myvar, array(), 'Toon - STILL');
        $myvar += 1;
      }
      else {
        // Reached the maximum, just delete the remain stills.
        mediamosa_io::unlink(mediamosa_storage::get_realpath_temporary_file($base_filename . sprintf(mediamosa_settings::STILL_EXTENSION, $i) . '.jpeg'));
      }

      $i++;
    }
    mediamosa_io::unlink(mediamosa_storage::get_realpath_status_file($job_id));

//    mediamosa_debug::log_export($mediafile_dest, WATCHDOG_NOTICE, 'Toon - STILL' );
    // Data to update.
    $fields = array(
      // Toon: Original
      //mediamosa_job_server_db::MEDIAFILE_DEST => serialize($mediafile_dest),
      // Toon: Modified
      mediamosa_job_db::MEDIAFILE_DEST => $mediafile_dest,
    );
    // Contents are fine
    //Omediamosa_debug::log_export($fields, WATCHDOG_NOTICE, 'Toon - STILL' );


    // Add changed.
    $fields = mediamosa_db::db_update_enrich($fields);

    // Update mediafile_dest of the job
    mediamosa_db::db_update(mediamosa_job_db::TABLE_NAME)
      ->fields($fields)
      ->condition(mediamosa_job_db::ID, $job_id)
      ->execute();

    // Log it.
    mediamosa_job_server::log_mediafile($mediafile_id_src, 'Job (job_id: @job_id) finished: Multiple stills saved as e.g.: @filenames.', array('@job_id' => $job_id, '@filenames' => implode(',', $mediafile_dest)));

    // unserialize turns value into false so away it goes
    //$this->add_still_to_db($job, unserialize((string)$mediafile_dest)); // $job here is nothing but app_id and hint
    //mediamosa_debug::log_export($mediafile_dest, WATCHDOG_NOTICE, 'Toon - STILL' );

    // Bug #1: mediafile_dest has the wrong contents
    $this->add_still_to_db($job, $mediafile_dest); // $job here is nothing but app_id and hint


    return mediamosa_job_server_db::JOB_STATUS_FINISHED;
  }

  /**
   * Update the job.
   *
   * Updates running jobs.
   *
   * 1. Get possible status file and parse it.
   * 2. When transcode or still job is done, files will be moved to the
   *    correct location.
   */
  // Toon: Modelled after running_job_update

  function get_status_contents($job_id, $orig = FALSE) {
    // Get the statusfile filename.
    $statusfile = mediamosa_storage::get_realpath_status_file($job_id);
    mediamosa_io::clearstatcache($statusfile);
    if (!mediamosa_io::file_exists($statusfile)) {
      mediamosa_job_server::log('Unable to load status contents; file @file does not exists.', array('@file' => $statusfile), WATCHDOG_CRITICAL);
      return array();
    }

    $result = array();
    $lines = array();

    // Set default.
    $result += array(
      'Errors' => 'none',
    );

    // FIXME: move to mediamosa_io
    $handle = @fopen($statusfile, 'r');
    if (is_resource($handle)) {
      while (($line = fgets($handle)) !== false) {
        $lines[] = $line;
      }
      fclose($handle);
    }
    else {
      mediamosa_job_server::log_debug('Unable to open status file using fopen; @file', array('@file' => $statusfile));
    }

    // Return the original?
    if ($orig) {
      return implode('', $lines);
    }

    // Strip the garbage from the file.
    foreach ($lines as $line) {
      if (mediamosa_unicode::strpos($line, ':') === FALSE) {
        continue;
      }

      list($name, $value) = explode(':', $line, 2);
      if ($name == 'Progress' && empty($value)) {
        $value = '0.000';
      }
      elseif ($name == 'Progress' || $name == 'Status' || $name == 'Errors') {
        $result[$name] = trim($value);
      }
      elseif ($name == 'ffmpeg-output') {
        $result[$name] = implode("\n", explode('}-{', trim($value)));
      }
    }

    // If there is no result we return empty array.
    if (!empty($result)) {
      // Set defaults, to fix some possible notices.
      $result += array(
        'Status' => '',
        'Errors' => 'none',
        'Progress' => '0.000',
      );
    }

    return $result;
  }



  /**
   * Process the data from a still job.
   *
   * @param int $job_id
   *   The job ID.
   * @param array $filenames
   *   The filenames of the stills.
   */
  public static function add_still_to_db($job, $filenames) {
    mediamosa_debug::log('Entered ' . __CLASS__ . ', ' . __METHOD__, array(), 'Toon - STILL');
    // Vergeet niet dat job id to string werd gedaan
    $job_id = $job['job_id'];
    $asset_id = $job['asset_id'];
    $app_id = $job['app_id'];
    $owner = $job['owner'];
    $still_parameters = unserialize($job['still_parameters']);

    // Scene changes.
    $scene_realpath = mediamosa_storage::get_realpath_temporary_file($job_id . '_scene.txt');

    $scenes = array();

    if (mediamosa_io::file_exists($scene_realpath)) {
      $fh = @fopen($scene_realpath, 'r');
      if ($fh) {
        while (!feof($fh)) {
          $scenes[] = (int) fgets($fh);
        }
        fclose($fh);
      }
    }

    mediamosa_job_scheduler::log_mediafile(
      $job['mediafile_id'],
      'Start creation DB multiple still, e.g. @filename, job: @job_id, still_parameters: @still_parameters',
      array(
        '@filename' => $filenames[0],
        '@job_id' => (string) $job_id,
        '@still_parameters' => print_r($still_parameters, TRUE),
      ),
      $asset_id
    );


    // Remove old stills
    // We have multiple stills now, so we don't delete the old ones
    // And deleting with asset_id is definetly not a good idea, while we have
    // multiple stills per mediafile _media_management_delete_still($asset_id);
    //
    // Add record to the mediafile metadata table.
    if (is_array($filenames)) {
//      mediamosa_debug::log('$filenames was determined as an array.', array(), 'Toon - STILL');
      //mediamosa_debug::log_export($filenames, WATCHDOG_NOTICE, 'Toon - STILL' );

      $frametime = $still_parameters['frametime'];
      if (isset($still_parameters['framerate']) && is_numeric($still_parameters['framerate'])) {
        $second = $still_parameters['framerate'] > 0 ? 1 / $still_parameters['framerate'] : 0;
      }
      $tag = $still_parameters['tag'];

      $order = 0;
      $sec = 0;
      if (isset($frametime) && is_numeric($frametime)) {
        $sec = $frametime;
      }
      $i = 0;
      foreach ($filenames as $filename) {
        mediamosa_asset_mediafile_still::create($asset_id, $filename, $app_id, $owner, '', $order, !$order, $still_parameters, ($scenes == array() ? $sec : $scenes[$i]), $job['mediafile_id'], $tag);

        $order++;
        if (isset($second) && is_numeric($second)) {
          $sec += $second;
        }

        $i++;
      }
    }

    // Remove file.
    mediamosa_io::unlink($scene_realpath);
  }
}
