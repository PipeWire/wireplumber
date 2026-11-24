/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
  /* the object manager that listens for proxies */
  WpObjectManager *om;
  /* the number of objects that wait_for_n_objects() waits for */
  guint n_objects;
} TestFixture;

static void
test_proxy_setup (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, 0);
  self->om = wp_object_manager_new ();
}

/* for tests with a node that another client creates, which this core only
   sees on its registry */
static void
test_proxy_setup_client_core (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_CLIENT_CORE);
  self->om = wp_object_manager_new ();
}

static void
test_proxy_teardown (TestFixture *self, gconstpointer user_data)
{
  g_clear_object (&self->om);
  wp_base_test_fixture_teardown (&self->base);
}

static void
test_proxy_basic_activated (WpObject *proxy, GAsyncResult *res,
    TestFixture *fixture)
{
  g_autoptr (GError) error = NULL;
  g_assert_true (wp_object_activate_finish (proxy, res, &error));
  g_assert_no_error (error);

  g_assert_true (wp_object_get_active_features (proxy) & WP_PROXY_FEATURE_BOUND);
  g_assert_nonnull (wp_proxy_get_pw_proxy (WP_PROXY (proxy)));

  g_main_loop_quit (fixture->base.loop);
}

static void
test_proxy_basic_object_added (WpObjectManager *om, WpGlobalProxy *proxy,
    TestFixture *fixture)
{
  g_assert_nonnull (proxy);
  {
    g_autoptr (WpCore) pcore = NULL;
    g_autoptr (WpCore) omcore = NULL;
    g_object_get (proxy, "core", &pcore, NULL);
    g_object_get (om, "core", &omcore, NULL);
    g_assert_nonnull (pcore);
    g_assert_nonnull (omcore);
    g_assert_true (pcore == omcore);
  }
  g_assert_cmphex (wp_global_proxy_get_permissions (proxy), ==, PW_PERM_ALL);
  g_assert_true (WP_IS_CLIENT (proxy));

  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (proxy)), ==, 0);
  g_assert_null (wp_proxy_get_pw_proxy (WP_PROXY (proxy)));

  {
    g_autoptr (WpProperties) props =
        wp_global_proxy_get_global_properties (proxy);
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, PW_KEY_PROTOCOL), ==,
        "protocol-native");
  }

  wp_object_activate (WP_OBJECT (proxy), WP_PROXY_FEATURE_BOUND, NULL,
      (GAsyncReadyCallback) test_proxy_basic_activated, fixture);
}

static void
test_proxy_basic (TestFixture *fixture, gconstpointer data)
{
  /* our test server should advertise exactly one
   * client: our core; use this to test WpProxy */
  g_signal_connect (fixture->om, "object-added",
      (GCallback) test_proxy_basic_object_added, fixture);

  wp_object_manager_add_interest (fixture->om, WP_TYPE_CLIENT, NULL);
  wp_core_install_object_manager (fixture->base.core, fixture->om);

  g_main_loop_run (fixture->base.loop);
}

static void
test_node_enum_params_done (WpPipewireObject *node, GAsyncResult *res,
    TestFixture *f)
{
  g_autoptr (WpIterator) params = NULL;
  g_autoptr (GError) error = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  guint n_params = 0;

  params = wp_pipewire_object_enum_params_finish (node, res, &error);
  g_assert_no_error (error);
  g_assert_nonnull (params);

  for (; wp_iterator_next (params, &item); g_value_unset (&item)) {
    WpSpaPod *pod = NULL;
    g_assert_cmpuint (G_VALUE_TYPE (&item), ==, WP_TYPE_SPA_POD);
    pod = g_value_get_boxed (&item);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_object (pod));
    g_assert_cmpuint (wp_spa_type_from_name ("Spa:Pod:Object:Param:PropInfo"),
        ==, wp_spa_pod_get_spa_type (pod));
    n_params++;
  }
  g_assert_cmpint (n_params, >, 0);

  g_main_loop_quit (f->base.loop);
}

