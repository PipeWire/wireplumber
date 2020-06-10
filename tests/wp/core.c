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
  g_signal_connect (f->base.core, "disconnected",
      G_CALLBACK (expect_disconnected), f);
  g_signal_connect (f->om, "object-added",
      G_CALLBACK (expect_object_added), f);

  wp_object_manager_add_interest (f->om, WP_TYPE_CLIENT, NULL);
  wp_core_install_object_manager (f->base.core, f->om);

  /* connect */
  g_assert_true (wp_core_connect (f->base.core));
  g_assert_true (wp_core_is_connected (f->base.core));

  /* wait for the object manager to collect the client proxy */
  g_main_loop_run (f->base.loop);
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 1);

  /* destroy the server and wait for the disconnected signal */
  wp_test_server_teardown (&f->base.server);
  g_main_loop_run (f->base.loop);
  g_assert_true (f->disconnected);

  g_assert_false (wp_core_is_connected (f->base.core));
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 0);
}

static void
test_core_client_disconnected (TestFixture *f, gconstpointer data)
{
  g_signal_connect (f->base.core, "disconnected",
      G_CALLBACK (expect_disconnected), f);
  g_signal_connect (f->om, "object-added",
      G_CALLBACK (expect_object_added), f);

  wp_object_manager_add_interest (f->om, WP_TYPE_CLIENT, NULL);
  wp_core_install_object_manager (f->base.core, f->om);

  /* connect */
  g_assert_true (wp_core_connect (f->base.core));
  g_assert_true (wp_core_is_connected (f->base.core));

  /* wait for the object manager to collect the client proxy */
  g_main_loop_run (f->base.loop);
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 1);

  /* disconnect and expect the disconnected signal */
  wp_core_disconnect (f->base.core);
  g_assert_true (f->disconnected);

  g_assert_false (wp_core_is_connected (f->base.core));
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 0);
}

static void
test_core_clone (TestFixture *f, gconstpointer data)
{
  g_assert_false (wp_core_is_connected (f->base.core));

  /* clone */
  g_autoptr (WpCore) clone = wp_core_clone (f->base.core);
  g_assert_nonnull (clone);
  g_assert_false (wp_core_is_connected (clone));

  /* connect clone */
  g_assert_true (wp_core_connect (clone));
  g_assert_true (wp_core_is_connected (clone));
  g_assert_false (wp_core_is_connected (f->base.core));

  /* connect core */
  g_assert_true (wp_core_connect (f->base.core));
  g_assert_true (wp_core_is_connected (clone));
  g_assert_true (wp_core_is_connected (f->base.core));

  /* disconnect clone */
  wp_core_disconnect (clone);
  g_assert_false (wp_core_is_connected (clone));
  g_assert_true (wp_core_is_connected (f->base.core));

  /* disconnect core */
  wp_core_disconnect (f->base.core);
  g_assert_false (wp_core_is_connected (f->base.core));
  g_assert_false (wp_core_is_connected (clone));
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
  g_test_add ("/wp/core/cline", TestFixture, NULL,
      test_core_setup, test_core_clone, test_core_teardown);

  return g_test_run ();
}
