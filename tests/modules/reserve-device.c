/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
  GTestDBus *test_dbus;
  WpPlugin *rd_plugin_1;
  WpPlugin *rd_plugin_2;
  gint expected_rd1_state;
  gint expected_rd2_state;
} RdTestFixture;

static void
test_rd_setup (RdTestFixture *f, gconstpointer data)
{
  wp_base_test_fixture_setup (&f->base,
      WP_BASE_TEST_FLAG_CLIENT_CORE | WP_BASE_TEST_FLAG_DONT_CONNECT);

  f->test_dbus = g_test_dbus_new (G_TEST_DBUS_NONE);
  g_test_dbus_up (f->test_dbus);

  {
    g_autoptr (GError) error = NULL;
    WpModule *module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-reserve-device", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
  }
  {
    g_autoptr (GError) error = NULL;
    WpModule *module = wp_module_load (f->base.client_core, "C",
        "libwireplumber-module-reserve-device", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
  }

  f->rd_plugin_1 = wp_plugin_find (f->base.core, "reserve-device");
  g_assert_nonnull (f->rd_plugin_1);

  f->rd_plugin_2 = wp_plugin_find (f->base.client_core, "reserve-device");
  g_assert_nonnull (f->rd_plugin_2);
}

static void
test_rd_teardown (RdTestFixture *f, gconstpointer data)
{
  g_clear_object (&f->rd_plugin_1);
  g_clear_object (&f->rd_plugin_2);
  g_test_dbus_down (f->test_dbus);
  g_clear_object (&f->test_dbus);
  wp_base_test_fixture_teardown (&f->base);
}

static void
ensure_plugins_stable_state (GObject * obj, GParamSpec * spec, RdTestFixture *f)
{
  gint state1 = 0, state2 = 0;
  g_object_get (f->rd_plugin_1, "state", &state1, NULL);
  g_object_get (f->rd_plugin_2, "state", &state2, NULL);
  if (state1 != 1 && state2 != 1 && state1 == state2)
    g_main_loop_quit (f->base.loop);
}

static void
test_rd_plugin (RdTestFixture *f, gconstpointer data)
{
  GObject *rd1 = NULL, *rd2 = NULL, *rd_video = NULL, *tmp = NULL;
  gint state = 0xffff;
  gchar *str;

  g_object_get (f->rd_plugin_1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 0);
  g_object_get (f->rd_plugin_2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 0);

  wp_plugin_activate (f->rd_plugin_1);
  wp_plugin_activate (f->rd_plugin_2);

  g_object_get (f->rd_plugin_1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 1);
  g_object_get (f->rd_plugin_2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 1);

  g_signal_connect (f->rd_plugin_1, "notify::state",
      G_CALLBACK (ensure_plugins_stable_state), f);
  g_signal_connect (f->rd_plugin_2, "notify::state",
      G_CALLBACK (ensure_plugins_stable_state), f);
  g_main_loop_run (f->base.loop);

  g_object_get (f->rd_plugin_1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 2);
  g_object_get (f->rd_plugin_2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 2);

  g_signal_emit_by_name (f->rd_plugin_1, "create-reservation",
      "Audio0", "WirePlumber", "hw:0,0", 10, &rd1);
  g_assert_nonnull (rd1);
  g_signal_emit_by_name (f->rd_plugin_2, "create-reservation",
      "Audio0", "Other Server", "hw:0,0", 15, &rd2);
  g_assert_nonnull (rd2);
  g_signal_emit_by_name (f->rd_plugin_1, "create-reservation",
      "Video0", "WirePlumber", "/dev/video0", 10, &rd_video);
  g_assert_nonnull (rd_video);

  g_signal_emit_by_name (f->rd_plugin_1, "get-reservation", "Video1", &tmp);
  g_assert_null (tmp);
  g_signal_emit_by_name (f->rd_plugin_2, "get-reservation", "Video0", &tmp);
  g_assert_null (tmp);

  g_signal_emit_by_name (f->rd_plugin_1, "get-reservation", "Audio0", &tmp);
  g_assert_nonnull (tmp);
  g_assert_true (tmp == rd1);
  g_clear_object (&tmp);

  g_object_get (rd1, "name", &str, NULL);
  g_assert_cmpstr (str, ==, "Audio0");
  g_free (str);
  g_object_get (rd2, "name", &str, NULL);
  g_assert_cmpstr (str, ==, "Audio0");
  g_free (str);
  g_object_get (rd_video, "name", &str, NULL);
  g_assert_cmpstr (str, ==, "Video0");
  g_free (str);
  g_object_get (rd1, "application-name", &str, NULL);
  g_assert_cmpstr (str, ==, "WirePlumber");
  g_free (str);
  g_object_get (rd1, "application-device-name", &str, NULL);
  g_assert_cmpstr (str, ==, "hw:0,0");
  g_free (str);
  g_object_get (rd1, "priority", &state, NULL);
  g_assert_cmpint (state, ==, 10);
  g_object_get (rd2, "priority", &state, NULL);
  g_assert_cmpint (state, ==, 15);

  g_signal_emit_by_name (f->rd_plugin_1, "destroy-reservation", "Audio0");
  g_signal_emit_by_name (f->rd_plugin_1, "get-reservation", "Audio0", &tmp);
  g_assert_null (tmp);
  g_signal_emit_by_name (f->rd_plugin_2, "get-reservation", "Audio0", &tmp);
  g_assert_nonnull (tmp);
  g_assert_true (tmp == rd2);
  g_clear_object (&tmp);
  g_clear_object (&rd2);
  g_clear_object (&rd1);
  g_clear_object (&rd_video);

  wp_plugin_deactivate (f->rd_plugin_1);
  wp_plugin_deactivate (f->rd_plugin_2);

  g_object_get (f->rd_plugin_1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 0);
  g_object_get (f->rd_plugin_2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 0);
}

static void
test_rd_conn_closed (RdTestFixture *f, gconstpointer data)
{
  GObject *rd1 = NULL;
  gint state = 0xffff;

  g_object_get (f->rd_plugin_1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 0);
  g_object_get (f->rd_plugin_2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 0);

  wp_plugin_activate (f->rd_plugin_1);
  wp_plugin_activate (f->rd_plugin_2);

  g_object_get (f->rd_plugin_1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 1);
  g_object_get (f->rd_plugin_2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 1);

  g_signal_connect (f->rd_plugin_1, "notify::state",
      G_CALLBACK (ensure_plugins_stable_state), f);
  g_signal_connect (f->rd_plugin_2, "notify::state",
      G_CALLBACK (ensure_plugins_stable_state), f);
  g_main_loop_run (f->base.loop);

  g_object_get (f->rd_plugin_1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 2);
  g_object_get (f->rd_plugin_2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 2);

  g_signal_emit_by_name (f->rd_plugin_1, "create-reservation",
      "Audio0", "WirePlumber", "hw:0,0", 10, &rd1);
  g_assert_nonnull (rd1);
  g_clear_object (&rd1);

  /* stop the bus, expect the connections to close
     and state to go back to CLOSED */
  g_test_dbus_stop (f->test_dbus);
  g_main_loop_run (f->base.loop);

  g_object_get (f->rd_plugin_1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 0);
  g_object_get (f->rd_plugin_2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 0);

  g_signal_emit_by_name (f->rd_plugin_1, "get-reservation", "Audio0", &rd1);
  g_assert_null (rd1);
}

static void
expect_rd1_state (GObject * rd, GParamSpec * spec, RdTestFixture *f)
{
  gint state;
  g_object_get (rd, "state", &state, NULL);
  if (state == f->expected_rd1_state)
    g_main_loop_quit (f->base.loop);
}

static void
expect_rd2_state (GObject * rd, GParamSpec * spec, RdTestFixture *f)
{
  gint state;
  g_object_get (rd, "state", &state, NULL);
  if (state == f->expected_rd2_state)
    g_main_loop_quit (f->base.loop);
}

static void
handle_release_requested (GObject * rd, gboolean forced, RdTestFixture *f)
{
  gint state = 0xffff;
  g_signal_emit_by_name (rd, "release");
  g_object_get (rd, "state", &state, NULL);
  g_assert_cmpint (state, ==, 2);
  g_main_loop_quit (f->base.loop);
}

static void
test_rd_acquire_release (RdTestFixture *f, gconstpointer data)
{
  GObject *rd1 = NULL, *rd2 = NULL;
  gint state = 0xffff;
  gchar *str = NULL;

  g_object_get (f->rd_plugin_1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 0);
  g_object_get (f->rd_plugin_2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 0);

  wp_plugin_activate (f->rd_plugin_1);
  wp_plugin_activate (f->rd_plugin_2);

  g_object_get (f->rd_plugin_1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 1);
  g_object_get (f->rd_plugin_2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 1);

  g_signal_connect (f->rd_plugin_1, "notify::state",
      G_CALLBACK (ensure_plugins_stable_state), f);
  g_signal_connect (f->rd_plugin_2, "notify::state",
      G_CALLBACK (ensure_plugins_stable_state), f);
  g_main_loop_run (f->base.loop);

  g_object_get (f->rd_plugin_1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 2);
  g_object_get (f->rd_plugin_2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 2);

  g_signal_emit_by_name (f->rd_plugin_1, "create-reservation",
      "Audio0", "WirePlumber", "hw:0,0", 10, &rd1);
  g_assert_nonnull (rd1);
  g_signal_emit_by_name (f->rd_plugin_2, "create-reservation",
      "Audio0", "Other Server", "hw:0,0", 15, &rd2);
  g_assert_nonnull (rd2);

  g_signal_connect (rd1, "notify::state", G_CALLBACK (expect_rd1_state), f);
  g_signal_connect (rd2, "notify::state", G_CALLBACK (expect_rd2_state), f);

  /* acquire */
  wp_info ("rd1 acquire");

  f->expected_rd1_state = 3;
  g_signal_emit_by_name (rd1, "acquire");
  g_main_loop_run (f->base.loop);
  g_object_get (rd1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 3);
  g_object_get (rd1, "owner-application-name", &str, NULL);
  g_assert_cmpstr (str, ==, "WirePlumber");
  g_free (str);

  g_signal_connect (rd1, "release-requested",
      G_CALLBACK (handle_release_requested), f);

  /* acquire with higher priority */
  wp_info ("rd2 acquire, higher prio");
  g_signal_emit_by_name (rd2, "acquire");

  /* rd1 is now released */
  g_main_loop_run (f->base.loop);
  g_object_get (rd1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 2);

  /* rd2 acquired */
  f->expected_rd2_state = 3;
  g_main_loop_run (f->base.loop);
  g_object_get (rd2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 3);

  /* rd1 busy */
  g_signal_connect_swapped (rd1, "notify::owner-application-name",
      G_CALLBACK (g_main_loop_quit), f->base.loop);
  g_main_loop_run (f->base.loop);
  g_object_get (rd1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 1);
  g_object_get (rd1, "owner-application-name", &str, NULL);
  g_assert_cmpstr (str, ==, "Other Server");
  g_free (str);
  g_signal_handlers_disconnect_by_func (rd1, G_CALLBACK (g_main_loop_quit),
      f->base.loop);

  /* try to acquire back with lower priority */
  wp_info ("rd1 acquire, lower prio");
  g_signal_emit_by_name (rd1, "acquire");

  /* ... expect this to fail */
  f->expected_rd1_state = 1;
  g_main_loop_run (f->base.loop);
  g_object_get (rd1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 1);

  g_object_get (rd1, "owner-application-name", &str, NULL);
  g_assert_cmpstr (str, ==, "Other Server");
  g_free (str);

  /* release */
  wp_info ("rd2 release");
  g_signal_emit_by_name (rd2, "release");
  g_object_get (rd2, "state", &state, NULL);
  g_assert_cmpint (state, ==, 2);

  f->expected_rd1_state = 2;
  g_main_loop_run (f->base.loop);
  g_object_get (rd1, "state", &state, NULL);
  g_assert_cmpint (state, ==, 2);

  g_object_get (rd1, "owner-application-name", &str, NULL);
  g_assert_null (str);

  g_clear_object (&rd1);
  g_clear_object (&rd2);

  wp_plugin_deactivate (f->rd_plugin_1);
  wp_plugin_deactivate (f->rd_plugin_2);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/modules/rd/plugin", RdTestFixture, NULL,
      test_rd_setup, test_rd_plugin, test_rd_teardown);
  g_test_add ("/modules/rd/conn_closed", RdTestFixture, NULL,
      test_rd_setup, test_rd_conn_closed, test_rd_teardown);
  g_test_add ("/modules/rd/acquire_release", RdTestFixture, NULL,
      test_rd_setup, test_rd_acquire_release, test_rd_teardown);

  return g_test_run ();
}
