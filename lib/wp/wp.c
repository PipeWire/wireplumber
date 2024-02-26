/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "wp.h"
#include <pipewire/pipewire.h>
#include <libintl.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp")

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
  /* Initialize the logging system */
  wp_log_init (flags);

  wp_info ("WirePlumber " WIREPLUMBER_VERSION " initializing");

  if (flags & WP_INIT_PIPEWIRE)
    pw_init (NULL, NULL);

  if (flags & WP_INIT_SPA_TYPES)
    wp_spa_dynamic_type_init ();

  bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

  /* ensure WpProxy subclasses are loaded, which is needed to be able
    to autodetect the GType of proxies created through wp_proxy_new_global() */
  g_type_ensure (WP_TYPE_CLIENT);
  g_type_ensure (WP_TYPE_DEVICE);
  g_type_ensure (WP_TYPE_LINK);
  g_type_ensure (WP_TYPE_METADATA);
  g_type_ensure (WP_TYPE_NODE);
  g_type_ensure (WP_TYPE_PORT);
  g_type_ensure (WP_TYPE_FACTORY);
}

/*!
 * \brief Gets the WirePlumber library version
 * \returns WirePlumber library version
 *
 * \since 0.4.12
 */
const char *
wp_get_library_version (void)
{
  return WIREPLUMBER_VERSION;
}

/*!
 * \brief Gets the WirePlumber library API version
 * \returns WirePlumber library API version
 *
 * \since 0.4.12
 */