static void
test_node (TestFixture *f, gconstpointer data)
{
  g_autoptr (WpPipewireObject) proxy = NULL;
  const struct pw_node_info *info;

  /* load audiotestsrc on the server side */
  {
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&f->base.server);

    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), ==, 0);
    if (!test_is_spa_lib_installed (&f->base, "audiotestsrc")) {
      g_test_skip ("The pipewire audiotestsrc factory was not found");
      return;
    }

    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-adapter", NULL, NULL));
  }

  proxy = WP_PIPEWIRE_OBJECT (wp_node_new_from_factory (f->base.core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", "audiotestsrc.adapter",
          NULL)));
  g_assert_nonnull (proxy);
  wp_object_activate (WP_OBJECT (proxy), WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* basic tests */
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (proxy)), ==,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  g_assert_nonnull (wp_proxy_get_pw_proxy (WP_PROXY (proxy)));
  g_assert_true (WP_IS_NODE (proxy));

  /* info */
  {
    info = wp_pipewire_object_get_native_info (proxy);
    g_assert_nonnull (info);
    g_assert_cmpint (wp_proxy_get_bound_id (WP_PROXY (proxy)), ==, info->id);
  }

  /* properties */
  {
    const gchar *id = wp_pipewire_object_get_property (proxy, PW_KEY_OBJECT_ID);
    g_assert_nonnull (id);
    g_assert_cmpint (info->id, ==, atoi(id));
  }
  {
    const char *id;
    g_autoptr (WpProperties) props = NULL;

    props = wp_pipewire_object_get_properties (proxy);
    g_assert_nonnull (props);
    id = wp_properties_get (props, PW_KEY_OBJECT_ID);
    g_assert_nonnull (id);
    g_assert_cmpint (info->id, ==, atoi(id));
  }

  /* param info */
  {
    const gchar *flags_str;
    g_autoptr (GVariant) param_info = wp_pipewire_object_get_param_info (proxy);

    g_assert_nonnull (param_info);
    g_assert_true (g_variant_is_of_type (param_info, G_VARIANT_TYPE ("a{ss}")));
    g_assert_cmpuint (g_variant_n_children (param_info), ==, info->n_params);
    g_assert_true (g_variant_lookup (param_info, "PropInfo", "&s", &flags_str));
    g_assert_cmpstr (flags_str, ==, "r");
    g_assert_true (g_variant_lookup (param_info, "Props", "&s", &flags_str));
    g_assert_cmpstr (flags_str, ==, "rw");
  }

  /* enum params */
  wp_pipewire_object_enum_params (proxy, "PropInfo", NULL, NULL,
      (GAsyncReadyCallback) test_node_enum_params_done, f);
  g_main_loop_run (f->base.loop);
}

static void
activate_error_cb (WpObject * object, GAsyncResult * res,
    WpBaseTestFixture * f)
{
  g_autoptr (GError) error = NULL;
  gboolean augment_ret = wp_object_activate_finish (object, res, &error);
  g_assert_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED);
  g_assert_false (augment_ret);

  g_main_loop_quit (f->loop);
}

static void
test_link_error (TestFixture *f, gconstpointer data)
{
  g_autoptr (WpPipewireObject) proxy = NULL;

  /* load audiotestsrc on the server side */
  {
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&f->base.server);

    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-link-factory", NULL, NULL));
  }

  proxy = WP_PIPEWIRE_OBJECT (wp_link_new_from_factory (f->base.core,
      "link-factory",
      wp_properties_new (
          "link.output.node", "invalid",
          "link.input.node", "invalid",
          NULL)));
  g_assert_nonnull (proxy);
  wp_object_activate (WP_OBJECT (proxy), WP_OBJECT_FEATURES_ALL,
      NULL, (GAsyncReadyCallback) activate_error_cb, f);
  g_main_loop_run (f->base.loop);
}

static void
enum_params_error_cb (WpPipewireObject * object, GAsyncResult * res,
    WpBaseTestFixture * f)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (WpIterator) it =
      wp_pipewire_object_enum_params_finish (object, res, &error);
  g_assert_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED);
  g_assert_null (it);

  g_main_loop_quit (f->loop);
}

static void
test_enum_params_error (TestFixture *f, gconstpointer data)
{
  g_autoptr (WpNode) node = NULL;

  /* load audiotestsrc on the server side */
  {
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&f->base.server);

    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), ==, 0);
    if (!test_is_spa_lib_installed (&f->base, "audiotestsrc")) {
      g_test_skip ("The pipewire audiotestsrc factory was not found");
      return;
    }
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-adapter", NULL, NULL));
  }

  node = wp_node_new_from_factory (f->base.core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", "audiotestsrc",
          NULL));
  g_assert_nonnull (node);

  wp_object_activate (WP_OBJECT (node), WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* EnumRoute doesn't exist on audiotestsrc, obviously */
  wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node), "EnumRoute", NULL,
      NULL, (GAsyncReadyCallback) enum_params_error_cb, f);
  g_main_loop_run (f->base.loop);
}

#define REGISTRY_NODE_NAME "test.registry.node"

static gboolean
load_audiotestsrc_adapter (TestFixture *f)
{
  g_autoptr (WpTestServerLocker) lock =
      wp_test_server_locker_new (&f->base.server);

  g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
          "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), ==, 0);
  if (!test_is_spa_lib_installed (&f->base, "audiotestsrc")) {
    g_test_skip ("The pipewire audiotestsrc factory was not found");
    return FALSE;
  }
  g_assert_nonnull (pw_context_load_module (f->base.server.context,
          "libpipewire-module-adapter", NULL, NULL));
  return TRUE;
}

