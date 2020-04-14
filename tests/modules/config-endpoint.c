/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>

#include <wp/wp.h>

#include "config-endpoint/endpoint-audiotestsrc.h"
#include "../wp/test-server.h"
#include "../../modules/module-config-endpoint/context.h"

typedef struct {
  WpTestServer server;
  GThread *loop_thread;
  GMainContext *context;
  GMainLoop *loop;
  GSource *timeout_source;
  WpCore *core;
} TestConfigEndpointFixture;

static gboolean
timeout_callback (TestConfigEndpointFixture *self)
{
  g_message ("test timed out");
  g_test_fail ();
  g_main_loop_quit (self->loop);

  return G_SOURCE_REMOVE;
}

static void
disconnected_callback (WpCore *core, TestConfigEndpointFixture *self)
{
  g_message ("core disconnected");
  g_test_fail ();
  g_main_loop_quit (self->loop);
}

static void
config_endpoint_setup (TestConfigEndpointFixture *self, gconstpointer data)
{
  g_autoptr (WpProperties) props = NULL;

  /* Create the server and load audiotestsrc */
  wp_test_server_setup (&self->server);
  pw_thread_loop_lock (self->server.thread_loop);
  pw_context_add_spa_lib (self->server.context, "audiotestsrc",
      "audiotestsrc/libspa-audiotestsrc");
  if (!pw_context_load_module (self->server.context,
        "libpipewire-module-spa-node", "audiotestsrc", NULL)) {
    pw_thread_loop_unlock (self->server.thread_loop);
    g_test_skip ("audiotestsrc SPA plugin is not installed");
    return;
  }
  pw_thread_loop_unlock (self->server.thread_loop);

  /* Create the main context and loop */
  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);
  g_main_context_push_thread_default (self->context);

  /* Set a timeout source */
  self->timeout_source = g_timeout_source_new_seconds (3);
  g_source_set_callback (self->timeout_source, (GSourceFunc) timeout_callback,
      self, NULL);
  g_source_attach (self->timeout_source, self->context);

  /* Create the core */
  props = wp_properties_new (PW_KEY_REMOTE_NAME, self->server.name, NULL);
  self->core = wp_core_new (self->context, props);
  g_signal_connect (self->core, "disconnected",
      (GCallback) disconnected_callback, self);

  /* Register the wp-endpoint-audiotestsrc */
  wp_factory_new (self->core, "wp-endpoint-audiotestsrc",
      wp_endpoint_audiotestsrc_factory);
}

static void
config_endpoint_teardown (TestConfigEndpointFixture *self, gconstpointer data)
{
  g_clear_object (&self->core);
  g_clear_pointer (&self->timeout_source, g_source_unref);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_clear_pointer (&self->context, g_main_context_unref);
  wp_test_server_teardown (&self->server);
}

static void
on_audiotestsrc_created (WpConfigEndpointContext *ctx, WpEndpoint *ep,
    TestConfigEndpointFixture *f)
{
  g_assert_nonnull (ep);
  g_main_loop_quit (f->loop);
}

static void
basic (TestConfigEndpointFixture *f, gconstpointer data)
{
  /* Set the configuration path */
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (f->core);
  g_assert_nonnull (config);
  wp_configuration_add_path (config, "config-endpoint/basic");

  /* Create the context and handle the endpoint-created callback */
  g_autoptr (WpConfigEndpointContext) ctx =
      wp_config_endpoint_context_new (f->core);
  g_assert_nonnull (ctx);
  g_assert_cmpint (wp_config_endpoint_context_get_length (ctx), ==, 0);

  /* Add a handler to stop the main loop when the endpoint is created */
  g_signal_connect (ctx, "endpoint-created",
      (GCallback) on_audiotestsrc_created, f);

  /* Connect */
  g_assert_true (wp_core_connect (f->core));

  /* Run the main loop */
  g_main_loop_run (f->loop);

  /* Check if the endpoint was created */
  g_assert_cmpint (wp_config_endpoint_context_get_length (ctx), ==, 1);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  pw_init (NULL, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add ("/modules/config-endpoint/basic", TestConfigEndpointFixture,
      NULL, config_endpoint_setup, basic, config_endpoint_teardown);

  return g_test_run ();
}
