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

#include "../common/test-server.h"

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

  WpImplSession *impl_session;
  WpProxy *proxy_session;

  gint n_events;

} TestSessionFixture;

static gboolean
timeout_callback (TestSessionFixture *fixture)
{
  g_message ("test timed out");
  g_test_fail ();
  g_main_loop_quit (fixture->loop);

  return G_SOURCE_REMOVE;
}

static void
test_session_disconnected (WpCore *core, TestSessionFixture *fixture)
{
  g_message ("core disconnected");
  g_test_fail ();
  g_main_loop_quit (fixture->loop);
}

static void
test_session_setup (TestSessionFixture *self, gconstpointer user_data)
{
  /* Register custom wireplumber session types */
  wp_spa_type_init (TRUE);

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
      (GCallback) test_session_disconnected, self);
  g_signal_connect (self->proxy_core, "disconnected",
      (GCallback) test_session_disconnected, self);

  self->timeout_source = g_timeout_source_new_seconds (3);
  g_source_set_callback (self->timeout_source, (GSourceFunc) timeout_callback,
      self, NULL);
  g_source_attach (self->timeout_source, self->context);
}

static void
test_session_teardown (TestSessionFixture *self, gconstpointer user_data)
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

  wp_spa_type_deinit ();
}

static void
test_session_basic_exported_object_added (WpObjectManager *om,
    WpSession *session, TestSessionFixture *fixture)
{
  g_debug ("exported object added");

  g_assert_true (WP_IS_IMPL_SESSION (session));

  g_assert_null (fixture->impl_session);
  fixture->impl_session = WP_IMPL_SESSION (session);

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->loop);
}

static void
test_session_basic_exported_object_removed (WpObjectManager *om,
    WpSession *session, TestSessionFixture *fixture)
{
  g_debug ("exported object removed");

  g_assert_true (WP_IS_IMPL_SESSION (session));

  g_assert_nonnull (fixture->impl_session);
  fixture->impl_session = NULL;

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->loop);
}

static void
test_session_basic_proxy_object_added (WpObjectManager *om,
    WpSession *session, TestSessionFixture *fixture)
{
  g_debug ("proxy object added");

  g_assert_true (WP_IS_SESSION (session));

  g_assert_null (fixture->proxy_session);
  fixture->proxy_session = WP_PROXY (session);

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->loop);
}

static void
test_session_basic_proxy_object_removed (WpObjectManager *om,
    WpSession *session, TestSessionFixture *fixture)
{
  g_debug ("proxy object removed");

  g_assert_true (WP_IS_SESSION (session));

  g_assert_nonnull (fixture->proxy_session);
  fixture->proxy_session = NULL;

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->loop);
}

static void
test_session_basic_export_done (WpProxy * session, GAsyncResult * res,
    TestSessionFixture *fixture)
{
  g_autoptr (GError) error = NULL;

  g_debug ("export done");

  g_assert_true (wp_proxy_augment_finish (session, res, &error));
  g_assert_no_error (error);

  g_assert_true (WP_IS_IMPL_SESSION (session));

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->loop);
}

static void
test_session_basic_default_endpoint_changed (WpSession * session,
    const char *type_name, guint32 id, TestSessionFixture *fixture)
{
  g_debug ("endpoint changed: %s (%s, %u)", G_OBJECT_TYPE_NAME (session),
      type_name, id);

  g_assert_true (WP_IS_SESSION (session));

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->loop);
}

static void
test_session_basic_notify_properties (WpSession * session, GParamSpec * param,
    TestSessionFixture *fixture)
{
  g_debug ("properties changed: %s", G_OBJECT_TYPE_NAME (session));

  g_assert_true (WP_IS_SESSION (session));

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->loop);
}

