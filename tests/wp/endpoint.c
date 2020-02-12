/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager.h>

#include "test-server.h"

typedef struct {
  /* the local pipewire server */
  WpTestServer server;

  /* the main loop */
  GMainContext *context;
  GMainLoop *loop;
  GSource *timeout_source;

  /* the client that exports */
  WpCore *export_core;
  WpObjectManager *export_om;

  /* the client that receives a proxy */
  WpCore *proxy_core;
  WpObjectManager *proxy_om;

  WpImplEndpoint *impl_endpoint;
  WpProxy *proxy_endpoint;

  gint n_events;

} TestEndpointFixture;

static gboolean
timeout_callback (TestEndpointFixture *fixture)
{
  g_message ("test timed out");
  g_test_fail ();
  g_main_loop_quit (fixture->loop);

  return G_SOURCE_REMOVE;
}

static void
test_endpoint_disconnected (WpCore *core, TestEndpointFixture *fixture)
{
  g_message ("core disconnected");
  g_test_fail ();
  g_main_loop_quit (fixture->loop);
}

static void
test_endpoint_setup (TestEndpointFixture *self, gconstpointer user_data)
{
  g_autoptr (WpProperties) props = NULL;

  wp_test_server_setup (&self->server);
  pw_thread_loop_lock (self->server.thread_loop);
  if (!pw_context_load_module (self->server.context,
      "libpipewire-module-session-manager", NULL, NULL)) {
    pw_thread_loop_unlock (self->server.thread_loop);
    g_test_skip ("libpipewire-module-session-manager is not installed");
    return;
  }
  pw_thread_loop_unlock (self->server.thread_loop);

  props = wp_properties_new (PW_KEY_REMOTE_NAME, self->server.name, NULL);
  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);

  self->export_core = wp_core_new (self->context, props);
  self->export_om = wp_object_manager_new ();

  self->proxy_core = wp_core_new (self->context, props);
  self->proxy_om = wp_object_manager_new ();

  g_main_context_push_thread_default (self->context);

  /* watchdogs */
  g_signal_connect (self->export_core, "disconnected",
      (GCallback) test_endpoint_disconnected, self);
  g_signal_connect (self->proxy_core, "disconnected",
      (GCallback) test_endpoint_disconnected, self);

  self->timeout_source = g_timeout_source_new_seconds (3);
  g_source_set_callback (self->timeout_source, (GSourceFunc) timeout_callback,
      self, NULL);
  g_source_attach (self->timeout_source, self->context);
}

static void
test_endpoint_teardown (TestEndpointFixture *self, gconstpointer user_data)
{
  g_main_context_pop_thread_default (self->context);

  g_clear_object (&self->proxy_om);
  g_clear_object (&self->proxy_core);
  g_clear_object (&self->export_om);
  g_clear_object (&self->export_core);
  g_clear_pointer (&self->timeout_source, g_source_unref);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_clear_pointer (&self->context, g_main_context_unref);
  wp_test_server_teardown (&self->server);
}

static void
test_endpoint_basic_impl_object_added (WpObjectManager *om,
    WpEndpoint *endpoint, TestEndpointFixture *fixture)
{
  g_debug ("impl object added");

  g_assert_true (WP_IS_IMPL_ENDPOINT (endpoint));

  g_assert_null (fixture->impl_endpoint);
  fixture->impl_endpoint = WP_IMPL_ENDPOINT (endpoint);

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->loop);
}

static void
test_endpoint_basic_impl_object_removed (WpObjectManager *om,
    WpEndpoint *endpoint, TestEndpointFixture *fixture)
{
  g_debug ("impl object removed");

  g_assert_true (WP_IS_IMPL_ENDPOINT (endpoint));

  g_assert_nonnull (fixture->impl_endpoint);
  fixture->impl_endpoint = NULL;

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->loop);
}

static void
test_endpoint_basic_proxy_object_added (WpObjectManager *om,
    WpEndpoint *endpoint, TestEndpointFixture *fixture)
{
  g_debug ("proxy object added");

  g_assert_true (WP_IS_ENDPOINT (endpoint));

  g_assert_null (fixture->proxy_endpoint);
  fixture->proxy_endpoint = WP_PROXY (endpoint);

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->loop);
}

static void
test_endpoint_basic_proxy_object_removed (WpObjectManager *om,
    WpEndpoint *endpoint, TestEndpointFixture *fixture)
{
  g_debug ("proxy object removed");

  g_assert_true (WP_IS_ENDPOINT (endpoint));

  g_assert_nonnull (fixture->proxy_endpoint);
  fixture->proxy_endpoint = NULL;

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->loop);
}

static void
test_endpoint_basic_export_done (WpProxy * endpoint, GAsyncResult * res,
    TestEndpointFixture *fixture)
{
  g_autoptr (GError) error = NULL;

  g_debug ("export done");

  g_assert_true (wp_proxy_augment_finish (endpoint, res, &error));
  g_assert_no_error (error);

  g_assert_true (WP_IS_IMPL_ENDPOINT (endpoint));

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->loop);
}

