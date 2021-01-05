/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "wp.h"
#include <pipewire/pipewire.h>

/**
 * SECTION: wp
 * @title: Library Initialization
 */


/**
 * WpInitFlags:
 * @WP_INIT_PIPEWIRE: Initializes libpipewire by calling `pw_init()`
 * @WP_INIT_SPA_TYPES: Initializes WirePlumber's SPA types integration,
 *     required for using #WpSpaPod among other things
 * @WP_INIT_SET_PW_LOG: Enables redirecting debug log messages from
 *     libpipewire to GLib's logging system, by installing WirePlumber's
 *     implementation of `struct spa_log` (see wp_spa_log_get_instance())
 *     with `pw_log_set()`
 * @WP_INIT_SET_GLIB_LOG: Installs WirePlumber's debug log handler,
 *     wp_log_writer_default(), on GLib with g_log_set_writer_func()
 * @WP_INIT_ALL: Enables all of the above
 *
 * See wp_init()
 */

/**
 * wp_init:
 * @flags: initialization flags
 *
 * Initializes WirePlumber and PipeWire underneath. @flags can modify
 * which parts are initialized, in cases where you want to handle part
 * of this initialization externally.
 */
void
wp_init (WpInitFlags flags)
{
  enum spa_log_level lvl = 0;

  if (flags & WP_INIT_SET_GLIB_LOG)
    g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  /* a dummy message, to initialize the logging system */
  wp_info ("WirePlumber " WIREPLUMBER_VERSION " initializing");

  if (flags & WP_INIT_SET_PW_LOG && !g_getenv ("WIREPLUMBER_NO_PW_LOG")) {
    pw_log_level = lvl = wp_spa_log_get_instance ()->level;
    pw_log_set (wp_spa_log_get_instance ());
  }

  if (flags & WP_INIT_PIPEWIRE)
    pw_init (NULL, NULL);

  /* restore our log level to override PIPEWIRE_DEBUG */
  if (flags & WP_INIT_SET_PW_LOG && !g_getenv ("WIREPLUMBER_NO_PW_LOG"))
    pw_log_set_level (lvl);

  if (flags & WP_INIT_SPA_TYPES)
    wp_spa_type_init (TRUE);

  /* ensure WpProxy subclasses are loaded, which is needed to be able
    to autodetect the GType of proxies created through wp_proxy_new_global() */
  g_type_ensure (WP_TYPE_CLIENT);
  g_type_ensure (WP_TYPE_DEVICE);
  g_type_ensure (WP_TYPE_ENDPOINT);
  g_type_ensure (WP_TYPE_ENDPOINT_LINK);
  g_type_ensure (WP_TYPE_ENDPOINT_STREAM);
  g_type_ensure (WP_TYPE_LINK);
  g_type_ensure (WP_TYPE_METADATA);
  g_type_ensure (WP_TYPE_NODE);
  g_type_ensure (WP_TYPE_PORT);
  g_type_ensure (WP_TYPE_SESSION);
}
