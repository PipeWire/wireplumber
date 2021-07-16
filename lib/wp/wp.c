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
 * \brief Gets the WirePlumber module directory
 * \returns WirePlumber's module directory
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
 * \returns WirePlumber's XDG_STATE_HOME subdirectory
 * \since 0.4.2
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
 * \brief Gets the full path to the WirePlumber XDG_CONFIG_HOME subdirectory
 * \returns WirePlumber's XDG_CONFIG_HOME subdirectory
 * \since 0.4.2
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
 * \brief Gets the full path to the WirePlumber configuration directory
 * \returns The WirePlumber configuration directory
 * \deprecated Use wp_find_config_file() instead
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
 * \brief Gets full path to the WirePlumber data directory
 * \returns The WirePlumber data directory
 * \deprecated Use wp_find_sysconfig_file() instead
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

/*! \} */

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

/*
 * Flags to specify lookup directories
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
 * \addtogroup wp
 * \{
 */

/*!
 * \brief Returns the full path of \a filename as found in
 * the hierarchy of configuration and data directories.
 *
 * \param filename the name of the file to search for
 * \param subdir (nullable): the name of the subdirectory to search in,
 *   inside the configuration directories
 * \returns (transfer full): An allocated string with the configuration
 *   file path or NULL if the file was not found.
 * \since 0.4.2
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

 * \param filename the name of the file to search for
 * \param subdir (nullable): the name of the subdirectory to search in,
 *   inside the configuration directories
 * \returns (transfer full): An allocated string with the configuration
 *   file path or NULL if the file was not found.
 * \since 0.4.2
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

/*! \} */

struct conffile_iterator_data
{
  GList *sorted_keys;
  GList *ptr;
  GHashTable *ht;
};

static void
conffile_iterator_reset (WpIterator *it)
{
  struct conffile_iterator_data *it_data = wp_iterator_get_user_data (it);
  it_data->ptr = it_data->sorted_keys;
}

static gboolean
conffile_iterator_next (WpIterator *it, GValue *item)
{
  struct conffile_iterator_data *it_data = wp_iterator_get_user_data (it);

  if (it_data->ptr) {
    const gchar *path = g_hash_table_lookup (it_data->ht, it_data->ptr->data);
    it_data->ptr = g_list_next (it_data->ptr);
    g_value_init (item, G_TYPE_STRING);
    g_value_set_string (item, path);
    return TRUE;
  }
  return FALSE;
}

static gboolean
conffile_iterator_fold (WpIterator *it, WpIteratorFoldFunc func, GValue *ret,
    gpointer data)
{
  struct conffile_iterator_data *it_data = wp_iterator_get_user_data (it);

  for (GList *ptr = it_data->sorted_keys; ptr != NULL; ptr = g_list_next (ptr)) {
    g_auto (GValue) item = G_VALUE_INIT;
    const gchar *path = g_hash_table_lookup (it_data->ht, ptr->data);
    g_value_init (&item, G_TYPE_STRING);
    g_value_set_string (&item, path);
    if (!func (&item, ret, data))
      return FALSE;
  }
  return TRUE;
}

static void
conffile_iterator_finalize (WpIterator *it)
{
  struct conffile_iterator_data *it_data = wp_iterator_get_user_data (it);
  g_list_free (it_data->sorted_keys);
  g_hash_table_unref (it_data->ht);
}

static const WpIteratorMethods conffile_iterator_methods = {
  .version = WP_ITERATOR_METHODS_VERSION,
  .reset = conffile_iterator_reset,
  .next = conffile_iterator_next,
  .fold = conffile_iterator_fold,
  .finalize = conffile_iterator_finalize,
};

/*!
 * \brief Creates an iterator to iterate over configuration files in the
 * \a subdir of the configuration directories
 *
 * Files are sorted across the hierarchy of configuration and data
 * directories with files in higher-priority directories shadowing files in
 * lower-priority directories. Files are only checked for existence, a
 * caller must be able to handle read errors.
 *
 * \note the iterator may contain directories too; it is the responsibility
 * of the caller to ignore or recurse into those.
 *
 * \ingroup wp
 * \param subdir (nullable): the name of the subdirectory to search in,
 *   inside the configuration directories
 * \param suffix (nullable): The filename suffix, NULL matches all entries
 * \returns (transfer full): a new iterator iterating over strings which are
 *   absolute paths to the configuration files found
 * \since 0.4.2
 */
WpIterator *
wp_new_config_files_iterator (const gchar *subdir, const gchar *suffix)
{
  g_autoptr (GHashTable) ht =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_autoptr (GPtrArray) dirs = NULL;

  if (subdir == NULL)
    subdir = ".";

  /* Note: this list is highest-priority first */
  dirs = lookup_dirs (WP_CDIR_SET_USER);

  /* Store all filenames with their full path in the hashtable, overriding
   * previous values. We need to run backwards through the list for that */
  for (guint i = dirs->len; i > 0; i--) {
    g_autofree gchar *dirpath =
        g_build_filename (g_ptr_array_index (dirs, i - 1), subdir, NULL);
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

  /* Sort by filename */
  GList *keys = g_hash_table_get_keys (ht);
  keys = g_list_sort (keys, (GCompareFunc)g_strcmp0);

  /* Construct iterator */
  WpIterator *it = wp_iterator_new (&conffile_iterator_methods,
      sizeof (struct conffile_iterator_data));
  struct conffile_iterator_data *it_data = wp_iterator_get_user_data (it);
  it_data->sorted_keys = keys;
  it_data->ht = g_hash_table_ref (ht);
  return g_steal_pointer (&it);
}
