/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "base-dirs.h"
#include "log.h"
#include "wpversion.h"
#include "wpbuildbasedirs.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-base-dirs")

/*!
 * \defgroup wpbasedirs Base Directories File Lookup
 */

/* Returns /basedir/subdir/filename, with filename treated as a module
 * if WP_BASE_DIRS_FLAG_MODULE is set.
 * The basedir is assumed to be either an absolute path or NULL.
 * The subdir is assumed to be a path relative to basedir or NULL.
 */
static gchar *
make_path (guint flags, const gchar *basedir, const gchar *subdir,
    const gchar *filename)
{
  g_autofree gchar *full_basedir = NULL;
  g_autofree gchar *full_filename = NULL;

  /* merge subdir into basedir, if necessary */
  if (subdir) {
    full_basedir = g_canonicalize_filename (subdir, basedir);
    basedir = full_basedir;
  }

  if (flags & WP_BASE_DIRS_FLAG_MODULE) {
    g_autofree gchar *basename = g_path_get_basename (filename);
    g_autofree gchar *dirname = g_path_get_dirname (filename);
    const gchar *prefix = "";
    const gchar *suffix = "";
    if (!g_str_has_prefix (basename, "lib"))
      prefix = "lib";
    if (!g_str_has_suffix (basename, ".so"))
      suffix = ".so";
    full_filename = g_strconcat (dirname, G_DIR_SEPARATOR_S,
                                 prefix, basename, suffix, NULL);
    filename = full_filename;
  }

  return g_canonicalize_filename (filename, basedir);
}

static GPtrArray *
lookup_dirs (guint flags, gboolean is_absolute)
{
  g_autoptr(GPtrArray) dirs = g_ptr_array_new_with_free_func (g_free);
  const gchar *dir;
  const gchar *subdir =
      (flags & WP_BASE_DIRS_FLAG_SUBDIR_WIREPLUMBER) ? "wireplumber" : ".";

  /* Compile the list of lookup directories in priority order */
  if (is_absolute) {
    g_ptr_array_add (dirs, NULL);
  }
  else if ((flags & WP_BASE_DIRS_ENV_CONFIG) &&
      (dir = g_getenv ("WIREPLUMBER_CONFIG_DIR"))) {
    g_auto (GStrv) env_dirs = g_strsplit (dir, G_SEARCHPATH_SEPARATOR_S, 0);
    for (guint i = 0; env_dirs[i]; i++) {
      g_ptr_array_add (dirs, g_canonicalize_filename (env_dirs[i], NULL));
    }
  }
  else if ((flags & WP_BASE_DIRS_ENV_DATA) &&
      (dir = g_getenv ("WIREPLUMBER_DATA_DIR"))) {
    g_auto (GStrv) env_dirs = g_strsplit (dir, G_SEARCHPATH_SEPARATOR_S, 0);
    for (guint i = 0; env_dirs[i]; i++) {
      g_ptr_array_add (dirs, g_canonicalize_filename (env_dirs[i], NULL));
    }
  }
  else if ((flags & WP_BASE_DIRS_ENV_MODULE) &&
      (dir = g_getenv ("WIREPLUMBER_MODULE_DIR"))) {
    g_auto (GStrv) env_dirs = g_strsplit (dir, G_SEARCHPATH_SEPARATOR_S, 0);
    for (guint i = 0; env_dirs[i]; i++) {
      g_ptr_array_add (dirs, g_canonicalize_filename (env_dirs[i], NULL));
    }
  }
  else {
    if (flags & WP_BASE_DIRS_XDG_CONFIG_HOME) {
      dir = g_get_user_config_dir ();
      if (G_LIKELY (g_path_is_absolute (dir)))
        g_ptr_array_add (dirs, g_canonicalize_filename (subdir, dir));
    }
    if (flags & WP_BASE_DIRS_XDG_DATA_HOME) {
      dir = g_get_user_data_dir ();
      if (G_LIKELY (g_path_is_absolute (dir)))
        g_ptr_array_add (dirs, g_canonicalize_filename (subdir, dir));
    }
    if (flags & WP_BASE_DIRS_XDG_CONFIG_DIRS) {
      const gchar * const *xdg_dirs = g_get_system_config_dirs ();
      for (guint i = 0; xdg_dirs[i]; i++) {
        if (G_LIKELY (g_path_is_absolute (xdg_dirs[i])))
          g_ptr_array_add (dirs, g_canonicalize_filename (subdir, xdg_dirs[i]));
      }
    }
    if (flags & WP_BASE_DIRS_BUILD_SYSCONFDIR) {
      g_ptr_array_add (dirs, g_canonicalize_filename (subdir, BUILD_SYSCONFDIR));
    }
    if (flags & WP_BASE_DIRS_XDG_DATA_DIRS) {
      const gchar * const *xdg_dirs = g_get_system_data_dirs ();
      for (guint i = 0; xdg_dirs[i]; i++) {
        if (G_LIKELY (g_path_is_absolute (xdg_dirs[i])))
          g_ptr_array_add (dirs, g_canonicalize_filename (subdir, xdg_dirs[i]));
      }
    }
    if (flags & WP_BASE_DIRS_BUILD_DATADIR) {
      g_ptr_array_add (dirs, g_canonicalize_filename (subdir, BUILD_DATADIR));
    }
    if (flags & WP_BASE_DIRS_BUILD_LIBDIR) {
      subdir = (flags & WP_BASE_DIRS_FLAG_SUBDIR_WIREPLUMBER) ?
          "wireplumber-" WIREPLUMBER_API_VERSION : ".";
      g_ptr_array_add (dirs, g_canonicalize_filename (subdir, BUILD_LIBDIR));
    }
  }

  return g_steal_pointer (&dirs);
}

