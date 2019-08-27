/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

#include "test-server.h"

typedef struct {
  /* the local pipewire server */
  WpTestServer server;

  /* the main loop */
  GMainContext *context;
  GMainLoop *loop;
  GSource *timeout_source;

  /* the client wireplumber objects */
  WpCore *core;
  WpRemote *remote;

} TestProxyFixture;

static gboolean
timeout_callback (TestProxyFixture *fixture)
{
  g_message ("test timed out");
  g_test_fail ();
  g_main_loop_quit (fixture->loop);

  return G_SOURCE_REMOVE;
}

static void
test_proxy_state_changed (WpRemote *remote, WpRemoteState state,
    TestProxyFixture *fixture)
{
  g_autofree gchar * msg = NULL;

  switch (state) {
  case WP_REMOTE_STATE_ERROR:
    g_object_get (remote, "error-message", &msg, NULL);
    g_message ("remote error: %s", msg);
    g_test_fail ();
    g_main_loop_quit (fixture->loop);
    break;
  default:
    break;
  }
}

static void
test_proxy_setup (TestProxyFixture *self, gconstpointer user_data)
{
  wp_test_server_setup (&self->server);
  g_setenv ("PIPEWIRE_REMOTE", self->server.name, TRUE);
  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);
  self->core = wp_core_new ();
  self->remote = wp_remote_pipewire_new (self->core, self->context);

  g_main_context_push_thread_default (self->context);

  /* watchdogs */
  g_signal_connect (self->remote, "state-changed",
      (GCallback) test_proxy_state_changed, self);

  self->timeout_source = g_timeout_source_new_seconds (3);
  g_source_set_callback (self->timeout_source, (GSourceFunc) timeout_callback,
      self, NULL);
  g_source_attach (self->timeout_source, self->context);
}

static void
test_proxy_teardown (TestProxyFixture *self, gconstpointer user_data)
{
  g_main_context_pop_thread_default (self->context);

  g_clear_object (&self->remote);
  g_clear_object (&self->core);
  g_clear_pointer (&self->timeout_source, g_source_unref);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_clear_pointer (&self->context, g_main_context_unref);
  g_unsetenv ("PIPEWIRE_REMOTE");
  wp_test_server_teardown (&self->server);
}

static void
test_proxy_basic_done (WpProxy *proxy, GAsyncResult *res,
    TestProxyFixture *fixture)
{
  g_autoptr (GError) error = NULL;
  g_assert_true (wp_proxy_sync_finish (proxy, res, &error));
  g_assert_no_error (error);

  g_main_loop_quit (fixture->loop);
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

  wp_proxy_sync (proxy, NULL, (GAsyncReadyCallback) test_proxy_basic_done,
      fixture);
}

static void
test_proxy_basic_global_added (WpRemote *remote, WpProxy *proxy,
    TestProxyFixture *fixture)
{
  g_assert_nonnull (proxy);
  {
    g_autoptr (WpRemote) remote = wp_proxy_get_remote (proxy);
    g_assert_nonnull (remote);
  }
  g_assert_cmpuint (wp_proxy_get_global_id (proxy), !=, 0);
  g_assert_true (wp_proxy_is_global (proxy));
  g_assert_cmpuint (wp_proxy_get_interface_quark (proxy), ==,
      g_quark_from_string ("client"));
  g_assert_cmpuint (wp_proxy_get_interface_type (proxy), ==,
      PW_TYPE_INTERFACE_Client);
  g_assert_cmpstr (wp_proxy_get_interface_name (proxy), ==,
      "PipeWire:Interface:Client");
  g_assert_cmphex (wp_proxy_get_global_permissions (proxy), ==, PW_PERM_RWX);
  g_assert_true (WP_IS_PROXY_CLIENT (proxy));

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
  g_signal_connect (fixture->remote, "global-added::client",
      (GCallback) test_proxy_basic_global_added, fixture);

  g_assert_true (wp_remote_connect (fixture->remote));
  g_main_loop_run (fixture->loop);
}

typedef struct {
  TestProxyFixture *fixture;
  guint n_params;
} TestProxyNodeParamData;

static void
test_proxy_node_param (WpProxyNode *node, int seq, guint id, guint index,
    guint next, struct spa_pod *param, TestProxyNodeParamData *data)
{
  data->n_params++;
}

