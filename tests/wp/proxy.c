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
} TestFixture;

static void
test_proxy_setup (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, 0);
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
    g_assert_nonnull (pod = g_value_get_boxed (&item));
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
    g_assert_nonnull (info = wp_pipewire_object_get_native_info (proxy));
    g_assert_cmpint (wp_proxy_get_bound_id (WP_PROXY (proxy)), ==, info->id);
  }

  /* properties */
  {
    const gchar *id;
    g_assert_nonnull(id =
        wp_pipewire_object_get_property (proxy, PW_KEY_OBJECT_ID));
    g_assert_cmpint (info->id, ==, atoi(id));
  }
  {
    const char *id;
    g_autoptr (WpProperties) props = wp_pipewire_object_get_properties (proxy);

    g_assert_nonnull (props);
    g_assert_true (wp_properties_peek_dict (props) == info->props);
    g_assert_nonnull (id = wp_properties_get (props, PW_KEY_OBJECT_ID));
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

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/proxy/basic", TestFixture, NULL,
      test_proxy_setup, test_proxy_basic, test_proxy_teardown);
  g_test_add ("/wp/proxy/node", TestFixture, NULL,
      test_proxy_setup, test_node, test_proxy_teardown);

  return g_test_run ();
}
