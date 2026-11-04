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
  gboolean connection_lost;
  gboolean object_added;
} TestFixture;

static void
test_core_setup (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_DONT_CONNECT);
  /* remove the "disconnected" handler that fails the test */
  g_signal_handlers_disconnect_by_data (
      wp_core_get_connection (self->base.core), &self->base);
  self->om = wp_object_manager_new ();
  self->connection_lost = FALSE;
  self->object_added = FALSE;
}

static void
test_core_teardown (TestFixture *self, gconstpointer user_data)
{
  g_clear_object (&self->om);
  wp_base_test_fixture_teardown (&self->base);
}

static void
on_core_activated (WpObject * core, GAsyncResult * res, TestFixture * f)
{
  g_autoptr (GError) error = NULL;
  g_assert_true (wp_object_activate_finish (core, res, &error));
  g_assert_no_error (error);
  g_main_loop_quit (f->base.loop);
}

static void
connect_core (WpCore * core, TestFixture * f)
{
  wp_object_activate (WP_OBJECT (core), WP_CORE_FEATURE_CONNECTED, NULL,
      (GAsyncReadyCallback) on_core_activated, f);
  g_main_loop_run (f->base.loop);
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
expect_disconnected (WpConnection * connection, TestFixture * f)
{
  f->connection_lost = TRUE;
  g_main_loop_quit (f->base.loop);
}

static void
expect_object_added (WpObjectManager *om, WpProxy *proxy, TestFixture *f)
{
  g_assert_true (WP_IS_CLIENT (proxy));
  f->object_added = TRUE;
}

static void
test_core_server_disconnected (TestFixture *f, gconstpointer data)
{
  g_signal_connect (wp_core_get_connection (f->base.core), "disconnected",
      G_CALLBACK (expect_disconnected), f);
  g_signal_connect (f->om, "object-added",
      G_CALLBACK (expect_object_added), f);

  wp_object_manager_add_interest (f->om, WP_TYPE_CLIENT, NULL);
  wp_core_install_object_manager (f->base.core, f->om);

  /* connect */
  connect_core (f->base.core, f);
  g_assert_true (is_connected (f->base.core));

  /* wait for the object manager to collect the client proxy */
  while (!f->object_added)
    g_main_context_iteration (f->base.context, TRUE);
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 1);

  /* destroy the server and wait for the disconnected signal */
  wp_test_server_teardown (&f->base.server);
  g_main_loop_run (f->base.loop);
  g_assert_true (f->connection_lost);

  /* nothing is disconnected until the core is deactivated */
  g_assert_nonnull (wp_connection_get_pw_core (
      wp_core_get_connection (f->base.core)));
  g_assert_true (is_connected (f->base.core));
  disconnect_core (f->base.core);

  g_assert_false (is_connected (f->base.core));
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 0);
}

static void
test_core_client_disconnected (TestFixture *f, gconstpointer data)
{
  g_signal_connect (f->om, "object-added",
      G_CALLBACK (expect_object_added), f);

  wp_object_manager_add_interest (f->om, WP_TYPE_CLIENT, NULL);
  wp_core_install_object_manager (f->base.core, f->om);

  /* connect */
  connect_core (f->base.core, f);
  g_assert_true (is_connected (f->base.core));

  /* wait for the object manager to collect the client proxy */
  while (!f->object_added)
    g_main_context_iteration (f->base.context, TRUE);
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 1);

  /* disconnect */
  disconnect_core (f->base.core);

  g_assert_false (is_connected (f->base.core));
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 0);
}

static void
test_connection (TestFixture *f, gconstpointer data)
{
  WpConnection *connection = wp_core_get_connection (f->base.core);

  {
    g_autoptr (WpCore) core = wp_connection_get_core (connection);
    g_assert_true (core == f->base.core);
  }

  wp_connection_update_properties (connection,
      wp_properties_new ("wp.test.key", "value", NULL));

  /* updates are kept before connecting and are passed to the connection */
  {
    g_autoptr (WpProperties) props = wp_connection_get_properties (connection);
    g_assert_cmpstr (wp_properties_get (props, "wp.test.key"), ==, "value");
  }

  connect_core (f->base.core, f);

  /* the main context uses the connection of the core */
  {
    WpMainContext *main_context =
        wp_core_get_main_context (f->base.core);
    g_assert_nonnull (wp_connection_get_pw_core (connection));
    g_assert_true (wp_main_context_get_connection (main_context) == connection);
    g_assert_true (wp_context_get_pw_core (WP_CONTEXT (main_context)) ==
        wp_connection_get_pw_core (connection));
  }

  {
    g_autoptr (WpProperties) props = wp_connection_get_properties (connection);
    g_assert_cmpstr (wp_properties_get (props, "wp.test.key"), ==, "value");
  }

  disconnect_core (f->base.core);
  g_assert_false (is_connected (f->base.core));
}

static void
on_sync_done_core_gone (WpConnection * connection, GAsyncResult * res, TestFixture * f)
{
  g_autoptr (GError) error = NULL;
  g_assert_false (wp_connection_sync_finish (connection, res, &error));
  g_assert_nonnull (error);
  g_main_loop_quit (f->base.loop);
}

static void
test_core_destroyed_with_pending_sync (TestFixture *f, gconstpointer data)
{
  connect_core (f->base.core, f);

  /* destroying the context fails the sync, which keeps the connection alive */
  wp_connection_sync (wp_core_get_connection (f->base.core), NULL,
      (GAsyncReadyCallback) on_sync_done_core_gone, f);
  g_clear_object (&f->base.core);
  g_main_loop_run (f->base.loop);
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
  g_test_add ("/wp/core/connection", TestFixture, NULL,
      test_core_setup, test_connection, test_core_teardown);
  g_test_add ("/wp/core/destroyed-with-pending-sync", TestFixture, NULL,
      test_core_setup, test_core_destroyed_with_pending_sync, test_core_teardown);

  return g_test_run ();
}
