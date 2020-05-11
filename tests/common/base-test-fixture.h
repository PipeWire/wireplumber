/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "test-server.h"
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

} WpBaseTestFixture;

static gboolean
timeout_callback (WpBaseTestFixture * self)
{
  wp_message ("test timed out");
  g_test_fail ();
  g_main_loop_quit (self->loop);

  return G_SOURCE_REMOVE;
}

static void
disconnected_callback (WpCore *core, WpBaseTestFixture * self)
{
  wp_message_object (core, "%s core disconnected",
      (core == self->client_core) ? "client" : "sm");
  g_test_fail ();
  g_main_loop_quit (self->loop);
}

static void
wp_base_test_fixture_setup (WpBaseTestFixture * self, WpBaseTestFlags flags)
{
  g_autoptr (WpProperties) props = NULL;

  /* init test server */
  wp_test_server_setup (&self->server);

  /* init our main loop */
  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);
  g_main_context_push_thread_default (self->context);

  /* watchdog */
  self->timeout_source = g_timeout_source_new_seconds (3);
  g_source_set_callback (self->timeout_source, (GSourceFunc) timeout_callback,
      self, NULL);
  g_source_attach (self->timeout_source, self->context);

  /* init our core */
  props = wp_properties_new (PW_KEY_REMOTE_NAME, self->server.name, NULL);
  self->core = wp_core_new (self->context, wp_properties_ref (props));
  g_signal_connect (self->core, "disconnected",
      (GCallback) disconnected_callback, self);

  if (!(flags & WP_BASE_TEST_FLAG_DONT_CONNECT))
    g_assert_true (wp_core_connect (self->core));

  /* init the second client's core */
  if (flags & WP_BASE_TEST_FLAG_CLIENT_CORE) {
    self->client_core = wp_core_new (self->context, wp_properties_ref (props));
    g_signal_connect (self->client_core, "disconnected",
        (GCallback) disconnected_callback, self);

    if (!(flags & WP_BASE_TEST_FLAG_DONT_CONNECT))
      g_assert_true (wp_core_connect (self->client_core));
  }
}

static void
wp_base_test_fixture_teardown (WpBaseTestFixture * self)
{
  g_main_context_pop_thread_default (self->context);

  g_clear_object (&self->client_core);
  g_clear_object (&self->core);
  g_clear_pointer (&self->timeout_source, g_source_unref);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_clear_pointer (&self->context, g_main_context_unref);
  wp_test_server_teardown (&self->server);
}

static G_GNUC_UNUSED void
test_proxy_augment_finish_cb (WpProxy * proxy, GAsyncResult * res,
    WpBaseTestFixture * f)
{
  g_autoptr (GError) error = NULL;
  gboolean augment_ret = wp_proxy_augment_finish (proxy, res, &error);
  g_assert_no_error (error);
  g_assert_true (augment_ret);

  g_main_loop_quit (f->loop);
}

static G_GNUC_UNUSED void
test_si_activate_finish_cb (WpSessionItem * item, GAsyncResult * res,
    WpBaseTestFixture * f)
{
  g_autoptr (GError) error = NULL;
  gboolean activate_ret = wp_session_item_activate_finish (item, res, &error);
  g_assert_no_error (error);
  g_assert_true (activate_ret);

  g_main_loop_quit (f->loop);
}

static G_GNUC_UNUSED void
test_si_export_finish_cb (WpSessionItem * item, GAsyncResult * res,
    WpBaseTestFixture * f)
{
  g_autoptr (GError) error = NULL;
  gboolean export_ret = wp_session_item_export_finish (item, res, &error);
  g_assert_no_error (error);
  g_assert_true (export_ret);

  g_main_loop_quit (f->loop);
}

static G_GNUC_UNUSED void
test_ensure_object_manager_is_installed (WpObjectManager * om, WpCore * core,
    GMainLoop * loop)
{
  gulong id = g_signal_connect_swapped (om, "installed",
      G_CALLBACK (g_main_loop_quit), loop);
  wp_core_install_object_manager (core, om);
  if (!wp_object_manager_is_installed (om))
    g_main_loop_run (loop);
  g_signal_handler_disconnect (om, id);
}
