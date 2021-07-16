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
    (void) g_strlcpy (xdg_dir, path, sizeof (xdg_dir));
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
    (void) g_strlcpy (xdg_dir, path, sizeof (xdg_dir));
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
    (void) g_strlcpy (config_dir, abspath, sizeof (config_dir));
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
    (void) g_strlcpy (data_dir, abspath, sizeof (data_dir));
  }
  return data_dir;
}

static gchar *
check_path (const gchar *basedir, const gchar *subdir, const gchar *filename)
{
  g_autofree gchar *path = g_build_filename (basedir,
                                             subdir ? subdir : filename,
                                             subdir ? filename : NULL,
                                             NULL);
  g_autofree gchar *abspath = g_canonicalize_filename (path, NULL);
  wp_trace ("checking %s", abspath);
  if (g_file_test (abspath, G_FILE_TEST_IS_REGULAR))
    return g_steal_pointer (&abspath);
  return NULL;
}

/*!
 * \brief Flags to specify lookup directories
 * Use one of the `WP_CDIR_SET` values instead of the values directly.
 */
typedef enum {
  /* config group */
  WP_CDIR_ENV_CONFIG = (1 << 0),  /**< $WIREPLUMBER_CONFIG_DIR */
  WP_CDIR_XDG_HOME = (1 << 1),    /**< XDG_CONFIG_HOME */
  WP_CDIR_ETC = (1 << 2),         /**< /etc/wireplumber */

  /* data group */
  WP_CDIR_ENV_DATA = (1 << 10),   /**< $WIREPLUMBER_DATA_DIR */
  WP_CDIR_USR = (1 << 11),        /**< /usr/share/wireplumber */

  /** The set for system-only files */
  WP_CDIR_SET_SYSTEM = WP_CDIR_ETC | WP_CDIR_USR | WP_CDIR_ENV_DATA,

  /** The set for user + system files */
  WP_CDIR_SET_USER = WP_CDIR_ENV_CONFIG | WP_CDIR_XDG_HOME | WP_CDIR_SET_SYSTEM,
} WpConfigDir;

static GPtrArray *
lookup_dirs (guint flags)
{
  g_autoptr(GPtrArray) dirs = g_ptr_array_new_with_free_func (g_free);
  const gchar *dir;

  /* Compile the list of lookup directories in priority order:
   * - environment variables
   * - XDG directories
   * - etc
   * - /usr/share/....
   *
   * Note that environment variables *replace* the equivalent config directory
   * group.
   */
  if ((flags & WP_CDIR_ENV_CONFIG) &&
      (dir = g_getenv ("WIREPLUMBER_CONFIG_DIR"))) {
    g_ptr_array_add (dirs, g_canonicalize_filename (dir, NULL));
  } else {
    if ((flags & WP_CDIR_XDG_HOME) && (dir = wp_get_xdg_config_dir ()))
      g_ptr_array_add (dirs, (gpointer)g_strdup (dir));
    if (flags & WP_CDIR_ETC)
      g_ptr_array_add (dirs, (gpointer)g_strdup (WIREPLUMBER_DEFAULT_CONFIG_DIR));
  }

  /* data dirs */
  if ((flags & WP_CDIR_ENV_DATA) && (dir = g_getenv ("WIREPLUMBER_DATA_DIR"))) {
    g_ptr_array_add (dirs, g_canonicalize_filename (dir, NULL));
  } else if (flags & WP_CDIR_USR) {
    g_ptr_array_add (dirs,
      (gpointer)g_canonicalize_filename(WIREPLUMBER_DEFAULT_DATA_DIR, NULL));
  }

  return g_steal_pointer (&dirs);
}

/*!
 * \brief Returns the full path of \a filename as found in
 * the hierarchy of configuration and data directories.
 * \returns An allocated string with the configuration file path or NULL if
 * the file was not found.
 */
