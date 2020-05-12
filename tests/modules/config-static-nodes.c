/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

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

  /* load wireplumber module */
  g_autoptr (GError) error = NULL;
  WpModule *module = wp_module_load (self->base.core, "C",
      "libwireplumber-module-config-static-nodes", NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (module);
}

static void
config_static_nodes_teardown (TestConfigStaticNodesFixture *self,
    gconstpointer data)
{
  wp_base_test_fixture_teardown (&self->base);
}

static void
on_node_created (WpPlugin *ctx, WpProxy *proxy, TestConfigStaticNodesFixture *f)
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

  /* Find the plugin context and handle the node-created callback */
  g_autoptr (WpObjectManager) om = wp_object_manager_new ();
  wp_object_manager_add_interest_1 (om, WP_TYPE_PLUGIN, NULL);
  wp_core_install_object_manager (f->base.core, om);
  g_autoptr (WpPlugin) ctx = wp_object_manager_lookup (om, WP_TYPE_PLUGIN, NULL);
  g_assert_nonnull (ctx);
  g_signal_connect (ctx, "node-created", (GCallback) on_node_created, f);

  /* Activate */
  wp_plugin_activate (ctx);

  /* Connect */
  g_assert_true (wp_core_connect (f->base.core));

  /* Run the main loop */
  g_main_loop_run (f->base.loop);
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