const char *
wp_get_library_api_version (void)
{
  return WIREPLUMBER_API_VERSION;
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

static GPtrArray *
lookup_dirs (guint flags)
{
  g_autoptr(GPtrArray) dirs = g_ptr_array_new_with_free_func (g_free);
  const gchar *dir;

  /* Compile the list of lookup directories in priority order:
   * - environment variables
   * - XDG config directories
   * - /etc/
   * - /usr/share/....
   */
  if (flags & (WP_LOOKUP_DIR_ENV_DATA | WP_LOOKUP_DIR_ENV_TEST_SRCDIR)) {
    if ((flags & WP_LOOKUP_DIR_ENV_DATA) &&
        (dir = g_getenv ("WIREPLUMBER_DATA_DIR")))
      g_ptr_array_add (dirs, g_canonicalize_filename (dir, NULL));

    if ((flags & WP_LOOKUP_DIR_ENV_TEST_SRCDIR) &&
        (dir = g_getenv ("G_TEST_SRCDIR")))
      g_ptr_array_add (dirs, g_canonicalize_filename (dir, NULL));

    if (dirs->len)
      goto done;
  }
  if (flags & WP_LOOKUP_DIR_XDG_CONFIG_HOME) {
    dir = g_get_user_config_dir ();
    g_ptr_array_add (dirs, g_build_filename (dir, "wireplumber", NULL));
  }
  if (flags & WP_LOOKUP_DIR_ETC)
    g_ptr_array_add (dirs,
        g_canonicalize_filename (WIREPLUMBER_DEFAULT_CONFIG_DIR, NULL));
  if (flags & WP_LOOKUP_DIR_PREFIX_SHARE)
    g_ptr_array_add (dirs,
        g_canonicalize_filename(WIREPLUMBER_DEFAULT_DATA_DIR, NULL));

done:
  return g_steal_pointer (&dirs);
}

/*!
 * \brief Returns the full path of \a filename as found in
 * the hierarchy of configuration and data directories.
 *
 * \ingroup wp
 * \param dirs the directories to look into
 * \param filename the name of the file to search for
 * \param subdir (nullable): the name of the subdirectory to search in,
 *   inside the configuration directories
 * \returns (transfer full): An allocated string with the configuration
 *   file path or NULL if the file was not found.
 * \since 0.4.2
 */
gchar *
wp_find_file (WpLookupDirs dirs, const gchar *filename, const gchar *subdir)
{
  g_autoptr(GPtrArray) dir_paths = lookup_dirs (dirs);

  if (g_path_is_absolute (filename))
    return g_strdup (filename);

  for (guint i = 0; i < dir_paths->len; i++) {
    gchar *path = check_path (g_ptr_array_index (dir_paths, i),
                              subdir, filename);
    if (path)
      return path;
  }

  return NULL;
}

struct conffile_iterator_item
{
  gchar *filename;
  gchar *path;
};

static void
conffile_iterator_item_clear (struct conffile_iterator_item *item)
{
  g_free (item->filename);
  g_free (item->path);
}

struct conffile_iterator_data
{
  GArray *items;
  guint idx;
};

static void
conffile_iterator_reset (WpIterator *it)
{
  struct conffile_iterator_data *it_data = wp_iterator_get_user_data (it);
  it_data->idx = 0;
}

static gboolean
conffile_iterator_next (WpIterator *it, GValue *item)
{
  struct conffile_iterator_data *it_data = wp_iterator_get_user_data (it);

  if (it_data->idx < it_data->items->len) {
    const gchar *path = g_array_index (it_data->items,
        struct conffile_iterator_item, it_data->idx).path;
    it_data->idx++;
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

  for (guint i = 0; i < it_data->items->len; i++) {
    g_auto (GValue) item = G_VALUE_INIT;
    const gchar *path = g_array_index (it_data->items,
        struct conffile_iterator_item, i).path;
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
  g_clear_pointer (&it_data->items, g_array_unref);
}

static const WpIteratorMethods conffile_iterator_methods = {
  .version = WP_ITERATOR_METHODS_VERSION,
  .reset = conffile_iterator_reset,
  .next = conffile_iterator_next,
  .fold = conffile_iterator_fold,
  .finalize = conffile_iterator_finalize,
};

static gint
conffile_iterator_item_compare (const struct conffile_iterator_item *a,
    const struct conffile_iterator_item *b)
{
  return g_strcmp0 (a->filename, b->filename);
}

/*!
 * \brief Creates an iterator to iterate over configuration files in the
 * \a subdir of the configuration directories
 *
 * The configuration directories are determined by the \a dirs parameter.
 * The \a subdir parameter is the name of the subdirectory to search in,
 * inside the configuration directories. If \a subdir is NULL, the base path
 * of each configuration directory is used.
 *
 * The \a suffix parameter is the filename suffix to match. If \a suffix is
 * NULL, all files are matched.
 *
 * The iterator will iterate over the absolute paths of the configuration
 * files found, in the order of priority of the directories, starting from
 * the lowest priority directory (e.g. /usr/share/wireplumber) and ending
 * with the highest priority directory (e.g. $XDG_CONFIG_HOME/wireplumber).
 *
 * Files within each directory are also sorted by filename.
 *
 * \ingroup wp
 * \param dirs the directories to look into
 * \param subdir (nullable): the name of the subdirectory to search in,
 *   inside the configuration directories
 * \param suffix (nullable): The filename suffix, NULL matches all entries
 * \returns (transfer full): a new iterator iterating over strings which are
 *   absolute paths to the files found
 * \since 0.4.2
 */
WpIterator *
wp_new_files_iterator (WpLookupDirs dirs, const gchar *subdir,
    const gchar *suffix)
{
  g_autoptr (GArray) items =
      g_array_new (FALSE, FALSE, sizeof (struct conffile_iterator_item));
  g_autoptr (GPtrArray) dir_paths = NULL;

  g_array_set_clear_func (items, (GDestroyNotify) conffile_iterator_item_clear);

  if (subdir == NULL)
    subdir = ".";

  /* Note: this list is highest-priority first */
  dir_paths = lookup_dirs (dirs);

  /* Run backwards through the list to get files in lowest-priority-first order */
  for (guint i = dir_paths->len; i > 0; i--) {
    g_autofree gchar *dirpath =
        g_build_filename (g_ptr_array_index (dir_paths, i - 1), subdir, NULL);
    g_autoptr (GDir) dir = g_dir_open (dirpath, 0, NULL);

    if (dir) {
      g_autoptr (GArray) dir_items = g_array_new (FALSE, FALSE,
          sizeof (struct conffile_iterator_item));

      wp_trace ("searching dir: %s", dirpath);

      /* Store all filenames with their full path in the local array */
      const gchar *filename;
      while ((filename = g_dir_read_name (dir))) {
        if (filename[0] == '.')
          continue;

        if (suffix && !g_str_has_suffix (filename, suffix))
          continue;

        /* verify the file is regular and canonicalize the path */
        g_autofree gchar *path = check_path (dirpath, NULL, filename);
        if (!path)
          continue;

        /* remove item with the same filename from the global items array,
           so that lower priority files can be shadowed */
        for (guint j = 0; j < items->len; j++) {
          struct conffile_iterator_item *item = &g_array_index (items,
              struct conffile_iterator_item, j);
          if (g_strcmp0 (item->filename, filename) == 0) {
            g_array_remove_index (items, j);
            break;
          }
        }

        /* append in the local array */
        g_array_append_val (dir_items, ((struct conffile_iterator_item) {
          .filename = g_strdup (filename),
          .path = g_steal_pointer (&path),
        }));
      }

      /* Sort files of the current dir by filename */
      g_array_sort (dir_items, (GCompareFunc) conffile_iterator_item_compare);

      /* Append the sorted files to the global array */
      g_array_append_vals (items, dir_items->data, dir_items->len);
    }
  }

  /* Construct iterator */
  WpIterator *it = wp_iterator_new (&conffile_iterator_methods,
      sizeof (struct conffile_iterator_data));
  struct conffile_iterator_data *it_data = wp_iterator_get_user_data (it);
  it_data->items = g_steal_pointer (&items);
  it_data->idx = 0;
  return g_steal_pointer (&it);
}
