<?php

class still
{

  // Can only handle video/mp4 at the moment due to the specific still exec
  function start($job) {
    $execution_string = $this->generate_still_exec($job);
    mediamosa_debug::log('Execution string received: ' . $execution_string, array(), 'T - STILL');
    $exec_output = mediamosa_io::exec($execution_string);


//    $link_asset = $this->get_asset_link($job['job_id']);
    $link_asset = $job['asset_link'];

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

  function generate_still_exec($job) {
    $mediafile_id_source = $job['mediafile_id'];
    $execution_string = '';

    mediamosa_debug::log('$job: ', array(), 'T - MISC10');
    mediamosa_debug::log_export($job, WATCHDOG_NOTICE, 'T - MISC10');

    $still_parameters = unserialize($job[mediamosa_job_server_still_db::STILL_PARAMETERS]);

    $job_info = [];
    $job_info['jobserver_job_id'] = 13;
    $job_info['size'] = $job['size'];
    $job_info['frametime'] = $still_parameters['frametime'];
    $job_info['h_padding'] = $still_parameters['h_padding'];
    $job_info['v_padding'] = $still_parameters['v_padding'];
    $job_info['blackstill_check'] = $job['blackstill_check'];
    $job_info[mediamosa_job_server_still_db::STILL_PARAMETERS] = $job[mediamosa_job_server_still_db::STILL_PARAMETERS];
    $job_info['job_id'] = $job['job_id'];

    $job_info['created'] = 13;
    $job_info['changed'] = 13;
    $job_info['install_id'] = 13;
    $job_info['status'] = 13;
    $job_info['progress'] = 13;
    $job_info['job_type'] = 13;
    $job_info['progress_id'] = 13;
    $job_info['mediafile_src'] = 13;
    $job_info['mediafile_dest'] = 13;
    $job_info['started'] = 13;
    $job_info['finished'] = 13;
    $job_info['error_description'] = 13;

//    $job_info = [];
//    $job_info['jobserver_job_id'] = 13;
//    $job_info['size'] = $job['size'];
//    $job_info['frametime'] = $job['still_parameters']['frametime'];
//    $job_info['h_padding'] = $job['still_parameters']['h_padding'];
//    $job_info['v_padding'] = $job['still_parameters']['v_padding'];
//    $job_info['blackstill_check'] = $job['h_padding'];
//    $job_info[mediamosa_job_server_still_db::STILL_PARAMETERS] = $job[mediamosa_job_server_still_db::STILL_PARAMETERS];
//    $job_info['job_id'] = $job['job_id'];
//    $job_info['created'] = 13;
//    $job_info['changed'] = 13;
//    $job_info['install_id'] = 13;
//    $job_info['status'] = 13;
//    $job_info['progress'] = 13;
//    $job_info['job_type'] = 13;
//    $job_info['progress_id'] = 13;
//    $job_info['mediafile_src'] = 13;
//    $job_info['mediafile_dest'] = 13;
//    $job_info['started'] = 13;
//    $job_info['finished'] = 13;
//    $job_info['error_description'] = 13;

    mediamosa_debug::log('$job_info: ', array(), 'T - MISC10');
    mediamosa_debug::log_export($job_info, WATCHDOG_NOTICE, 'T - MISC10');

    // ############
    // Section for files other than video
    $mime_type = mediamosa_asset_mediafile_metadata::get_mediafile_metadata_char($mediafile_id_source, mediamosa_asset_mediafile_metadata::MIME_TYPE);
    foreach (module_implements('mediamosa_tool_can_generate_still') as $module) {
      if (module_invoke($module, 'mediamosa_tool_can_generate_still', $mime_type)) {
        // Get generate still exec.
        return module_invoke($module, 'mediamosa_tool_get_generate_still_exec', $job_info, $mediafile_id_source);
      }
    }
    // ############
    mediamosa_debug::log('Contents of $job_info: ', array(), 'T - MISC5');
    mediamosa_debug::log_export($job_info, WATCHDOG_NOTICE, 'T - MISC5');

    // ????????
    if ($job_info['job_id']) {
      mediamosa_debug::log('Entered this strange if statement', array(), 'T - MISC5');

//      // maybe this?
//      $frametime = (int) $job_info[mediamosa_job_server_still_db::frametime];
//      $size = $job_info[mediamosa_job_server_still_db::size];
//      $h_padding = $job_info[mediamosa_job_server_still_db::h_padding];
//      $v_padding = $job_info[mediamosa_job_server_still_db::v_padding];

      // maybe this?
      $frametime = (int) $still_parameters[mediamosa_job_server_still_db::FRAMETIME];
      $size = $still_parameters[mediamosa_job_server_still_db::SIZE];
      $h_padding = $still_parameters[mediamosa_job_server_still_db::H_PADDING];
      $v_padding = $still_parameters[mediamosa_job_server_still_db::V_PADDING];
      mediamosa_debug::log('Reached1 15:31 ', array(), 'T - MISC10');

      // gives errors: $still_parameters = unserialize($job_info[mediamosa_job_server_still_db::STILL_PARAMETERS]);
//      $still_parameters = $job_info[mediamosa_job_server_still_db::STILL_PARAMETERS];
      $fps = (int) $still_parameters['fps'];
      $video_duration = (int) $still_parameters['video_duration'];

      // Video duration check, if still_type = scene.
      if (!$video_duration && $still_parameters['still_type'] == mediamosa_asset_mediafile_metadata::STILL_TYPE_SCENE) {

        mediamosa_debug::log('Reached2 15:31 video dur: ' . $video_duration, array(), 'T - MISC10');

        // Some videos don't have video durations according to FFMpeg. If we
        // want to create multiple stills, we usually need this information
        // calculating other data.  But in case of scene detection we have a
        // work around, we can put the video duration on a maximum value.
        mediamosa_job_server::log_debug_high_mediafile($mediafile_id_source, t('No video duration, when still_type = scene. Try to get the complete video. video_duration = @video_duration.', array(
          '@video_duration' => print_r($video_duration, TRUE),
        )));
        $video_duration = mediamosa_tool_ffmpeg::VIDEO_DURATION_MAX;
      }

      mediamosa_debug::log('Reached3 15:31 ', array(), 'T - MISC10');

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

      mediamosa_debug::log('Reached4 15:31 ', array(), 'T - MISC10');
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

      mediamosa_debug::log('Reached5 15:31 ', array(), 'T - MISC10');

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
      mediamosa_debug::log('Reached6 15:31 ', array(), 'T - MISC10');

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
    mediamosa_debug::log('Reached7 15:31 ', array(), 'T - MISC10');

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

    mediamosa_debug::log('Reached8 15:31 ', array(), 'T - MISC10');
    mediamosa_debug::log('Checkpoint1', array(), 'T - MISC10');
    mediamosa_settings::lua_transcode_script();
    mediamosa_debug::log('Checkpoint1 passed', array(), 'T - MISC10');
    mediamosa_debug::log('Checkpoint2', array(), 'T - MISC10');
    mediamosa_storage::get_local_mediafile_path($mediafile_id_source);
    mediamosa_debug::log('Checkpoint2 passed', array(), 'T - MISC10');
    mediamosa_debug::log('Checkpoint3', array(), 'T - MISC10');
    mediamosa_storage::get_realpath_temporary();
    mediamosa_debug::log('Checkpoint3 passed', array(), 'T - MISC10');


    // Prepare with and heigt variations.
    $size_explode = explode('x', $size);
    // -vf scale takes "width:height" as parameter:
    $size_semicolon = $size_explode[0] . ':' . $size_explode[1];
    $size_width = ($v_padding > 0) ? $size_explode[0] + (2 * $v_padding) : 0;
    $size_height = ($h_padding > 0) ? $size_explode[1] + (2 * $h_padding) : 0;

    $arr = [
      'med id source' => $mediafile_id_source,
      'id' => $job['job_id'],
      'size' => $size,
      'size_semicolon' => $size_semicolon,
      'size_width' => $size_width,
      'size_height' => $size_height,
      'v_padding' => $v_padding,
      'h_padding' => $h_padding,
      'something' => round($duration * $framerate),
      'frametime' => $frametime,
      'duration' => $duration
    ];
    mediamosa_debug::log_export($arr, WATCHDOG_NOTICE, 'T - MISC11');
    // Create execution string.
    // STILL_STRING_VFPAD = "' -s %s -vf
    // 'yadif,scale=%s,pad=%d:%d:%d:%d:black' -vframes %d -an -y -ss %s -t %s
    // -r %s -vcodec mjpeg -f image2'";
    $execution_string = sprintf('%s %s %s %s %s jpeg ' . mediamosa_settings::STILL_STRING_VFPAD,
      mediamosa_settings::lua_transcode_script(),
      mediamosa_storage::get_local_mediafile_path($mediafile_id_source),
      mediamosa_storage::get_realpath_temporary(),
      $mediafile_id_source,
      $job['job_id'],
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

    mediamosa_debug::log('Reached9 15:24', array(), 'T - MISC10');


    return $execution_string;
  }

}