gchar *
wp_find_config_file (const gchar *filename, const char *subdir)
{
  g_autoptr(GPtrArray) dirs = lookup_dirs (WP_CDIR_SET_USER);

  if (g_path_is_absolute (filename))
    return g_strdup (filename);

  for (guint i = 0; i < dirs->len; i++) {
    gchar *path = check_path (g_ptr_array_index (dirs, i),
                              subdir, filename);
    if (path)
      return path;
  }

  return NULL;
}

/*!
 * \brief Returns the full path of \a filename as found in
 * the hierarchy of system-only configuration and data directories.
 * \returns An allocated string with the configuration file path or NULL if
 * the file was not found.
 */
gchar *
wp_find_sysconfig_file (const gchar *filename, const char *subdir)
{
  g_autoptr(GPtrArray) dirs = lookup_dirs (WP_CDIR_SET_SYSTEM);

  if (g_path_is_absolute (filename))
    return g_strdup (filename);

  for (guint i = 0; i < dirs->len; i++) {
    gchar *path = check_path (g_ptr_array_index (dirs, i),
                              subdir, filename);
    if (path)
      return path;
  }

  return NULL;
}

/**
 * \brief Iterates over configuration files in the \a subdir and calls the
 * \a func for each file.
 *
 * Files are sorted across the hierarchy of configuration and data
 * directories with files in higher-priority directories shadowing files in
 * lower-priority directories. Files are only checked for existence, a
 * caller must be able to handle read errors.
 *
 * If the \a func returns a negative errno the iteration stops and that
 * errno is returned to the caller. The \a func should set \a error on
 * failure.
 *
 * Note that \a func is called for directories too, it is the responsibility
 * of the caller to ignore or recurse into those.
 *
 * \param subdir The name of the subdirectory to search for in the
 * configuration directories
 * \param suffix The filename suffix, NULL matches all entries
 * \param func The callback to invoke for each file.
 * \param user_data Passed through to \a func
 * \param error Passed through to \a func
 *
 * \return the number of files on success or a negative errno on failure
 */
int
wp_iter_config_files (const gchar *subdir, const gchar *suffix,
                      wp_file_iter_func func, gpointer user_data,
                      GError **error)
{
  g_autoptr(GHashTable) ht = g_hash_table_new_full (g_str_hash, g_str_equal,
                               g_free, g_free);
  g_autoptr(GPtrArray) dirs = NULL;
  gint count = 0;

  if (subdir == NULL)
    subdir = ".";

  /* Note: this list is highest-priority first */
  dirs = lookup_dirs (WP_CDIR_SET_USER);

  /* Store all filenames with their full path in the hashtable, overriding
   * previous values. We need to run backwards through the list for that */
  for (guint i = dirs->len; i > 0; i--) {
    g_autofree gchar *dirpath = g_build_filename (g_ptr_array_index (dirs, i - 1),
                                  subdir, NULL);
    g_autoptr(GDir) dir = g_dir_open (dirpath, 0, NULL);

    wp_trace ("searching config dir: %s", dirpath);

    if (dir) {
      const gchar *filename;
      while ((filename = g_dir_read_name (dir))) {
        if (suffix && !g_str_has_suffix (filename, suffix))
          continue;

        g_hash_table_replace (ht, g_strdup (filename),
          g_build_filename (dirpath, filename, NULL));
      }
    }
  }

  if (g_hash_table_size (ht) == 0)
    return 0;

  /* Sort by filename */
  g_autoptr(GList) keys = g_hash_table_get_keys (ht);
  keys = g_list_sort (keys, (GCompareFunc)g_strcmp0);

  /* Now we have our filenames in a sorted order so we can call the callback */
  for (GList *elem = g_list_first (keys); elem; elem = g_list_next (elem)) {
    const gchar *path = g_hash_table_lookup (ht, elem->data);
    gint rc = func (path, user_data, error);

    if (rc < 0)
      return rc;

    count++;
  }

  return count;
}

/*! \} */
