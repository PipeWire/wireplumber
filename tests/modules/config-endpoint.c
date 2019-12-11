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

  GMutex mutex;
  GCond cond;
  gboolean created;

  GThread *loop_thread;
  GMainContext *context;
  GMainLoop *loop;

  WpCore *core;
} TestConfigEndpointFixture;

void wait_for_created (TestConfigEndpointFixture *self)
{
  g_mutex_lock (&self->mutex);
  while (!self->created)
    g_cond_wait (&self->cond, &self->mutex);
  self->created = FALSE;
  g_mutex_unlock (&self->mutex);
}

void signal_created (TestConfigEndpointFixture *self)
{
  g_mutex_lock (&self->mutex);
  self->created = TRUE;
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->mutex);
}

static void
create_audiotestsrc (TestConfigEndpointFixture *self)
{
  pw_thread_loop_lock (self->server.thread_loop);
  pw_core_add_spa_lib (self->server.core, "audiotestsrc",
      "audiotestsrc/libspa-audiotestsrc");
  if (!pw_module_load (self->server.core, "libpipewire-module-spa-node",
        "audiotestsrc", NULL)) {
    pw_thread_loop_unlock (self->server.thread_loop);
    g_test_skip ("audiotestsrc SPA plugin is not installed");
    return;
  }
  pw_thread_loop_unlock (self->server.thread_loop);
}

static void
on_connected (WpCore *core, enum pw_remote_state new_state,
    TestConfigEndpointFixture *self)
{
  /* Register the wp-endpoint-audiotestsrc */
  wp_factory_new (self->core, "wp-endpoint-audiotestsrc",
      wp_endpoint_audiotestsrc_factory);

  /* Create the audiotestsrc node */
  create_audiotestsrc (self);

  /* Signal we are done */
  signal_created (self);
}

static void *
loop_thread_start (void *d)
{
  TestConfigEndpointFixture *self = d;

  /* Create the main loop using the default thread context */
  self->context = g_main_context_get_thread_default ();
  self->loop = g_main_loop_new (self->context, FALSE);

  /* Create the server */
  wp_test_server_setup (&self->server);

  /* Create the core and connect to the server */
  g_autoptr (WpProperties) props = NULL;
  props = wp_properties_new (PW_KEY_REMOTE_NAME, self->server.name, NULL);
  self->core = wp_core_new (self->context, props);
  g_signal_connect (self->core, "remote-state-changed::connected",
      (GCallback) on_connected, self);
  wp_core_connect (self->core);

  /* Run the main loop */
  g_main_loop_run (self->loop);

  /* Clean up */
  g_clear_object (&self->core);
  wp_test_server_teardown (&self->server);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  return NULL;
}

static void
config_endpoint_setup (TestConfigEndpointFixture *self, gconstpointer data)
{
  /* Data */
  g_mutex_init (&self->mutex);
  g_cond_init (&self->cond);
  self->created = FALSE;

  /* Initialize main loop, server and core in a thread */
  self->loop_thread = g_thread_new("loop-thread", &loop_thread_start, self);

  /* Wait for everything to be created */
  wait_for_created (self);
}

static gboolean
loop_thread_stop (gpointer data)
{
  TestConfigEndpointFixture *self = data;
  g_main_loop_quit (self->loop);
  return G_SOURCE_REMOVE;
}

static void
config_endpoint_teardown (TestConfigEndpointFixture *self, gconstpointer data)
{
  /* Stop the main loop and wait until it is done */
  g_autoptr (GSource) source = g_idle_source_new ();
  g_source_set_callback (source, loop_thread_stop, self, NULL);
  g_source_attach (source, self->context);
  g_thread_join (self->loop_thread);

  /* Data */
  g_mutex_clear (&self->mutex);
  g_cond_clear (&self->cond);
  self->created = FALSE;
}

static void
on_audiotestsrc_created (WpConfigEndpointContext *ctx, WpEndpoint *ep,
    TestConfigEndpointFixture *f)
{
  g_assert_nonnull (ep);
  signal_created (f);
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
  g_signal_connect (ctx, "endpoint-created",
      (GCallback) on_audiotestsrc_created, f);

  /* Wait for the endpoint to be created */
  wait_for_created (f);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  pw_init (NULL, NULL);

  g_test_add ("/modules/config-endpoint/basic", TestConfigEndpointFixture,
      NULL, config_endpoint_setup, basic, config_endpoint_teardown);

  return g_test_run ();
}
