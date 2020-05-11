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

} TestProxyFixture;

static void
test_proxy_setup (TestProxyFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, 0);
  self->om = wp_object_manager_new ();
}

static void
test_proxy_teardown (TestProxyFixture *self, gconstpointer user_data)
{
  g_clear_object (&self->om);
  wp_base_test_fixture_teardown (&self->base);
}

static void
test_proxy_basic_augmented (WpProxy *proxy, GAsyncResult *res,
    TestProxyFixture *fixture)
{
  g_autoptr (GError) error = NULL;
  g_assert_true (wp_proxy_augment_finish (proxy, res, &error));
  g_assert_no_error (error);

  g_assert_true (wp_proxy_get_features (proxy) & WP_PROXY_FEATURE_PW_PROXY);
  g_assert_nonnull (wp_proxy_get_pw_proxy (proxy));

  g_main_loop_quit (fixture->base.loop);
}

static void
test_proxy_basic_object_added (WpObjectManager *om, WpProxy *proxy,
    TestProxyFixture *fixture)
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
  g_assert_cmphex (wp_proxy_get_global_permissions (proxy), ==, PW_PERM_RWX);
  g_assert_true (WP_IS_CLIENT (proxy));

  g_assert_cmphex (wp_proxy_get_features (proxy), ==, 0);
  g_assert_null (wp_proxy_get_pw_proxy (proxy));

  {
    g_autoptr (WpProperties) props = wp_proxy_get_global_properties (proxy);
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, PW_KEY_PROTOCOL), ==,
        "protocol-native");
  }

  wp_proxy_augment (proxy, WP_PROXY_FEATURE_PW_PROXY, NULL,
      (GAsyncReadyCallback) test_proxy_basic_augmented, fixture);
}

static void
test_proxy_basic (TestProxyFixture *fixture, gconstpointer data)
{
  /* our test server should advertise exactly one
   * client: our WpRemote; use this to test WpProxy */
  g_signal_connect (fixture->om, "object-added",
      (GCallback) test_proxy_basic_object_added, fixture);

  wp_object_manager_add_interest_1 (fixture->om, WP_TYPE_CLIENT, NULL);
  wp_core_install_object_manager (fixture->base.core, fixture->om);

  g_main_loop_run (fixture->base.loop);
}

typedef struct {
  TestProxyFixture *fixture;
  guint n_params;
} TestNodeParamData;

static void
test_node_param (WpNode *node, int seq, guint id, guint index,
    guint next, struct spa_pod *param, TestNodeParamData *data)
{
  data->n_params++;
}

static void
test_node_enum_params_done (WpProxy *node, GAsyncResult *res,
    TestNodeParamData *data)
{
  g_autoptr (GPtrArray) params = NULL;
  g_autoptr (GError) error = NULL;
  guint i;

  params = wp_proxy_enum_params_collect_finish (node, res, &error);
  g_assert_no_error (error);
  g_assert_nonnull (params);

  /* the param signal must have also been fired for all params */
  g_assert_cmpint (params->len, ==, data->n_params);

  for (i = 0; i < params->len; i++) {
    WpSpaPod *pod = g_ptr_array_index (params, i);
    g_assert_cmpstr ("PropInfo", ==, wp_spa_pod_get_object_type_name (pod));
  }

  g_main_loop_quit (data->fixture->base.loop);
  g_free (data);
}

static void
test_node_object_added (WpObjectManager *om, WpProxy *proxy,
    TestProxyFixture *fixture)
{
  const struct pw_node_info *info;
  TestNodeParamData *param_data;

  g_assert_nonnull (proxy);
  g_assert_cmphex (wp_proxy_get_features (proxy), ==, WP_PROXY_FEATURES_STANDARD);
  g_assert_nonnull (wp_proxy_get_pw_proxy (proxy));

  g_assert_true (WP_IS_NODE (proxy));
  info = wp_proxy_get_info (proxy);
  g_assert_nonnull (info);
  g_assert_cmpint (wp_proxy_get_bound_id (proxy), ==, info->id);

  {
    const char *id;
    g_autoptr (WpProperties) props = wp_proxy_get_properties (proxy);

    g_assert_nonnull (props);
    g_assert_true (wp_properties_peek_dict (props) == info->props);
    id = wp_properties_get (props, PW_KEY_OBJECT_ID);
    g_assert_nonnull (id);
    g_assert_cmpint (info->id, ==, atoi(id));
  }

  param_data = g_new0 (TestNodeParamData, 1);
  param_data->fixture = fixture;

  g_signal_connect (proxy, "param", (GCallback) test_node_param,
      param_data);
  g_autoptr (WpSpaPod) filter = wp_spa_pod_new_none ();
  wp_proxy_enum_params_collect (proxy, SPA_PARAM_PropInfo, 0, -1,
      filter, NULL, (GAsyncReadyCallback) test_node_enum_params_done,
      param_data);
}

static void
test_node (TestProxyFixture *fixture, gconstpointer data)
{
  /* load audiotestsrc on the server side */
  pw_thread_loop_lock (fixture->base.server.thread_loop);
  pw_context_add_spa_lib (fixture->base.server.context, "audiotestsrc",
      "audiotestsrc/libspa-audiotestsrc");
  if (!pw_context_load_module (fixture->base.server.context,
        "libpipewire-module-spa-node", "audiotestsrc", NULL)) {
    pw_thread_loop_unlock (fixture->base.server.thread_loop);
    g_test_skip ("audiotestsrc SPA plugin is not installed");
    return;
  }
  pw_thread_loop_unlock (fixture->base.server.thread_loop);

  /* we should be able to see this exported audiotestsrc node on the client */
  g_signal_connect (fixture->om, "object-added",
      (GCallback) test_node_object_added, fixture);

  /* declare interest and set default features to be ready
     when the signal is fired */
  wp_object_manager_add_interest_1 (fixture->om, WP_TYPE_NODE, NULL);
  wp_object_manager_request_proxy_features (fixture->om, WP_TYPE_NODE,
      WP_PROXY_FEATURES_STANDARD);
  wp_core_install_object_manager (fixture->base.core, fixture->om);

  g_main_loop_run (fixture->base.loop);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/proxy/basic", TestProxyFixture, NULL,
      test_proxy_setup, test_proxy_basic, test_proxy_teardown);
  g_test_add ("/wp/proxy/node", TestProxyFixture, NULL,
      test_proxy_setup, test_node, test_proxy_teardown);

  return g_test_run ();
}