static void
test_proxy_node_enum_params_done (WpProxyNode *node, GAsyncResult *res,
    TestProxyNodeParamData *data)
{
  g_autoptr (GPtrArray) params = NULL;
  g_autoptr (GError) error = NULL;
  guint i;

  params = wp_proxy_node_enum_params_collect_finish (node, res, &error);
  g_assert_no_error (error);
  g_assert_nonnull (params);

  /* the param signal must have also been fired for all params */
  g_assert_cmpint (params->len, ==, data->n_params);

  for (i = 0; i < params->len; i++) {
    struct spa_pod *pod = g_ptr_array_index(params, i);
    g_assert_true (spa_pod_is_object_type (pod, SPA_TYPE_OBJECT_PropInfo));
  }

  g_main_loop_quit (data->fixture->loop);
  g_free (data);
}

static void
test_proxy_node_global_added (WpRemote *remote, WpProxy *proxy,
    TestProxyFixture *fixture)
{
  const struct pw_node_info *info;
  TestProxyNodeParamData *param_data;

  g_assert_nonnull (proxy);
  g_assert_true (wp_proxy_is_global (proxy));
  g_assert_cmpuint (wp_proxy_get_interface_type (proxy), ==,
      PW_TYPE_INTERFACE_Node);
  g_assert_cmphex (wp_proxy_get_features (proxy), ==,
      WP_PROXY_FEATURE_PW_PROXY | WP_PROXY_FEATURE_INFO);
  g_assert_nonnull (wp_proxy_get_pw_proxy (proxy));

  g_assert_true (WP_IS_PROXY_NODE (proxy));
  info = wp_proxy_node_get_info (WP_PROXY_NODE (proxy));
  g_assert_nonnull (info);
  g_assert_cmpint (wp_proxy_get_global_id (proxy), ==, info->id);

  {
    const char *id;
    g_autoptr (WpProperties) props =
        wp_proxy_node_get_properties (WP_PROXY_NODE (proxy));

    g_assert_nonnull (props);
    g_assert_true (wp_properties_peek_dict (props) == info->props);
    id = wp_properties_get (props, "node.id");
    g_assert_nonnull (id);
    g_assert_cmpint (info->id, ==, atoi(id));
  }

  param_data = g_new0 (TestProxyNodeParamData, 1);
  param_data->fixture = fixture;

  g_signal_connect (proxy, "param", (GCallback) test_proxy_node_param,
      param_data);
  wp_proxy_node_enum_params_collect (WP_PROXY_NODE (proxy), SPA_PARAM_PropInfo,
      NULL, NULL, (GAsyncReadyCallback) test_proxy_node_enum_params_done,
      param_data);
}

static void
test_proxy_node (TestProxyFixture *fixture, gconstpointer data)
{
  /* load audiotestsrc on the server side */
  pw_thread_loop_lock (fixture->server.thread_loop);
  pw_core_add_spa_lib (fixture->server.core, "audiotestsrc",
      "audiotestsrc/libspa-audiotestsrc");
  if (!pw_module_load (fixture->server.core, "libpipewire-module-spa-node",
        "audiotestsrc", NULL)) {
    pw_thread_loop_unlock (fixture->server.thread_loop);
    g_test_skip ("audiotestsrc SPA plugin is not installed");
    return;
  }
  pw_thread_loop_unlock (fixture->server.thread_loop);

  /* we should be able to see this exported audiotestsrc node on the client */
  g_signal_connect (fixture->remote, "global-added::node",
      (GCallback) test_proxy_node_global_added, fixture);

  /* tell the remote to call global-added only when these features are ready */
  wp_remote_pipewire_set_default_features (WP_REMOTE_PIPEWIRE (fixture->remote),
      WP_TYPE_PROXY_NODE, WP_PROXY_FEATURE_PW_PROXY | WP_PROXY_FEATURE_INFO);

  g_assert_true (wp_remote_connect (fixture->remote));
  g_main_loop_run (fixture->loop);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  pw_init (NULL, NULL);

  g_test_add ("/wp/proxy/basic", TestProxyFixture, NULL,
      test_proxy_setup, test_proxy_basic, test_proxy_teardown);
  g_test_add ("/wp/proxy/node", TestProxyFixture, NULL,
      test_proxy_setup, test_proxy_node, test_proxy_teardown);

  return g_test_run ();
}