/* creates a node from the client core and waits until our core knows it */
static WpNode *
create_client_node (TestFixture *f)
{
  g_autoptr (WpNode) node = wp_node_new_from_factory (f->base.client_core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          PW_KEY_NODE_NAME, REGISTRY_NODE_NAME,
          NULL));
  g_assert_nonnull (node);
  wp_object_activate (WP_OBJECT (node), WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, &f->base);
  g_main_loop_run (f->base.loop);

  wp_core_sync (f->base.core, NULL,
      (GAsyncReadyCallback) test_core_done_cb, &f->base);
  g_main_loop_run (f->base.loop);

  return g_steal_pointer (&node);
}

static void
install_node_om (TestFixture *f, WpObjectManager *om)
{
  wp_object_manager_add_interest (om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_NODE_NAME, "=s",
      REGISTRY_NODE_NAME, NULL);
  test_ensure_object_manager_is_installed (om, f->base.core, f->base.loop);
}

static void
on_objects_changed (WpObjectManager *om, TestFixture *f)
{
  if (wp_object_manager_get_n_objects (om) == f->n_objects)
    g_main_loop_quit (f->base.loop);
}

static void
wait_for_n_objects (TestFixture *f, guint n_objects)
{
  if (wp_object_manager_get_n_objects (f->om) != n_objects) {
    gulong handler;

    f->n_objects = n_objects;
    handler = g_signal_connect (f->om, "objects-changed",
        G_CALLBACK (on_objects_changed), f);
    g_main_loop_run (f->base.loop);
    g_signal_handler_disconnect (f->om, handler);
  }
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, n_objects);
}

static void
on_core_error (void *data, uint32_t id, int seq, int res, const char *message)
{
  guint *n_errors = data;

  g_test_message ("core error on id %u: %d (%s)", id, res, message);
  (*n_errors)++;
}

static const struct pw_core_events core_error_events = {
  PW_VERSION_CORE_EVENTS,
  .error = on_core_error,
};

/* the global of the proxy is gone: requesting its destruction must neither
   cause an error nor destroy another object */
static void
assert_request_destroy_does_nothing (TestFixture *f, WpGlobalProxy *proxy,
    WpNode *client_node)
{
  struct spa_hook listener = { 0 };
  guint n_errors = 0;

  pw_core_add_listener (wp_core_get_pw_core (f->base.core), &listener,
      &core_error_events, &n_errors);
  wp_global_proxy_request_destroy (proxy);
  wp_core_sync (f->base.core, NULL,
      (GAsyncReadyCallback) test_core_done_cb, &f->base);
  g_main_loop_run (f->base.loop);
  spa_hook_remove (&listener);
  g_assert_cmpuint (n_errors, ==, 0);

  wp_core_sync (f->base.client_core, NULL,
      (GAsyncReadyCallback) test_core_done_cb, &f->base);
  g_main_loop_run (f->base.loop);
  g_assert_true (wp_object_get_active_features (WP_OBJECT (client_node)) &
      WP_PROXY_FEATURE_BOUND);
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 1);
}

static void
test_registry_node_removed_unbound (TestFixture *f, gconstpointer data)
{
  g_autoptr (WpNode) client_node = NULL;
  g_autoptr (WpNode) node = NULL;

  if (!load_audiotestsrc_adapter (f))
    return;

  client_node = create_client_node (f);
  install_node_om (f, f->om);
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 1);
  node = wp_object_manager_lookup (f->om, WP_TYPE_NODE, NULL);
  g_assert_nonnull (node);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (node)), ==, 0);

  /* destroy the node, whose proxy was never bound */
  g_clear_object (&client_node);
  wait_for_n_objects (f, 0);

  client_node = create_client_node (f);
  wait_for_n_objects (f, 1);
  assert_request_destroy_does_nothing (f, WP_GLOBAL_PROXY (node), client_node);
}

/* a new object manager on our core must expose the same node */
static void
assert_node_exposed (TestFixture *f, WpNode *node)
{
  g_autoptr (WpObjectManager) om = wp_object_manager_new ();
  g_autoptr (WpNode) found = NULL;

  install_node_om (f, om);
  g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 1);
  found = wp_object_manager_lookup (om, WP_TYPE_NODE, NULL);
  g_assert_true (found == node);
}

