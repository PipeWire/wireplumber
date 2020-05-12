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
} TestConfigEndpointFixture;

static void
config_endpoint_setup (TestConfigEndpointFixture *f, gconstpointer data)
{
  wp_base_test_fixture_setup (&f->base, 0);

  /* load modules */
  {
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&f->base.server);

    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), ==, 0);
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-spa-node-factory", NULL, NULL));
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-adapter", NULL, NULL));
  }
  {
    g_autoptr (GError) error = NULL;
    WpModule *module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-si-simple-node-endpoint", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
  }
  {
    g_autoptr (GError) error = NULL;
    WpModule *module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-si-adapter", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
  }
  {
    g_autoptr (GError) error = NULL;
    WpModule *module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-si-convert", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
  }
  {
    g_autoptr (GError) error = NULL;
    WpModule *module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-si-audio-softdsp-endpoint", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
  }
  {
    g_autoptr (GError) error = NULL;
    WpModule *module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-config-endpoint", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
  }
}

static void
config_endpoint_teardown (TestConfigEndpointFixture *f, gconstpointer data)
{
  wp_base_test_fixture_teardown (&f->base);
}

static void
on_audiotestsrc_simple_endpoint_created (GObject *ctx,
    WpSessionItem *ep, TestConfigEndpointFixture *f)
{
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpProperties) props = NULL;
  g_assert_nonnull (ep);

  g_autoptr (GVariant) v = wp_session_item_get_configuration (ep);
  const gchar *str;
  guint32 prio;
  g_assert_true (g_variant_lookup (v, "name", "&s", &str));
  g_assert_cmpstr (str, ==, "audiotestsrc-endpoint");
  g_assert_true (g_variant_lookup (v, "media-class", "&s", &str));
  g_assert_cmpstr (str, ==, "Audio/Source");
  g_assert_true (g_variant_lookup (v, "role", "&s", &str));
  g_assert_cmpstr (str, ==, "Multimedia");
  g_assert_true (g_variant_lookup (v, "priority", "u", &prio));
  g_assert_cmpuint (prio, ==, 0);

  g_main_loop_quit (f->base.loop);
}

static void
on_audiotestsrc_streams_endpoint_created (GObject *ctx,
    WpSessionItem *ep, TestConfigEndpointFixture *f)
{
  g_assert_nonnull (ep);
  g_assert_cmpuint (5, ==, wp_session_bin_get_n_children (WP_SESSION_BIN (ep)));

  g_autoptr (GVariant) v = wp_session_item_get_configuration (ep);
  guint64 p_i;
  g_assert_true (g_variant_lookup (v, "adapter", "t", &p_i));
  g_assert_nonnull ((gpointer)p_i);

  g_autoptr (GVariant) v2 = wp_session_item_get_configuration ((gpointer)p_i);
  const gchar *str;
  guint32 prio;
  g_assert_true (g_variant_lookup (v2, "name", "&s", &str));
  g_assert_cmpstr (str, ==, "audiotestsrc-endpoint");
  g_assert_true (g_variant_lookup (v2, "media-class", "&s", &str));
  g_assert_cmpstr (str, ==, "Audio/Source");
  g_assert_true (g_variant_lookup (v2, "role", "&s", &str));
  g_assert_cmpstr (str, ==, "Multimedia");
  g_assert_true (g_variant_lookup (v2, "priority", "u", &prio));
  g_assert_cmpuint (prio, ==, 0);

  g_main_loop_quit (f->base.loop);
}

static void
simple (TestConfigEndpointFixture *f, gconstpointer data)
{
  /* Set the configuration path */
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (f->base.core);
  g_assert_nonnull (config);
  wp_configuration_add_path (config, "config-endpoint/simple");

  /* Find the plugin context and handle the endpoint-created callback */
  g_autoptr (WpObjectManager) om = wp_object_manager_new ();
  wp_object_manager_add_interest_1 (om, WP_TYPE_PLUGIN, NULL);
  wp_core_install_object_manager (f->base.core, om);

  g_autoptr (WpPlugin) ctx = wp_object_manager_lookup (om, WP_TYPE_PLUGIN, NULL);
  g_assert_nonnull (ctx);
  g_signal_connect (ctx, "endpoint-created",
      (GCallback) on_audiotestsrc_simple_endpoint_created, f);

  /* Create and export the default session */
  g_autoptr (WpImplSession) session = wp_impl_session_new (f->base.core);
  wp_impl_session_set_property (session, "session.name", "default");
  wp_proxy_augment (WP_PROXY (session), WP_SESSION_FEATURES_STANDARD, NULL,
      (GAsyncReadyCallback) test_proxy_augment_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* Activate */
  wp_plugin_activate (ctx);

  /* Create the audiotestsrc node and run until the endpoint is created */
  g_autoptr (WpNode) node = wp_node_new_from_factory (f->base.core,
      "spa-node-factory",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", "audiotestsrc0",
          NULL));
  g_assert_nonnull (node);
  g_main_loop_run (f->base.loop);
}

static void
streams (TestConfigEndpointFixture *f, gconstpointer data)
{
  /* Set the configuration path */
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (f->base.core);
  g_assert_nonnull (config);
  wp_configuration_add_path (config, "config-endpoint/streams");

  /* Find the plugin context and handle the endpoint-created callback */
  g_autoptr (WpObjectManager) om = wp_object_manager_new ();
  wp_object_manager_add_interest_1 (om, WP_TYPE_PLUGIN, NULL);
  wp_core_install_object_manager (f->base.core, om);

  g_autoptr (WpPlugin) ctx = wp_object_manager_lookup (om, WP_TYPE_PLUGIN, NULL);
  g_assert_nonnull (ctx);
  g_signal_connect (ctx, "endpoint-created",
      (GCallback) on_audiotestsrc_streams_endpoint_created, f);

  /* Create and export the default session */
  g_autoptr (WpImplSession) session = wp_impl_session_new (f->base.core);
  wp_impl_session_set_property (session, "session.name", "default");
  wp_proxy_augment (WP_PROXY (session), WP_SESSION_FEATURES_STANDARD, NULL,
      (GAsyncReadyCallback) test_proxy_augment_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* Activate */
  wp_plugin_activate (ctx);

  /* create audiotestsrc adapter node and run until the endpoint is created */
  g_autoptr (WpNode) node = wp_node_new_from_factory (f->base.core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", "adapter-audiotestsrc0",
          NULL));
  g_assert_nonnull (node);
  g_main_loop_run (f->base.loop);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/modules/config-endpoint/simple", TestConfigEndpointFixture,
      NULL, config_endpoint_setup, simple, config_endpoint_teardown);
  g_test_add ("/modules/config-endpoint/streams", TestConfigEndpointFixture,
      NULL, config_endpoint_setup, streams, config_endpoint_teardown);

  return g_test_run ();
}