static void
test_session_basic (TestSessionFixture *fixture, gconstpointer data)
{
  g_autoptr (WpImplSession) session = NULL;

  /* set up the export side */
  g_signal_connect (fixture->export_om, "object-added",
      (GCallback) test_session_basic_exported_object_added, fixture);
  g_signal_connect (fixture->export_om, "object-removed",
      (GCallback) test_session_basic_exported_object_removed, fixture);
  wp_object_manager_add_interest (fixture->export_om,
      WP_TYPE_IMPL_SESSION, NULL,
      WP_SESSION_FEATURES_STANDARD);
  wp_core_install_object_manager (fixture->export_core, fixture->export_om);

  g_assert_true (wp_core_connect (fixture->export_core));

  /* set up the proxy side */
  g_signal_connect (fixture->proxy_om, "object-added",
      (GCallback) test_session_basic_proxy_object_added, fixture);
  g_signal_connect (fixture->proxy_om, "object-removed",
      (GCallback) test_session_basic_proxy_object_removed, fixture);
  wp_object_manager_add_interest (fixture->proxy_om,
      WP_TYPE_SESSION, NULL,
      WP_SESSION_FEATURES_STANDARD);
  wp_core_install_object_manager (fixture->proxy_core, fixture->proxy_om);

  g_assert_true (wp_core_connect (fixture->proxy_core));

  /* create session */
  session = wp_impl_session_new (fixture->export_core);
  wp_impl_session_set_property (session, "test.property", "test-value");
  wp_session_set_default_endpoint (WP_SESSION (session),
      "wp-session-default-endpoint-audio-sink", 5);
  wp_session_set_default_endpoint (WP_SESSION (session),
      "wp-session-default-endpoint-video-source", 9);

  /* verify properties are set before export */
  {
    g_autoptr (WpProperties) props =
        wp_proxy_get_properties (WP_PROXY (session));
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "test-value");
  }
  g_assert_cmpuint (wp_session_get_default_endpoint (WP_SESSION (session),
          "wp-session-default-endpoint-audio-sink"), ==, 5);
  g_assert_cmpuint (wp_session_get_default_endpoint (WP_SESSION (session),
          "wp-session-default-endpoint-video-source"), ==, 9);

  /* do export */
  wp_proxy_augment (WP_PROXY (session), WP_PROXY_FEATURE_BOUND, NULL,
      (GAsyncReadyCallback) test_session_basic_export_done, fixture);

  /* run until objects are created and features are cached */
  fixture->n_events = 0;
  g_main_loop_run (fixture->loop);
  g_assert_cmpint (fixture->n_events, ==, 3);
  g_assert_nonnull (fixture->impl_session);
  g_assert_nonnull (fixture->proxy_session);
  g_assert_true (fixture->impl_session == session);

  /* test round 1: verify the values on the proxy */

  g_assert_cmphex (wp_proxy_get_features (fixture->proxy_session), ==,
      WP_PROXY_FEATURE_PW_PROXY |
      WP_PROXY_FEATURE_INFO |
      WP_PROXY_FEATURE_BOUND |
      WP_PROXY_FEATURE_CONTROLS |
      WP_SESSION_FEATURE_ENDPOINTS);

  g_assert_cmpuint (wp_proxy_get_bound_id (fixture->proxy_session), ==,
      wp_proxy_get_bound_id (WP_PROXY (session)));

  {
    g_autoptr (WpProperties) props =
        wp_proxy_get_properties (fixture->proxy_session);
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "test-value");
  }
  g_assert_cmpuint (wp_session_get_default_endpoint (
          WP_SESSION (fixture->proxy_session),
          "wp-session-default-endpoint-audio-sink"), ==, 5);
  g_assert_cmpuint (wp_session_get_default_endpoint (
          WP_SESSION (fixture->proxy_session),
          "wp-session-default-endpoint-video-source"), ==, 9);

  /* setup change signals */
  g_signal_connect (fixture->proxy_session, "default-endpoint-changed",
      (GCallback) test_session_basic_default_endpoint_changed, fixture);
  g_signal_connect (session, "default-endpoint-changed",
      (GCallback) test_session_basic_default_endpoint_changed, fixture);
  g_signal_connect (fixture->proxy_session, "notify::properties",
      (GCallback) test_session_basic_notify_properties, fixture);
  g_signal_connect (session, "notify::properties",
      (GCallback) test_session_basic_notify_properties, fixture);

  /* change default endpoint on the proxy */
  wp_session_set_default_endpoint (WP_SESSION (fixture->proxy_session),
      "wp-session-default-endpoint-audio-sink", 73);

  /* run until the change is on both sides */
  fixture->n_events = 0;
  g_main_loop_run (fixture->loop);
  g_assert_cmpint (fixture->n_events, ==, 2);

  /* test round 2: verify the value change on both sides */

  g_assert_cmpuint (wp_session_get_default_endpoint (
          WP_SESSION (fixture->proxy_session),
          "wp-session-default-endpoint-audio-sink"), ==, 73);
  g_assert_cmpuint (wp_session_get_default_endpoint (
          WP_SESSION (fixture->proxy_session),
          "wp-session-default-endpoint-video-source"), ==, 9);

  g_assert_cmpuint (wp_session_get_default_endpoint (
          WP_SESSION (session), "wp-session-default-endpoint-audio-sink"), ==, 73);
  g_assert_cmpuint (wp_session_get_default_endpoint (
          WP_SESSION (session), "wp-session-default-endpoint-video-source"), ==, 9);

  /* change default endpoint on the exported */
  fixture->n_events = 0;
  wp_session_set_default_endpoint (WP_SESSION (session),
      "wp-session-default-endpoint-audio-source", 44);

  /* run until the change is on both sides */
  g_main_loop_run (fixture->loop);
  g_assert_cmpint (fixture->n_events, ==, 2);

  /* test round 3: verify the value change on both sides */

  g_assert_cmpuint (wp_session_get_default_endpoint (
          WP_SESSION (session), "wp-session-default-endpoint-audio-source"), ==, 44);
  g_assert_cmpuint (wp_session_get_default_endpoint (
          WP_SESSION (fixture->proxy_session),
          "wp-session-default-endpoint-audio-source"), ==, 44);

  /* change a property on the exported */
  fixture->n_events = 0;
  wp_impl_session_set_property (session, "test.property", "changed-value");

  /* run until the change is on both sides */
  g_main_loop_run (fixture->loop);
  g_assert_cmpint (fixture->n_events, ==, 2);

  /* test round 4: verify the property change on both sides */

  {
    g_autoptr (WpProperties) props =
        wp_proxy_get_properties (WP_PROXY (session));
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "changed-value");
  }
  {
    g_autoptr (WpProperties) props =
        wp_proxy_get_properties (fixture->proxy_session);
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "changed-value");
  }

  /* destroy impl session */
  fixture->n_events = 0;
  g_clear_object (&session);

  /* run until objects are destroyed */
  g_main_loop_run (fixture->loop);
  g_assert_cmpint (fixture->n_events, ==, 2);
  g_assert_null (fixture->impl_session);
  g_assert_null (fixture->proxy_session);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  pw_init (NULL, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add ("/wp/session/basic", TestSessionFixture, NULL,
      test_session_setup, test_session_basic, test_session_teardown);

  return g_test_run ();
}
