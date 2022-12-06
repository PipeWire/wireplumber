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
  WpObjectManager *om;
} TestFixture;

static void
test_om_setup (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_CLIENT_CORE);
}

static void
test_om_teardown (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_teardown (&self->base);
}

static void
test_om_interest_on_pw_props (TestFixture *f, gconstpointer user_data)
{
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpObjectManager) om = NULL;

  /* load modules on the server side */
  {
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&f->base.server);

    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), ==, 0);
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-adapter", NULL, NULL));
  }

  /* export node on the client core */
  node = wp_node_new_from_factory (f->base.client_core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", "Test Source",
          "test.answer", "42",
          NULL));
  g_assert_nonnull (node);

  wp_object_activate (WP_OBJECT (node), WP_OBJECT_FEATURES_ALL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* ensure the base core is in sync */
  wp_core_sync (f->base.core, NULL, (GAsyncReadyCallback) test_core_done_cb, f);
  g_main_loop_run (f->base.loop);

  /* request that node from the base core */
  om = wp_object_manager_new ();
  wp_object_manager_add_interest (om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "node.name", "=s", "Test Source",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "test.answer", "=s", "42",
      NULL);
  test_ensure_object_manager_is_installed (om, f->base.core, f->base.loop);

  g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 1);
  g_clear_object (&om);

  /* request "test.answer" to be absent... this will not match */
  om = wp_object_manager_new ();
  wp_object_manager_add_interest (om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "node.name", "=s", "Test Source",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "test.answer", "-",
      NULL);
  test_ensure_object_manager_is_installed (om, f->base.core, f->base.loop);

  g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 0);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/om/interest-on-pw-props", TestFixture, NULL,
      test_om_setup, test_om_interest_on_pw_props, test_om_teardown);

  return g_test_run ();
}
