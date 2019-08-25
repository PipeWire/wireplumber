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
  GMainLoop *loop;

  /* the client wireplumber objects */
  WpCore *core;
  WpRemote *remote;

} TestProxyFixture;

static void
test_proxy_setup (TestProxyFixture *self, gconstpointer user_data)
{
  wp_test_server_setup (&self->server);
  g_setenv ("PIPEWIRE_REMOTE", self->server.name, TRUE);
  self->loop = g_main_loop_new (NULL, FALSE);
  self->core = wp_core_new ();
  self->remote = wp_remote_pipewire_new (self->core, NULL);
}

static void
test_proxy_teardown (TestProxyFixture *self, gconstpointer user_data)
{
  g_clear_object (&self->remote);
  g_clear_object (&self->core);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_unsetenv ("PIPEWIRE_REMOTE");
  wp_test_server_teardown (&self->server);
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
test_proxy_basic_augmented (WpProxy *proxy, GAsyncResult *res,
    TestProxyFixture *fixture)
{
  g_autoptr (GError) error = NULL;
  g_assert_true (wp_proxy_augment_finish (proxy, res, &error));

  g_assert_true (wp_proxy_get_features (proxy) & WP_PROXY_FEATURE_PW_PROXY);
  g_assert_nonnull (wp_proxy_get_pw_proxy (proxy));

  g_main_loop_quit (fixture->loop);
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

  g_assert_cmphex (wp_proxy_get_features (proxy), ==, 0);
  g_assert_null (wp_proxy_get_pw_proxy (proxy));
  g_assert_null (wp_proxy_get_native_info (proxy));

  {
    g_autoptr (WpProperties) props = wp_proxy_get_global_properties (proxy);
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, PW_KEY_PROTOCOL), ==,
        "protocol-native");
  }

  wp_proxy_augment (proxy, WP_PROXY_FEATURE_PW_PROXY, NULL,
      (GAsyncReadyCallback) test_proxy_basic_augmented, fixture);
}

static gboolean
timeout_callback (TestProxyFixture *fixture)
{
  g_message ("test timed out");
  g_test_fail ();
  g_main_loop_quit (fixture->loop);

  return G_SOURCE_REMOVE;
}

static void
test_proxy_basic (TestProxyFixture *fixture, gconstpointer data)
{
  g_signal_connect (fixture->remote, "state-changed",
      (GCallback) test_proxy_state_changed, fixture);

  /* our test server should advertise exactly one
   * client: our WpRemote; use this to test WpProxy */
  g_signal_connect (fixture->remote, "global-added::client",
      (GCallback) test_proxy_basic_global_added, fixture);

  g_assert_true (wp_remote_connect (fixture->remote));
  g_timeout_add_seconds (3, (GSourceFunc) timeout_callback, fixture);
  g_main_loop_run (fixture->loop);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  pw_init (NULL, NULL);

  g_test_add ("/wp/proxy/basic", TestProxyFixture, NULL,
      test_proxy_setup, test_proxy_basic, test_proxy_teardown);

  return g_test_run ();
}
