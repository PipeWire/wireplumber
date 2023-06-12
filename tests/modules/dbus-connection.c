/* WirePlumber
 *
 * Copyright © 2022-2023 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include "../../modules/dbus-connection-state.h"
#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
  GTestDBus *test_dbus;
} TestFixture;

static void
test_setup (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, 0);
  self->test_dbus = g_test_dbus_new (G_TEST_DBUS_NONE);
  g_test_dbus_up (self->test_dbus);
}

static void
test_teardown (TestFixture *self, gconstpointer user_data)
{
  g_test_dbus_down (self->test_dbus);
  g_clear_object (&self->test_dbus);
  wp_base_test_fixture_teardown (&self->base);
}

static void
on_dbus_state_changed (GObject * dbus, GParamSpec * spec, TestFixture *f)
{
  WpDBusConnectionState state = -1;
  g_object_get (dbus, "state", &state, NULL);
  g_assert_cmpint (state, ==, WP_DBUS_CONNECTION_STATE_CLOSED);

  g_object_set_data (G_OBJECT (dbus), "state-closed", GUINT_TO_POINTER (0x1));
}

static void
on_plugin_loaded (WpCore * core, GAsyncResult * res, TestFixture *f)
{
  gboolean loaded;
  GError *error = NULL;

  loaded = wp_core_load_component_finish (core, res, &error);
  g_assert_no_error (error);
  g_assert_true (loaded);

  g_main_loop_quit (f->base.loop);
}

static void
test_dbus_connection (TestFixture *f, gconstpointer user_data)
{
  g_autoptr (WpPlugin) dbus = NULL;
  g_autoptr (GDBusConnection) conn = NULL;
  WpDBusConnectionState state = -1;

  wp_core_load_component (f->base.core,
      "libwireplumber-module-dbus-connection", "module", NULL, NULL, NULL,
      (GAsyncReadyCallback) on_plugin_loaded, f);
  g_main_loop_run (f->base.loop);

  dbus = wp_plugin_find (f->base.core, "dbus-connection");
  g_assert_nonnull (dbus);

  g_object_get (dbus, "state", &state, NULL);
  g_assert_cmpint (state, ==, WP_DBUS_CONNECTION_STATE_CONNECTED);

  g_object_get (dbus, "connection", &conn, NULL);
  g_assert_nonnull (conn);
  g_clear_object (&conn);

  g_signal_connect (dbus, "notify::state", G_CALLBACK (on_dbus_state_changed),
     f);

  wp_object_deactivate (WP_OBJECT (dbus), WP_PLUGIN_FEATURE_ENABLED);

  // set by on_dbus_state_changed
  g_assert_cmphex (GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (dbus), "state-closed")), ==, 0x1);

  g_object_get (dbus, "connection", &conn, NULL);
  g_assert_null (conn);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/dbus/connection", TestFixture, NULL,
      test_setup, test_dbus_connection, test_teardown);

  return g_test_run ();
}
