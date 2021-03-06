<?php
// $Id$

/**
 * MediaMosa is Open Source Software to build a Full Featured, Webservice
 * Oriented Media Management and Distribution platform (http://mediamosa.org)
 *
 * Copyright (C) 2012 SURFnet BV (http://www.surfnet.nl) and Kennisnet
 * (http://www.kennisnet.nl)
 *
 * MediaMosa is based on the open source Drupal platform and
 * was originally developed by Madcap BV (http://www.madcap.nl)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, you can find it at:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

/**
 * @file
 * Server job still table.
 */
class mediamosa_job_server_still_db {
  // ------------------------------------------------------------------ Consts.
  // Table name;
  const TABLE_NAME = 'mediamosa_job_server_still';

  // Database fields;
  const ID = 'jobserver_job_id';
  const SIZE = 'size';
  const SIZE_LENGTH = 10;
  const FRAMETIME = 'frametime';
  const H_PADDING = 'h_padding';
  const V_PADDING = 'v_padding';
  const BLACKSTILL_CHECK = 'blackstill_check';
  const BLACKSTILL_CHECK_TRUE = 'TRUE';
  const BLACKSTILL_CHECK_FALSE = 'FALSE';
  const STILL_PARAMETERS = 'still_parameters';
  const CREATED = 'created';
  const CHANGED = 'changed';
}
