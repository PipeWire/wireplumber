/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "test-server.h"
#include "test-log.h"
#include <wp/wp.h>

typedef enum {
  WP_BASE_TEST_FLAG_CLIENT_CORE = (1<<0),
  WP_BASE_TEST_FLAG_DONT_CONNECT = (1<<1),
} WpBaseTestFlags;

typedef struct {
  /* the local pipewire server */
  WpTestServer server;

  /* the main loop */
  GMainContext *context;
  GMainLoop *loop;

  /* watchdog */
  GSource *timeout_source;

  /* our session manager core */
  WpCore *core;

  /* the "client" core, which receives proxies
    (second client to our internal server) */
  WpCore *client_core;

 /* Custom JSON config file if any,
  * this overrides the default conf file that is picked
  */
  gchar *conf_file;

} WpBaseTestFixture;

static gboolean
timeout_callback (WpBaseTestFixture * self)
{
  wp_critical ("test timed out");
  g_test_fail ();
  g_main_loop_quit (self->loop);

  return G_SOURCE_REMOVE;
}

static void
disconnected_callback (WpConnection *connection, WpBaseTestFixture * self)
{
  wp_critical_object (connection, "%s core lost its connection",
      (self->client_core && connection ==
          wp_core_get_connection (self->client_core)) ?
          "client" : "sm");
  g_test_fail ();
  g_main_loop_quit (self->loop);
}

static void
on_core_connected (WpObject * core, GAsyncResult * res, WpBaseTestFixture * self)
{
  g_autoptr (GError) error = NULL;
  wp_object_activate_finish (core, res, &error);
  g_assert_no_error (error);
  g_main_loop_quit (self->loop);
}

static void
wp_base_test_fixture_setup (WpBaseTestFixture * self, WpBaseTestFlags flags)
{
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (WpConf) conf = NULL;

  /* init test server */
  wp_test_server_setup (&self->server);

  /* init our main loop */
  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);
  g_main_context_push_thread_default (self->context);

  /* watchdog */
  self->timeout_source = g_timeout_source_new_seconds (8);
  g_source_set_callback (self->timeout_source, (GSourceFunc) timeout_callback,
      self, NULL);
  g_source_attach (self->timeout_source, self->context);

  /* init our core */
  props = wp_properties_new (PW_KEY_REMOTE_NAME, self->server.name, NULL);

  if (self->conf_file) {
    g_autoptr (GError) error = NULL;
    conf = wp_conf_new_open (self->conf_file, NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (conf);
  }

  self->core = wp_core_new (self->context, g_steal_pointer (&conf),
      wp_properties_ref (props));
  g_assert_true (self->core);

  g_signal_connect (wp_core_get_connection (self->core), "disconnected",
      (GCallback) disconnected_callback, self);

  if (!(flags & WP_BASE_TEST_FLAG_DONT_CONNECT)) {
    wp_object_activate (WP_OBJECT (self->core), WP_CORE_FEATURE_CONNECTED, NULL,
        (GAsyncReadyCallback) on_core_connected, self);
    g_main_loop_run (self->loop);
    g_assert_true (wp_object_test_active_features (WP_OBJECT (self->core),
        WP_CORE_FEATURE_CONNECTED));
  }

  /* init the second client's core */
  if (flags & WP_BASE_TEST_FLAG_CLIENT_CORE) {
    self->client_core = wp_core_new (self->context, NULL,
        wp_properties_ref (props));
    g_signal_connect (
        wp_core_get_connection (self->client_core), "disconnected",
        (GCallback) disconnected_callback, self);

    if (!(flags & WP_BASE_TEST_FLAG_DONT_CONNECT)) {
      wp_object_activate (WP_OBJECT (self->client_core), WP_CORE_FEATURE_CONNECTED,
          NULL, (GAsyncReadyCallback) on_core_connected, self);
      g_main_loop_run (self->loop);
      g_assert_true (wp_object_test_active_features (
        WP_OBJECT (self->client_core), WP_CORE_FEATURE_CONNECTED));
    }
  }
}