/*!
 * \brief Searches for \a filename in the hierarchy of directories specified
 * by the \a flags parameter
 *
 * Returns the highest priority file found in the hierarchy of directories
 * specified by the \a flags parameter. The \a subdir parameter is the name
 * of the subdirectory to search in, inside the specified directories. If
 * \a subdir is NULL, the base path of each directory is used.
 *
 * The \a filename parameter is the name of the file to search for. If the
 * file is found, its full path is returned. If the file is not found, NULL
 * is returned. The file is considered found if it is a regular file.
 *
 * If the \a filename is an absolute path, it is tested for existence and
 * returned as is, ignoring the lookup directories in \a flags as well as
 * the \a subdir parameter.
 *
 * \ingroup wpbasedirs
 * \param flags flags to specify the directories to look into and other
 *   options specific to the kind of file being looked up
 * \param subdir (nullable): the name of the subdirectory to search in,
 *   inside the specified directories
 * \param filename the name of the file to search for
 * \returns (transfer full) (nullable): A newly allocated string with the
 *   absolute, canonicalized file path, or NULL if the file was not found.
 * \since 0.5.0
 */
gchar *
wp_base_dirs_find_file (WpBaseDirsFlags flags, const gchar * subdir,
    const gchar * filename)
{
  gboolean is_absolute = g_path_is_absolute (filename);
  g_autoptr (GPtrArray) dir_paths = lookup_dirs (flags, is_absolute);
  gchar *ret = NULL;

  /* ignore the subdir if filename is absolute */
  if (is_absolute)
    subdir = NULL;

  for (guint i = 0; i < dir_paths->len; i++) {
    g_autofree gchar *path = make_path (flags, g_ptr_array_index (dir_paths, i),
                                        subdir, filename);
    wp_trace ("test file: %s", path);
    if (g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
      ret = g_steal_pointer (&path);
      break;
    }
  }

  wp_debug ("lookup '%s', return: %s", filename, ret);
  return ret;
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
 * \brief Creates an iterator to iterate over all files that match \a suffix
 * within the \a subdir of the directories specified in \a flags
 *
 * The \a subdir parameter is the name of the subdirectory to search in,
 * inside the directories specified by \a flags. If \a subdir is NULL,
 * the base path of each directory is used. If \a subdir is an absolute path,
 * files are only looked up in that directory and the directories in \a flags
 * are ignored.
 *
 * The \a suffix parameter is the filename suffix to match. If \a suffix is
 * NULL, all files are matched.
 *
 * The iterator will iterate over the absolute paths of all the files
 * files found, in the order of priority of the directories, starting from
 * the lowest priority directory (e.g. /usr/share/wireplumber) and ending
 * with the highest priority directory (e.g. $XDG_CONFIG_HOME/wireplumber).
 * Files within each directory are also sorted by filename.
 *
 * \ingroup wpbasedirs
 * \param flags flags to specify the directories to look into and other
 *   options specific to the kind of file being looked up
 * \param subdir (nullable): the name of the subdirectory to search in,
 *   inside the configuration directories
 * \param suffix (nullable): The filename suffix, NULL matches all entries
 * \returns (transfer full): a new iterator iterating over strings which are
 *   absolute & canonicalized paths to the files found
 * \since 0.5.0
 */
WpIterator *
wp_base_dirs_new_files_iterator (WpBaseDirsFlags flags,
    const gchar * subdir, const gchar * suffix)
{
  g_autoptr (GArray) items =
      g_array_new (FALSE, FALSE, sizeof (struct conffile_iterator_item));
  g_autoptr (GPtrArray) dir_paths = NULL;

  g_array_set_clear_func (items, (GDestroyNotify) conffile_iterator_item_clear);

  if (subdir == NULL)
    subdir = ".";

  /* Note: this list is highest-priority first */
  dir_paths = lookup_dirs (flags, g_path_is_absolute (subdir));

  /* Run backwards through the list to get files in lowest-priority-first order */
  for (guint i = dir_paths->len; i > 0; i--) {
    g_autofree gchar *dirpath =
        g_canonicalize_filename (subdir, g_ptr_array_index (dir_paths, i - 1));
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
        g_autofree gchar *path = make_path (flags, dirpath, NULL, filename);
        if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
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
