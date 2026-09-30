/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"
#include <wp/wp.h>

typedef struct {
  WpBaseTestFixture base;
  WpObjectManager *om;
  gboolean disconnected;
} TestFixture;

static void
test_core_setup (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_DONT_CONNECT);
  /* remove the "disconnected" handler that fails the test */
  g_signal_handlers_disconnect_by_data (self->base.core, &self->base);
  self->om = wp_object_manager_new ();
  self->disconnected = FALSE;
}

static void
test_core_teardown (TestFixture *self, gconstpointer user_data)
{
  g_clear_object (&self->om);
  wp_base_test_fixture_teardown (&self->base);
}

static void
expect_connected (WpCore * core, TestFixture * f)
{
  g_main_loop_quit (f->base.loop);
}

static void
on_core_activated (WpObject * core, GAsyncResult * res, TestFixture * f)
{
  g_autoptr (GError) error = NULL;
  g_assert_true (wp_object_activate_finish (core, res, &error));
  g_assert_no_error (error);
}

static void
connect_core (WpCore * core, TestFixture * f)
{
  wp_object_activate (WP_OBJECT (core), WP_CORE_FEATURE_CONNECTED, NULL,
      (GAsyncReadyCallback) on_core_activated, f);
}

static gboolean
is_connected (WpCore * core)
{
  return wp_object_test_active_features (WP_OBJECT (core),
      WP_CORE_FEATURE_CONNECTED);
}

static void
disconnect_core (WpCore * core)
{
  wp_object_deactivate (WP_OBJECT (core), WP_CORE_FEATURE_CONNECTED);
}

static void
expect_disconnected (WpCore * core, TestFixture * f)
{
  f->disconnected = TRUE;
  g_main_loop_quit (f->base.loop);
}

static void
expect_object_added (WpObjectManager *om, WpProxy *proxy, TestFixture *f)
{
  g_assert_true (WP_IS_CLIENT (proxy));
  g_main_loop_quit (f->base.loop);
}

static void
test_core_server_disconnected (TestFixture *f, gconstpointer data)
{
  g_signal_connect (f->base.core, "connected",
      G_CALLBACK (expect_connected), f);
  g_signal_connect (f->base.core, "disconnected",
      G_CALLBACK (expect_disconnected), f);
  g_signal_connect (f->om, "object-added",
      G_CALLBACK (expect_object_added), f);

  wp_object_manager_add_interest (f->om, WP_TYPE_CLIENT, NULL);
  wp_core_install_object_manager (f->base.core, f->om);

  /* connect */
  connect_core (f->base.core, f);
  g_main_loop_run (f->base.loop);
  g_assert_true (is_connected (f->base.core));

  /* wait for the object manager to collect the client proxy */
  g_main_loop_run (f->base.loop);
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 1);

  /* destroy the server and wait for the disconnected signal */
  wp_test_server_teardown (&f->base.server);
  g_main_loop_run (f->base.loop);
  g_assert_true (f->disconnected);

  g_assert_false (is_connected (f->base.core));
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 0);
}

static void
test_core_client_disconnected (TestFixture *f, gconstpointer data)
{
  g_signal_connect (f->base.core, "connected",
      G_CALLBACK (expect_connected), f);
  g_signal_connect (f->base.core, "disconnected",
      G_CALLBACK (expect_disconnected), f);
  g_signal_connect (f->om, "object-added",
      G_CALLBACK (expect_object_added), f);

  wp_object_manager_add_interest (f->om, WP_TYPE_CLIENT, NULL);
  wp_core_install_object_manager (f->base.core, f->om);

  /* connect */
  connect_core (f->base.core, f);
  g_main_loop_run (f->base.loop);
  g_assert_true (is_connected (f->base.core));

  /* wait for the object manager to collect the client proxy */
  g_main_loop_run (f->base.loop);
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 1);

  /* disconnect and expect the disconnected signal */
  disconnect_core (f->base.core);
  g_assert_true (f->disconnected);

  g_assert_false (is_connected (f->base.core));
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 0);
}

static void
test_core_clone (TestFixture *f, gconstpointer data)
{
  g_assert_false (is_connected (f->base.core));

  /* clone */
  g_autoptr (WpCore) clone = wp_core_clone (f->base.core);
  g_assert_nonnull (clone);
  g_assert_false (is_connected (clone));

  g_signal_connect (f->base.core, "connected",
      G_CALLBACK (expect_connected), f);
  g_signal_connect (clone, "connected",
      G_CALLBACK (expect_connected), f);

  /* connect clone */
  connect_core (clone, f);
  g_main_loop_run (f->base.loop);
  g_assert_true (is_connected (clone));
  g_assert_false (is_connected (f->base.core));

  /* connect core */
  connect_core (f->base.core, f);
  g_main_loop_run (f->base.loop);
  g_assert_true (is_connected (clone));
  g_assert_true (is_connected (f->base.core));

  /* disconnect clone */
  disconnect_core (clone);
  g_assert_false (is_connected (clone));
  g_assert_true (is_connected (f->base.core));

  /* disconnect core */
  disconnect_core (f->base.core);
  g_assert_false (is_connected (f->base.core));
  g_assert_false (is_connected (clone));
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/core/server-disconnected", TestFixture, NULL,
      test_core_setup, test_core_server_disconnected, test_core_teardown);
  g_test_add ("/wp/core/client-disconnected", TestFixture, NULL,
      test_core_setup, test_core_client_disconnected, test_core_teardown);
  g_test_add ("/wp/core/clone", TestFixture, NULL,
      test_core_setup, test_core_clone, test_core_teardown);

  return g_test_run ();
}