static void
test_registry_node_bind_again (TestFixture *f, gconstpointer data)
{
  g_autoptr (WpNode) client_node = NULL;
  g_autoptr (WpNode) node = NULL;

  if (!load_audiotestsrc_adapter (f))
    return;

  client_node = create_client_node (f);
  install_node_om (f, f->om);
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 1);
  node = wp_object_manager_lookup (f->om, WP_TYPE_NODE, NULL);
  g_assert_nonnull (node);

  wp_object_activate (WP_OBJECT (node), WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, &f->base);
  g_main_loop_run (f->base.loop);

  wp_object_deactivate (WP_OBJECT (node), WP_PROXY_FEATURE_BOUND);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (node)), ==, 0);
  g_assert_null (wp_proxy_get_pw_proxy (WP_PROXY (node)));

  /* the node is still on the registry, so it can be bound again */
  wp_object_activate (WP_OBJECT (node), WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, &f->base);
  g_main_loop_run (f->base.loop);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (node)), ==,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  g_assert_cmpuint (wp_proxy_get_bound_id (WP_PROXY (node)), ==,
      wp_proxy_get_bound_id (WP_PROXY (client_node)));

  assert_node_exposed (f, node);
}

static void
activate_bind_error_cb (WpObject * object, GAsyncResult * res,
    WpBaseTestFixture * f)
{
  g_autoptr (GError) error = NULL;
  g_assert_false (wp_object_activate_finish (object, res, &error));
  g_assert_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT);

  g_main_loop_quit (f->loop);
}

static void
test_registry_node_removed (TestFixture *f, gconstpointer data)
{
  g_autoptr (WpNode) client_node = NULL;
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpNode) new_node = NULL;
  guint32 id;

  if (!load_audiotestsrc_adapter (f))
    return;

  client_node = create_client_node (f);
  install_node_om (f, f->om);
  g_assert_cmpuint (wp_object_manager_get_n_objects (f->om), ==, 1);
  node = wp_object_manager_lookup (f->om, WP_TYPE_NODE, NULL);
  g_assert_nonnull (node);

  wp_object_activate (WP_OBJECT (node), WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, &f->base);
  g_main_loop_run (f->base.loop);
  id = wp_proxy_get_bound_id (WP_PROXY (node));
  wp_object_deactivate (WP_OBJECT (node), WP_PROXY_FEATURE_BOUND);

  /* destroy the node while its proxy is not bound */
  g_clear_object (&client_node);
  wait_for_n_objects (f, 0);

  /* another node, which can get the same id */
  client_node = create_client_node (f);
  wait_for_n_objects (f, 1);
  new_node = wp_object_manager_lookup (f->om, WP_TYPE_NODE, NULL);
  g_assert_true (new_node != node);
  g_test_message ("removed node id: %u, new node id: %u", id,
      wp_proxy_get_bound_id (WP_PROXY (client_node)));

  /* the proxy of the removed node must not be bound to anything */
  wp_object_activate (WP_OBJECT (node), WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL,
      NULL, (GAsyncReadyCallback) activate_bind_error_cb, &f->base);
  g_main_loop_run (f->base.loop);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (node)), ==, 0);
  g_assert_null (wp_proxy_get_pw_proxy (WP_PROXY (node)));
  assert_request_destroy_does_nothing (f, WP_GLOBAL_PROXY (node), client_node);

  wp_object_activate (WP_OBJECT (new_node), WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, &f->base);
  g_main_loop_run (f->base.loop);
  g_assert_cmpuint (wp_proxy_get_bound_id (WP_PROXY (new_node)), ==,
      wp_proxy_get_bound_id (WP_PROXY (client_node)));
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/proxy/basic", TestFixture, NULL,
      test_proxy_setup, test_proxy_basic, test_proxy_teardown);
  g_test_add ("/wp/proxy/node", TestFixture, NULL,
      test_proxy_setup, test_node, test_proxy_teardown);
  g_test_add ("/wp/proxy/link_error", TestFixture, NULL,
      test_proxy_setup, test_link_error, test_proxy_teardown);
  g_test_add ("/wp/proxy/enum_params_error", TestFixture, NULL,
      test_proxy_setup, test_enum_params_error, test_proxy_teardown);
  g_test_add ("/wp/proxy/registry_node_removed_unbound", TestFixture, NULL,
      test_proxy_setup_client_core, test_registry_node_removed_unbound,
      test_proxy_teardown);
  g_test_add ("/wp/proxy/registry_node_bind_again", TestFixture, NULL,
      test_proxy_setup_client_core, test_registry_node_bind_again,
      test_proxy_teardown);
  g_test_add ("/wp/proxy/registry_node_removed", TestFixture, NULL,
      test_proxy_setup_client_core, test_registry_node_removed,
      test_proxy_teardown);
  return g_test_run ();
}
