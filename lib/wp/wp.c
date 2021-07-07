/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp"

#include "wp.h"
#include <pipewire/pipewire.h>

/*!
 * \defgroup wp Library Initialization
 * \{
 */

/*!
 * \brief Initializes WirePlumber and PipeWire underneath.
 *
 * \em flags can modify which parts are initialized, in cases where you want
 * to handle part of this initialization externally.
 *
 * \param flags initialization flags
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
      g_warn_if_fail (g_setenv ("PIPEWIRE_DEBUG", lvl_str, TRUE));
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
 * \brief Gets the Wireplumber module directory
 * \returns The Wireplumber module directory
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
 * \brief Gets the full path to the WirePlumber XDG_STATE_HOME subdirectory
 * \returns Wireplumber's XDG_STATE_HOME subdirectory
 */
const gchar *
wp_get_xdg_state_dir (void)
{
  static gchar xdg_dir[PATH_MAX] = {0};
  if (xdg_dir[0] == '\0') {
    g_autofree gchar *path = NULL;
    g_autofree gchar *base = g_strdup (g_getenv ("XDG_STATE_HOME"));
    if (!base)
      base = g_build_filename (g_get_home_dir (), ".local", "state", NULL);

    path = g_build_filename (base, "wireplumber", NULL);
    g_strlcpy (xdg_dir, path, sizeof (xdg_dir));
  }
  return xdg_dir;
}

/*!
 * \brief Gets the full path to the Wireplumber XDG_CONFIG_DIR subdirectory
 * \returns The XDG_CONFIG_DIR subdirectory
 */
const gchar *
wp_get_xdg_config_dir (void)
{
  static gchar xdg_dir[PATH_MAX] = {0};
  if (xdg_dir[0] == '\0') {
    g_autofree gchar *path = g_build_filename (g_get_user_config_dir (),
                                               "wireplumber", NULL);
    g_strlcpy (xdg_dir, path, sizeof (xdg_dir));
  }
  return xdg_dir;
}

/*!
 * \brief Gets the full path to the Wireplumber configuration directory
 * \returns The Wireplumber configuration directory
 */
const gchar *
wp_get_config_dir (void)
{
  static gchar config_dir[PATH_MAX] = {0};
  if (config_dir[0] == '\0') {
    g_autofree gchar *abspath;
    const gchar *path = g_getenv ("WIREPLUMBER_CONFIG_DIR");

    if (!path)
      path = WIREPLUMBER_DEFAULT_CONFIG_DIR;

    abspath = g_canonicalize_filename (path, NULL);
    g_strlcpy (config_dir, abspath, sizeof (config_dir));
  }
  return config_dir;
}

/*!
 * \brief Gets full path to the Wireplumber data directory
 * \returns The Wireplumber data directory
 */
const gchar *
wp_get_data_dir (void)
{
  static gchar data_dir[PATH_MAX] = {0};
  if (data_dir[0] == '\0') {
    g_autofree gchar *abspath;
    const char *path = g_getenv ("WIREPLUMBER_DATA_DIR");
    if (!path)
      path = WIREPLUMBER_DEFAULT_DATA_DIR;
    abspath = g_canonicalize_filename (path, NULL);
    g_strlcpy (data_dir, abspath, sizeof (data_dir));
  }
  return data_dir;
}

/*! \} */
