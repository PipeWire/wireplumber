/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
  GTestDBus *test_dbus;
} TestFixture;

static void
test_dbus_setup (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, 0);
  self->test_dbus = g_test_dbus_new (G_TEST_DBUS_NONE);
  g_test_dbus_up (self->test_dbus);
}

static void
test_dbus_teardown (TestFixture *self, gconstpointer user_data)
{
  g_test_dbus_down (self->test_dbus);
  g_clear_object (&self->test_dbus);
  wp_base_test_fixture_teardown (&self->base);
}

static void
test_dbus_basic (TestFixture *f, gconstpointer user_data)
{
  g_autoptr (WpDbus) dbus = wp_dbus_get_instance (f->base.core,
      G_BUS_TYPE_SESSION);
  g_assert_nonnull (dbus);
  g_autoptr (WpDbus) dbus2 = wp_dbus_get_instance (f->base.core,
      G_BUS_TYPE_SESSION);
  g_assert_nonnull (dbus2);

  GBusType bus_type = wp_dbus_get_bus_type (dbus);
  g_assert_true (bus_type == G_BUS_TYPE_SESSION);
  GBusType bus_type2 = wp_dbus_get_bus_type (dbus);
  g_assert_true (bus_type2 == G_BUS_TYPE_SESSION);
  g_assert_true (dbus == dbus2);
}

static void
on_dbus_activated (WpObject * dbus, GAsyncResult * res, TestFixture * f)
{
  g_autoptr (GError) error = NULL;
  if (!wp_object_activate_finish (dbus, res, &error)) {
    wp_critical_object (dbus, "%s", error->message);
    g_main_loop_quit (f->base.loop);
  }
}

static void
on_dbus_state_changed (GObject * obj, GParamSpec * spec, TestFixture *f)
{
  WpDBusState state = wp_dbus_get_state (WP_DBUS (obj));
  if (state == WP_DBUS_STATE_CONNECTED)
    g_main_loop_quit (f->base.loop);
}

static void
test_dbus_activation (TestFixture *f, gconstpointer user_data)
{
  g_autoptr (WpDbus) dbus = wp_dbus_get_instance (
    f->base.core, G_BUS_TYPE_SESSION);
  g_assert_nonnull (dbus);

  wp_object_activate (WP_OBJECT (dbus), WP_DBUS_FEATURE_ENABLED,
      NULL, (GAsyncReadyCallback) on_dbus_activated, f);

  g_signal_connect (dbus, "notify::state", G_CALLBACK (on_dbus_state_changed),
     f);

  g_main_loop_run (f->base.loop);
  g_assert_cmpint (wp_dbus_get_state (WP_DBUS (dbus)), ==,
      WP_DBUS_STATE_CONNECTED);

  wp_object_deactivate (WP_OBJECT (dbus), WP_DBUS_FEATURE_ENABLED);
  g_assert_cmpint (wp_dbus_get_state (WP_DBUS (dbus)), ==,
      WP_DBUS_STATE_CLOSED);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/dbus/basic", TestFixture, NULL,
      test_dbus_setup, test_dbus_basic, test_dbus_teardown);
  g_test_add ("/wp/dbus/activation", TestFixture, NULL,
      test_dbus_setup, test_dbus_activation, test_dbus_teardown);

  return g_test_run ();
}
