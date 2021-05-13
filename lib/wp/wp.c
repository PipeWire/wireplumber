/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/*!
 * @file wp.c
 */
#define G_LOG_DOMAIN "wp"

#include "wp.h"
#include <pipewire/pipewire.h>

/*!
 * @struct Wp
 *
 * @section wp_section Wp
 *
 */

/*!
 * @memberof Wp
 * @param flags: initialization flags
 *
 * @brief Initializes WirePlumber and PipeWire underneath. @em flags can modify
 * which parts are initialized, in cases where you want to handle part
 * of this initialization externally.
 */

void
wp_init (WpInitFlags flags)
{
  if (flags & WP_INIT_SET_GLIB_LOG)
    g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  /* Initialize the logging system */
  wp_log_set_level (g_getenv ("WIREPLUMBER_DEBUG"));
  wp_info ("WirePlumber " WIREPLUMBER_VERSION " initializing");

  /* set PIPEWIRE_DEBUG and the spa_log interface that pipewire will use */
  if (flags & WP_INIT_SET_PW_LOG && !g_getenv ("WIREPLUMBER_NO_PW_LOG")) {
    if (g_getenv ("WIREPLUMBER_DEBUG")) {
      gchar lvl_str[2];
      g_snprintf (lvl_str, 2, "%d", wp_spa_log_get_instance ()->level);
      g_setenv ("PIPEWIRE_DEBUG", lvl_str, TRUE);
    }
    pw_log_set (wp_spa_log_get_instance ());
  }

  if (flags & WP_INIT_PIPEWIRE)
    pw_init (NULL, NULL);

  if (flags & WP_INIT_SPA_TYPES)
    wp_spa_dynamic_type_init ();

  /* ensure WpProxy subclasses are loaded, which is needed to be able
    to autodetect the GType of proxies created through wp_proxy_new_global() */
  g_type_ensure (WP_TYPE_CLIENT);
  g_type_ensure (WP_TYPE_DEVICE);
  g_type_ensure (WP_TYPE_ENDPOINT);
  g_type_ensure (WP_TYPE_LINK);
  g_type_ensure (WP_TYPE_METADATA);
  g_type_ensure (WP_TYPE_NODE);
  g_type_ensure (WP_TYPE_PORT);
}

/*!
 * @memberof Wp
 *
 * @param Void
 *
 * @returns The Wireplumber module directory
 */

const gchar *
wp_get_module_dir (void)
{
  static const gchar *module_dir = NULL;
  if (!module_dir) {
    module_dir = g_getenv ("WIREPLUMBER_MODULE_DIR");
    if (!module_dir)
      module_dir = WIREPLUMBER_DEFAULT_MODULE_DIR;
  }
  return module_dir;
}

/*!
 * @memberof Wp
 *
 * @param Void
 *
 * @returns The Wireplumber config directory
 */

const gchar *
wp_get_config_dir (void)
{
  static const gchar *config_dir = NULL;
  if (!config_dir) {
    config_dir = g_getenv ("WIREPLUMBER_CONFIG_DIR");
    if (!config_dir)
      config_dir = WIREPLUMBER_DEFAULT_CONFIG_DIR;
  }
  return config_dir;
}

/*!
 * @memberof Wp
 *
 * @param Void
 *
 * @returns The Wireplumber data directory
 */

const gchar *
wp_get_data_dir (void)
{
  static const gchar *data_dir = NULL;
  if (!data_dir) {
    data_dir = g_getenv ("WIREPLUMBER_DATA_DIR");
    if (!data_dir)
      data_dir = WIREPLUMBER_DEFAULT_DATA_DIR;
  }
  return data_dir;
}