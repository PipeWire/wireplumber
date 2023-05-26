/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <sys/inotify.h>

#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
  WpPlugin *plugin;
  gchar *path;
  gchar *file;
  gchar *evtype;
} TestFixture;

static void
on_plugin_loaded (WpCore * core, GAsyncResult * res, TestFixture *f)
{
  g_autoptr (GObject) o = NULL;
  GError *error = NULL;

  o = wp_core_load_component_finish (core, res, &error);
  g_assert_nonnull (o);
  g_assert_no_error (error);

  g_main_loop_quit (f->base.loop);
}

static void
test_file_monitor_setup (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&f->base, WP_BASE_TEST_FLAG_DONT_CONNECT);

  wp_core_load_component (f->base.core,
      "libwireplumber-module-file-monitor-api", "module", NULL, NULL,
      (GAsyncReadyCallback) on_plugin_loaded, f);
  g_main_loop_run (f->base.loop);

  f->plugin = wp_plugin_find (f->base.core, "file-monitor-api");
  g_assert_nonnull (f->plugin);

  f->path = g_strdup (g_getenv ("FILE_MONITOR_DIR"));
  g_mkdir_with_parents (f->path, 0700);
}

static void
test_file_monitor_teardown (TestFixture * f, gconstpointer user_data)
{
  g_clear_pointer (&f->evtype, g_free);
  g_clear_pointer (&f->file, g_free);
  g_clear_pointer (&f->path, g_free);
  g_clear_object (&f->plugin);
  wp_base_test_fixture_teardown (&f->base);
}

static void
on_changed (WpPlugin *plugin, const gchar *file, const gchar *old,
    const char *evtype, TestFixture * f)
{
  g_assert_nonnull (file);
  g_assert_nonnull (evtype);
  f->file = g_strdup (file);
  f->evtype = g_strdup (evtype);
  g_main_loop_quit (f->base.loop);
}

static void
test_file_monitor_basic (TestFixture * f, gconstpointer user_data)
{
  gboolean res = FALSE;

  /* delete the 'foo' file if it exists in path */
  g_autofree gchar *filename = g_build_filename (f->path, "foo", NULL);
  (void) remove (filename);

  /* handle changed signal */
  f->file = NULL;
  f->evtype = NULL;
  g_signal_connect (f->plugin, "changed", G_CALLBACK (on_changed), f);

  /* add watch */
  g_signal_emit_by_name (f->plugin, "add-watch", f->path, "m", &res);
  g_assert_true (res);

  /* create the foo file in path */
  int fd = open (filename, O_CREAT | O_EXCL, 0700);
  g_assert_cmpint (fd, >=, 0);

  /* run */
  g_main_loop_run (f->base.loop);
  g_assert_cmpstr (f->file, ==, filename);
  g_assert_cmpstr (f->evtype, ==, "created");

  /* removed watch */
  g_signal_emit_by_name (f->plugin, "remove-watch", f->path);

  /* remove 'foo' */
  close (fd);
  (void) remove (filename);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/modules/file-monitor/basic",
      TestFixture, NULL,
      test_file_monitor_setup,
      test_file_monitor_basic,
      test_file_monitor_teardown);

  return g_test_run ();
}