static void
test_core_done_cb (WpConnection * connection, GAsyncResult *res,
    WpBaseTestFixture *self)
{
  g_autoptr (GError) error = NULL;
  g_assert_cmpint (wp_connection_sync_finish (connection, res, &error), >=, 0);
  g_assert_null (error);
  g_main_loop_quit (self->loop);
}

static void
wp_base_test_fixture_teardown (WpBaseTestFixture * self)
{
  /* wait for all client core pending tasks to be done */
  if (self->client_core && wp_object_test_active_features (
        WP_OBJECT (self->client_core), WP_CORE_FEATURE_CONNECTED)) {
    wp_connection_sync (wp_core_get_connection (self->client_core), NULL,
        (GAsyncReadyCallback) test_core_done_cb, self);
    g_main_loop_run (self->loop);
    g_signal_handlers_disconnect_by_data (
        wp_core_get_connection (self->client_core), self);
    wp_object_deactivate (WP_OBJECT (self->client_core),
        WP_CORE_FEATURE_CONNECTED);
  }

  /* wait for all core pending tasks to be done */
  if (self->core && wp_object_test_active_features (WP_OBJECT (self->core),
        WP_CORE_FEATURE_CONNECTED)) {
    wp_connection_sync (wp_core_get_connection (self->core),
        NULL, (GAsyncReadyCallback) test_core_done_cb,
        self);
    g_main_loop_run (self->loop);
    g_signal_handlers_disconnect_by_data (
        wp_core_get_connection (self->core), self);
    wp_object_deactivate (WP_OBJECT (self->core), WP_CORE_FEATURE_CONNECTED);
  }

  /* double check and ensure that there is no event pending */
  while (g_main_context_pending (self->context))
    g_main_context_iteration (self->context, TRUE);

  g_main_context_pop_thread_default (self->context);
  g_clear_pointer (&self->conf_file, g_free);
  g_clear_object (&self->client_core);
  g_clear_object (&self->core);
  g_clear_pointer (&self->timeout_source, g_source_unref);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_clear_pointer (&self->context, g_main_context_unref);
  wp_test_server_teardown (&self->server);
}

static G_GNUC_UNUSED void
test_object_activate_finish_cb (WpObject * object, GAsyncResult * res,
    WpBaseTestFixture * f)
{
  g_autoptr (GError) error = NULL;
  gboolean augment_ret = wp_object_activate_finish (object, res, &error);
  g_assert_no_error (error);
  g_assert_true (augment_ret);

  g_main_loop_quit (f->loop);
}

static void
test_object_manager_installed (WpObjectManager * om, GAsyncResult * res,
    GMainLoop * loop)
{
  g_autoptr (GError) error = NULL;
  gboolean ret = wp_object_activate_finish (WP_OBJECT (om), res, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  if (loop)
    g_main_loop_quit (loop);
}

/* installs the object manager without waiting for it */
static G_GNUC_UNUSED void
test_object_manager_install (WpObjectManager * om)
{
  wp_object_activate (WP_OBJECT (om), WP_OBJECT_MANAGER_FEATURE_INSTALLED,
      NULL, (GAsyncReadyCallback) test_object_manager_installed, NULL);
}

static G_GNUC_UNUSED void
test_ensure_object_manager_is_installed (WpObjectManager * om,
    GMainLoop * loop)
{
  wp_object_activate (WP_OBJECT (om), WP_OBJECT_MANAGER_FEATURE_INSTALLED,
      NULL, (GAsyncReadyCallback) test_object_manager_installed, loop);
  g_main_loop_run (loop);
}

static G_GNUC_UNUSED gboolean
test_is_spa_lib_installed (WpBaseTestFixture *f, const gchar *factory_name) {
  struct spa_handle *handle;

  handle = pw_context_load_spa_handle (f->server.context, factory_name, NULL);
  if (!handle)
    return FALSE;

  pw_unload_spa_handle (handle);
  return TRUE;
}
