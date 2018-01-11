<?php
/**
 * Created by PhpStorm.
 * User: tony
 * Date: 1/10/18
 * Time: 3:37 PM
 */

interface helper
{
  function generate_exec($job_id, $mediafile_id, $job_parameters, $mediafile_path);
  function start($job);
}