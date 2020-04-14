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
  GMainContext *context;
  GMainLoop *loop;
  GSource *timeout_source;
  WpCore *core;
} TestConfigStaticNodesFixture;

static gboolean
timeout_callback (TestConfigStaticNodesFixture *self)
{
  g_message ("test timed out");
  g_test_fail ();
  g_main_loop_quit (self->loop);

  return G_SOURCE_REMOVE;
}

static void
disconnected_callback (WpCore *core, TestConfigStaticNodesFixture *self)
{
  g_message ("core disconnected");
  g_test_fail ();
  g_main_loop_quit (self->loop);
}

static void
config_static_nodes_setup (TestConfigStaticNodesFixture *self,
    gconstpointer data)
{
  g_autoptr (WpProperties) props = NULL;

  /* Create the server and load audioconvert plugin */
  wp_test_server_setup (&self->server);
  pw_thread_loop_lock (self->server.thread_loop);
  pw_context_add_spa_lib (self->server.context, "audio.convert*",
      "audioconvert/libspa-audioconvert");
  pw_context_load_module (self->server.context,
      "libpipewire-module-spa-node-factory", NULL, NULL);
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
}

static void
config_static_nodes_teardown (TestConfigStaticNodesFixture *self,
    gconstpointer data)
{
  g_clear_object (&self->core);
  g_clear_pointer (&self->timeout_source, g_source_unref);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_clear_pointer (&self->context, g_main_context_unref);
  wp_test_server_teardown (&self->server);
}

static void
on_node_created (WpConfigStaticNodesContext *ctx, WpProxy *proxy,
    TestConfigStaticNodesFixture *f)
{
  g_assert_nonnull (proxy);
  g_main_loop_quit (f->loop);
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
  g_assert_cmpint (wp_config_static_nodes_context_get_length (ctx), ==, 0);

  /* Add a handler to stop the main loop when a node is created */
  g_signal_connect (ctx, "node-created", (GCallback) on_node_created, f);

  /* Connect */
  g_assert_true (wp_core_connect (f->core));

  /* Run the main loop */
  g_main_loop_run (f->loop);

  /* Check if the node was created */
  g_assert_cmpint (wp_config_static_nodes_context_get_length (ctx), ==, 1);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  pw_init (NULL, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add ("/modules/config-static-nodes/basic",
      TestConfigStaticNodesFixture, NULL,
      config_static_nodes_setup, basic, config_static_nodes_teardown);

  return g_test_run ();
}