static void
test_endpoint_basic_control_changed (WpEndpoint * endpoint,
    guint32 control_id, TestEndpointFixture *fixture)
{
  g_debug ("endpoint changed: %s (0x%x)", G_OBJECT_TYPE_NAME (endpoint),
      control_id);

  g_assert_true (WP_IS_ENDPOINT (endpoint));

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->loop);
}

static void
test_endpoint_basic_notify_properties (WpEndpoint * endpoint, GParamSpec * param,
    TestEndpointFixture *fixture)
{
  g_debug ("properties changed: %s", G_OBJECT_TYPE_NAME (endpoint));

  g_assert_true (WP_IS_ENDPOINT (endpoint));

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->loop);
}

static void
test_endpoint_basic (TestEndpointFixture *fixture, gconstpointer data)
{
  g_autoptr (WpImplEndpoint) endpoint = NULL;
  gfloat float_value;
  gboolean boolean_value;

  /* set up the export side */
  g_signal_connect (fixture->export_om, "object-added",
      (GCallback) test_endpoint_basic_impl_object_added, fixture);
  g_signal_connect (fixture->export_om, "object-removed",
      (GCallback) test_endpoint_basic_impl_object_removed, fixture);
  wp_object_manager_add_object_interest (fixture->export_om,
      WP_TYPE_IMPL_ENDPOINT, NULL);
  wp_core_install_object_manager (fixture->export_core, fixture->export_om);

  g_assert_true (wp_core_connect (fixture->export_core));

  /* set up the proxy side */
  g_signal_connect (fixture->proxy_om, "object-added",
      (GCallback) test_endpoint_basic_proxy_object_added, fixture);
  g_signal_connect (fixture->proxy_om, "object-removed",
      (GCallback) test_endpoint_basic_proxy_object_removed, fixture);
  wp_object_manager_add_proxy_interest (fixture->proxy_om,
      WP_TYPE_ENDPOINT, NULL,
      WP_PROXY_FEATURES_STANDARD | WP_ENDPOINT_FEATURE_CONTROLS);
  wp_core_install_object_manager (fixture->proxy_core, fixture->proxy_om);

  g_assert_true (wp_core_connect (fixture->proxy_core));

  /* create endpoint */
  endpoint = wp_impl_endpoint_new (fixture->export_core);
  wp_impl_endpoint_set_property (endpoint, "test.property", "test-value");
  wp_impl_endpoint_register_control (endpoint, WP_ENDPOINT_CONTROL_VOLUME);
  wp_impl_endpoint_register_control (endpoint, WP_ENDPOINT_CONTROL_MUTE);
  g_assert_true (wp_endpoint_set_control_float (WP_ENDPOINT (endpoint),
          WP_ENDPOINT_CONTROL_VOLUME, 0.7f));
  g_assert_true (wp_endpoint_set_control_boolean (WP_ENDPOINT (endpoint),
          WP_ENDPOINT_CONTROL_MUTE, TRUE));

  /* verify properties are set before export */
  {
    g_autoptr (WpProperties) props =
        wp_proxy_get_properties (WP_PROXY (endpoint));
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "test-value");
  }
  g_assert_true (wp_endpoint_get_control_float (WP_ENDPOINT (endpoint),
          WP_ENDPOINT_CONTROL_VOLUME, &float_value));
  g_assert_true (wp_endpoint_get_control_boolean (WP_ENDPOINT (endpoint),
          WP_ENDPOINT_CONTROL_MUTE, &boolean_value));
  g_assert_cmpfloat_with_epsilon (float_value, 0.7f, 0.001);
  g_assert_cmpint (boolean_value, ==, TRUE);

  /* do export */
  wp_proxy_augment (WP_PROXY (endpoint), WP_PROXY_FEATURE_BOUND, NULL,
      (GAsyncReadyCallback) test_endpoint_basic_export_done, fixture);

  /* run until objects are created and features are cached */
  fixture->n_events = 0;
  g_main_loop_run (fixture->loop);
  g_assert_cmpint (fixture->n_events, ==, 3);
  g_assert_nonnull (fixture->impl_endpoint);
  g_assert_nonnull (fixture->proxy_endpoint);
  g_assert_true (fixture->impl_endpoint == endpoint);

  /* test round 1: verify the values on the proxy */

  g_assert_cmphex (wp_proxy_get_features (fixture->proxy_endpoint), ==,
      WP_PROXY_FEATURE_PW_PROXY |
      WP_PROXY_FEATURE_INFO |
      WP_PROXY_FEATURE_BOUND |
      WP_ENDPOINT_FEATURE_CONTROLS);

  g_assert_cmpuint (wp_proxy_get_bound_id (fixture->proxy_endpoint), ==,
      wp_proxy_get_bound_id (WP_PROXY (endpoint)));

  {
    g_autoptr (WpProperties) props =
        wp_proxy_get_properties (fixture->proxy_endpoint);
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "test-value");
  }
  g_assert_true (wp_endpoint_get_control_float (
          WP_ENDPOINT (fixture->proxy_endpoint),
          WP_ENDPOINT_CONTROL_VOLUME, &float_value));
  g_assert_true (wp_endpoint_get_control_boolean (
          WP_ENDPOINT (fixture->proxy_endpoint),
          WP_ENDPOINT_CONTROL_MUTE, &boolean_value));
  g_assert_cmpfloat_with_epsilon (float_value, 0.7f, 0.001);
  g_assert_cmpint (boolean_value, ==, TRUE);

  /* setup change signals */
  g_signal_connect (fixture->proxy_endpoint, "control-changed",
      (GCallback) test_endpoint_basic_control_changed, fixture);
  g_signal_connect (endpoint, "control-changed",
      (GCallback) test_endpoint_basic_control_changed, fixture);
  g_signal_connect (fixture->proxy_endpoint, "notify::properties",
      (GCallback) test_endpoint_basic_notify_properties, fixture);
  g_signal_connect (endpoint, "notify::properties",
      (GCallback) test_endpoint_basic_notify_properties, fixture);

  /* change control on the proxy */
  g_assert_true (wp_endpoint_set_control_float (
          WP_ENDPOINT (fixture->proxy_endpoint),
          WP_ENDPOINT_CONTROL_VOLUME, 1.0f));

  /* run until the change is on both sides */
  fixture->n_events = 0;
  g_main_loop_run (fixture->loop);
  g_assert_cmpint (fixture->n_events, ==, 2);

  /* test round 2: verify the value change on both sides */

  g_assert_true (wp_endpoint_get_control_float (
          WP_ENDPOINT (fixture->proxy_endpoint),
          WP_ENDPOINT_CONTROL_VOLUME, &float_value));
  g_assert_true (wp_endpoint_get_control_boolean (
          WP_ENDPOINT (fixture->proxy_endpoint),
          WP_ENDPOINT_CONTROL_MUTE, &boolean_value));
  g_assert_cmpfloat_with_epsilon (float_value, 1.0f, 0.001);
  g_assert_cmpint (boolean_value, ==, TRUE);

  g_assert_true (wp_endpoint_get_control_float (WP_ENDPOINT (endpoint),
          WP_ENDPOINT_CONTROL_VOLUME, &float_value));
  g_assert_true (wp_endpoint_get_control_boolean (WP_ENDPOINT (endpoint),
          WP_ENDPOINT_CONTROL_MUTE, &boolean_value));
  g_assert_cmpfloat_with_epsilon (float_value, 1.0f, 0.001);
  g_assert_cmpint (boolean_value, ==, TRUE);

  /* change control on the impl */
  fixture->n_events = 0;
  g_assert_true (wp_endpoint_set_control_boolean (WP_ENDPOINT (endpoint),
          WP_ENDPOINT_CONTROL_MUTE, FALSE));

  /* run until the change is on both sides */
  g_main_loop_run (fixture->loop);
  g_assert_cmpint (fixture->n_events, ==, 2);

  /* test round 3: verify the value change on both sides */

  g_assert_true (wp_endpoint_get_control_float (
          WP_ENDPOINT (fixture->proxy_endpoint),
          WP_ENDPOINT_CONTROL_VOLUME, &float_value));
  g_assert_true (wp_endpoint_get_control_boolean (
          WP_ENDPOINT (fixture->proxy_endpoint),
          WP_ENDPOINT_CONTROL_MUTE, &boolean_value));
  g_assert_cmpfloat_with_epsilon (float_value, 1.0f, 0.001);
  g_assert_cmpint (boolean_value, ==, FALSE);

  g_assert_true (wp_endpoint_get_control_float (WP_ENDPOINT (endpoint),
          WP_ENDPOINT_CONTROL_VOLUME, &float_value));
  g_assert_true (wp_endpoint_get_control_boolean (WP_ENDPOINT (endpoint),
          WP_ENDPOINT_CONTROL_MUTE, &boolean_value));
  g_assert_cmpfloat_with_epsilon (float_value, 1.0f, 0.001);
  g_assert_cmpint (boolean_value, ==, FALSE);

  /* change a property on the impl */
  fixture->n_events = 0;
  wp_impl_endpoint_set_property (endpoint, "test.property", "changed-value");

  /* run until the change is on both sides */
  g_main_loop_run (fixture->loop);
  g_assert_cmpint (fixture->n_events, ==, 2);

  /* test round 4: verify the property change on both sides */

  {
    g_autoptr (WpProperties) props =
        wp_proxy_get_properties (WP_PROXY (endpoint));
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "changed-value");
  }
  {
    g_autoptr (WpProperties) props =
        wp_proxy_get_properties (fixture->proxy_endpoint);
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "changed-value");
  }

  /* destroy impl endpoint */
  fixture->n_events = 0;
  g_clear_object (&endpoint);

  /* run until objects are destroyed */
  g_main_loop_run (fixture->loop);
  g_assert_cmpint (fixture->n_events, ==, 2);
  g_assert_null (fixture->impl_endpoint);
  g_assert_null (fixture->proxy_endpoint);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  pw_init (NULL, NULL);

  g_test_add ("/wp/endpoint/basic", TestEndpointFixture, NULL,
      test_endpoint_setup, test_endpoint_basic, test_endpoint_teardown);

  return g_test_run ();
}
