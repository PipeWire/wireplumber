/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

#include <pipewire/permission.h>

typedef struct {
  WpBaseTestFixture base;
  WpClient *client;
} TestFixture;

static void
test_permission_manager_setup (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpObjectManager) om = NULL;
  guint32 client_id;

  wp_base_test_fixture_setup (&f->base, WP_BASE_TEST_FLAG_CLIENT_CORE);

  /* find the client object of the "client" core on the sm core */
  client_id = wp_connection_get_own_bound_id (
      wp_core_get_connection (f->base.client_core));
  om = wp_object_manager_new ();
  wp_object_manager_add_interest (om, WP_TYPE_CLIENT,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", client_id, NULL);
  wp_object_manager_request_object_features (om, WP_TYPE_CLIENT,
      WP_OBJECT_FEATURES_ALL);
  test_ensure_object_manager_is_installed (om, f->base.core, f->base.loop);

  f->client = wp_object_manager_lookup (om, WP_TYPE_CLIENT, NULL);
  g_assert_nonnull (f->client);
}

static void
test_permission_manager_teardown (TestFixture * f, gconstpointer data)
{
  g_clear_object (&f->client);
  wp_base_test_fixture_teardown (&f->base);
}

static void
on_sync_done (WpConnection * connection, GAsyncResult * res, TestFixture * f)
{
  g_autoptr (GError) error = NULL;
  g_assert_cmpint (wp_connection_sync_finish (connection, res, &error), >=, 0);
  g_assert_no_error (error);
  g_main_loop_quit (f->base.loop);
}

static void
sync_core (TestFixture * f)
{
  wp_connection_sync (wp_core_get_connection (f->base.core), NULL,
      (GAsyncReadyCallback) on_sync_done, f);
  g_main_loop_run (f->base.loop);
}

/* Returns the permissions of the test client on the @global_id object */
static guint32
get_permissions (TestFixture * f, guint32 global_id)
{
  g_autoptr (WpTestServerLocker) lock =
      wp_test_server_locker_new (&f->base.server);
  struct pw_global *global;
  struct pw_impl_client *client;
  const guint32 bits[] = { PW_PERM_R, PW_PERM_W, PW_PERM_X, PW_PERM_M };
  guint32 perms = 0;

  global = pw_context_find_global (f->base.server.context,
      wp_proxy_get_bound_id (WP_PROXY (f->client)));
  g_assert_nonnull (global);
  client = pw_global_get_object (global);

  for (guint i = 0; i < G_N_ELEMENTS (bits); i++)
    if (pw_impl_client_check_permissions (client, global_id, bits[i]) == 0)
      perms |= bits[i];

  return perms;
}

static void
attach_permission_manager (TestFixture * f, guint32 default_perms)
{
  g_autoptr (WpPermissionManager) pm = NULL;
  guint32 client_id = wp_proxy_get_bound_id (WP_PROXY (f->client));

  /* restrict the client like the portal does, keeping the core usable */
  wp_client_update_permissions (f->client, 2,
      PW_ID_ANY, PW_PERM_R, PW_ID_CORE, PW_PERM_ALL);
  sync_core (f);

  /* only the client's own object is of interest */
  pm = wp_permission_manager_new (f->base.core);
  wp_permission_manager_set_default_permissions (pm, default_perms);
  wp_permission_manager_add_interest_match_simple (pm, PW_PERM_ALL,
      wp_object_interest_new (WP_TYPE_CLIENT,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", client_id, NULL));
  wp_object_activate (WP_OBJECT (pm), WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) test_object_activate_finish_cb, &f->base);
  g_main_loop_run (f->base.loop);

  wp_client_attach_permission_manager (f->client, pm);
  sync_core (f);
}

static void
test_permission_manager_invalid_default (TestFixture * f, gconstpointer data)
{
  guint32 client_id = wp_proxy_get_bound_id (WP_PROXY (f->client));
  guint32 sm_id = wp_connection_get_own_bound_id (
      wp_core_get_connection (f->base.core));

  attach_permission_manager (f, PW_PERM_INVALID);

  /* the matched object is updated, PW_ID_ANY is left untouched */
  g_assert_cmphex (get_permissions (f, client_id), ==, PW_PERM_ALL);
  g_assert_cmphex (get_permissions (f, sm_id), ==, PW_PERM_R);
}

static void
test_permission_manager_default (TestFixture * f, gconstpointer data)
{
  guint32 client_id = wp_proxy_get_bound_id (WP_PROXY (f->client));
  guint32 sm_id = wp_connection_get_own_bound_id (
      wp_core_get_connection (f->base.core));

  attach_permission_manager (f, PW_PERM_R | PW_PERM_X);

  /* both the matched object and PW_ID_ANY are updated */
  g_assert_cmphex (get_permissions (f, client_id), ==, PW_PERM_ALL);
  g_assert_cmphex (get_permissions (f, sm_id), ==, PW_PERM_R | PW_PERM_X);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/permission-manager/invalid-default", TestFixture, NULL,
      test_permission_manager_setup,
      test_permission_manager_invalid_default,
      test_permission_manager_teardown);
  g_test_add ("/wp/permission-manager/default", TestFixture, NULL,
      test_permission_manager_setup,
      test_permission_manager_default,
      test_permission_manager_teardown);

  return g_test_run ();
}
