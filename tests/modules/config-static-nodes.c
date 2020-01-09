/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>

#include <wp/wp.h>

#include "../wp/test-server.h"
#include "../../modules/module-config-static-nodes/context.h"

typedef struct {
  WpTestServer server;

  GMutex mutex;
  GCond cond;
  gboolean created;

  GThread *loop_thread;
  GMainContext *context;
  GMainLoop *loop;

  WpCore *core;
} TestConfigStaticNodesFixture;

void wait_for_created (TestConfigStaticNodesFixture *self)
{
  g_mutex_lock (&self->mutex);
  while (!self->created)
    g_cond_wait (&self->cond, &self->mutex);
  self->created = FALSE;
  g_mutex_unlock (&self->mutex);
}

void signal_created (TestConfigStaticNodesFixture *self)
{
  g_mutex_lock (&self->mutex);
  self->created = TRUE;
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->mutex);
}

static void *
loop_thread_start (void *d)
{
  TestConfigStaticNodesFixture *self = d;

  /* Create the main loop using the default thread context */
  self->context = g_main_context_get_thread_default ();
  self->loop = g_main_loop_new (self->context, FALSE);

  /* Create the server */
  wp_test_server_setup (&self->server);

  /* Add the audioconvert SPA library */
  pw_context_add_spa_lib (self->server.context, "audio.convert*",
      "audioconvert/libspa-audioconvert");

  /* Create the core and connect to the server */
  g_autoptr (WpProperties) props = NULL;
  props = wp_properties_new (PW_KEY_REMOTE_NAME, self->server.name, NULL);
  self->core = wp_core_new (self->context, props);

  /* Signal we are done */
  signal_created (self);

  /* Run the main loop */
  g_main_loop_run (self->loop);

  /* Clean up */
  g_clear_object (&self->core);
  wp_test_server_teardown (&self->server);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  return NULL;
}

static void
config_static_nodes_setup (TestConfigStaticNodesFixture *self,
    gconstpointer data)
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
  TestConfigStaticNodesFixture *self = data;
  g_main_loop_quit (self->loop);
  return G_SOURCE_REMOVE;
}

static void
config_static_nodes_teardown (TestConfigStaticNodesFixture *self,
    gconstpointer data)
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
on_node_created (WpConfigStaticNodesContext *ctx, WpProxy *proxy,
    TestConfigStaticNodesFixture *f)
{
  g_assert_nonnull (proxy);
  signal_created (f);
}

static void
basic (TestConfigStaticNodesFixture *f, gconstpointer data)
{
  /* Set the configuration path */
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (f->core);
  g_assert_nonnull (config);
  wp_configuration_add_path (config, "config-static-nodes/basic");

  /* Create the context */
  g_autoptr (WpConfigStaticNodesContext) ctx =
      wp_config_static_nodes_context_new (f->core);
  g_assert_nonnull (ctx);
  g_signal_connect (ctx, "node-created", (GCallback) on_node_created, f);

  /* Connect */
  g_assert_true (wp_core_connect (f->core));

  /* Wait for the node to be created */
  wait_for_created (f);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  pw_init (NULL, NULL);

  g_test_add ("/modules/config-static-nodes/basic",
      TestConfigStaticNodesFixture, NULL,
      config_static_nodes_setup, basic, config_static_nodes_teardown);

  return g_test_run ();
}
