/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"
#include "../../modules/module-config-static-nodes/context.h"

typedef struct {
  WpBaseTestFixture base;
} TestConfigStaticNodesFixture;

static void
config_static_nodes_setup (TestConfigStaticNodesFixture *self,
    gconstpointer data)
{
  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_DONT_CONNECT);

  /* load audioconvert plugin */
  pw_thread_loop_lock (self->base.server.thread_loop);
  pw_context_add_spa_lib (self->base.server.context, "audio.convert*",
      "audioconvert/libspa-audioconvert");
  pw_context_load_module (self->base.server.context,
      "libpipewire-module-spa-node-factory", NULL, NULL);
  pw_thread_loop_unlock (self->base.server.thread_loop);
}

static void
config_static_nodes_teardown (TestConfigStaticNodesFixture *self,
    gconstpointer data)
{
  wp_base_test_fixture_teardown (&self->base);
}

static void
on_node_created (WpConfigStaticNodesContext *ctx, WpProxy *proxy,
    TestConfigStaticNodesFixture *f)
{
  g_assert_nonnull (proxy);
  g_main_loop_quit (f->base.loop);
}

static void
basic (TestConfigStaticNodesFixture *f, gconstpointer data)
{
  /* Set the configuration path */
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (f->base.core);
  g_assert_nonnull (config);
  wp_configuration_add_path (config, "config-static-nodes/basic");

  /* Create the context */
  g_autoptr (WpConfigStaticNodesContext) ctx =
      wp_config_static_nodes_context_new (f->base.core);
  g_assert_nonnull (ctx);
  g_assert_cmpint (wp_config_static_nodes_context_get_length (ctx), ==, 0);

  /* Add a handler to stop the main loop when a node is created */
  g_signal_connect (ctx, "node-created", (GCallback) on_node_created, f);

  /* Connect */
  g_assert_true (wp_core_connect (f->base.core));

  /* Run the main loop */
  g_main_loop_run (f->base.loop);

  /* Check if the node was created */
  g_assert_cmpint (wp_config_static_nodes_context_get_length (ctx), ==, 1);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/modules/config-static-nodes/basic",
      TestConfigStaticNodesFixture, NULL,
      config_static_nodes_setup, basic, config_static_nodes_teardown);

  return g_test_run ();
}
