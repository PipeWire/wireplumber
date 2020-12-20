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
  WpSession *session;
} TestFixture;

static WpSessionItem *
load_item (TestFixture * f, const gchar * factory, const gchar * media_class)
{
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpSessionItem) item = NULL;
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);

  /* create item */

  item = wp_session_item_make (f->base.core, "si-simple-node-endpoint");
  g_assert_nonnull (item);

  node = wp_node_new_from_factory (f->base.core,
      "spa-node-factory",
      wp_properties_new (
          "factory.name", factory,
          "node.name", factory,
          "node.autoconnect", "true",
          NULL));
  g_assert_nonnull (node);

  wp_proxy_augment (WP_PROXY (node), WP_PROXY_FEATURES_STANDARD, NULL,
      (GAsyncReadyCallback) test_proxy_augment_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* configure */

  g_variant_builder_add (&b, "{sv}", "node",
      g_variant_new_uint64 ((guint64) node));
  g_variant_builder_add (&b, "{sv}", "media-class",
      g_variant_new_string (media_class));
  g_assert_true (wp_session_item_configure (item, g_variant_builder_end (&b)));

  /* activate */

  wp_session_item_activate (item,
      (GAsyncReadyCallback) test_si_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* export */

  wp_session_item_export (item, f->session,
      (GAsyncReadyCallback) test_si_export_finish_cb, f);
  g_main_loop_run (f->base.loop);

  return g_steal_pointer (&item);
}

static WpSessionItem *
load_adapter_item (TestFixture * f, const gchar * factory,
    const gchar * media_class, guint num_streams)
{
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpSessionItem) adapter = NULL;
  g_autoptr (WpSessionItem) item = NULL;

  /* create item */

  adapter = wp_session_item_make (f->base.core, "si-adapter");
  g_assert_nonnull (adapter);
  item = wp_session_item_make (f->base.core, "si-audio-softdsp-endpoint");
  g_assert_nonnull (item);

  node = wp_node_new_from_factory (f->base.core,
      "adapter",
      wp_properties_new (
          "factory.name", factory,
          "node.name", factory,
          "node.autoconnect", "true",
          NULL));
  g_assert_nonnull (node);

  wp_proxy_augment (WP_PROXY (node), WP_PROXY_FEATURES_STANDARD, NULL,
      (GAsyncReadyCallback) test_proxy_augment_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* configure adapter */

  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "node",
        g_variant_new_uint64 ((guint64) node));
    g_variant_builder_add (&b, "{sv}", "media-class",
        g_variant_new_string (media_class));
    g_assert_true (wp_session_item_configure (adapter, g_variant_builder_end (&b)));
  }

  /* configure item */

  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "adapter",
        g_variant_new_uint64 ((guint64) adapter));
    g_assert_true (
        wp_session_item_configure (item, g_variant_builder_end (&b)));
  }

  /* add the streams */

  for (guint i = 0; i < num_streams; i++) {
      g_autoptr (WpSessionItem) stream =
          wp_session_item_make (f->base.core, "si-convert");
      g_assert_nonnull (stream);
      g_assert_true (WP_IS_SI_STREAM (stream));

      {
        g_auto (GVariantBuilder) b =
            G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add (&b, "{sv}", "target",
            g_variant_new_uint64 ((guint64) adapter));
        g_variant_builder_add (&b, "{sv}", "name",
            g_variant_new_printf ("stream-%u", i));
        g_assert_true (
            wp_session_item_configure (stream, g_variant_builder_end (&b)));
      }

      g_assert_true (wp_session_bin_add (WP_SESSION_BIN (item),
              g_steal_pointer (&stream)));
    }

  /* activate */

  wp_session_item_activate (item,
      (GAsyncReadyCallback) test_si_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* export */

  wp_session_item_export (item, f->session,
      (GAsyncReadyCallback) test_si_export_finish_cb, f);
  g_main_loop_run (f->base.loop);

  return g_steal_pointer (&item);
}


static void
config_policy_setup (TestFixture *f, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&f->base, 0);

  /* load modules */
  {
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&f->base.server);

    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "fake*", "test/libspa-test"), ==, 0);
    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), ==, 0);
    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "audio.convert", "audioconvert/libspa-audioconvert"), ==, 0);
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-spa-node-factory", NULL, NULL));
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-adapter", NULL, NULL));
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-link-factory", NULL, NULL));
  }
  {
    g_autoptr (GError) error = NULL;
    WpModule *module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-si-simple-node-endpoint", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
    module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-si-adapter", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
    module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-si-convert", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
    module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-si-audio-softdsp-endpoint", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
    module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-si-standard-link", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
    module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-config-policy", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
  }

  g_assert_nonnull (
      f->session = WP_SESSION (wp_impl_session_new (f->base.core)));
  wp_impl_session_set_property (WP_IMPL_SESSION (f->session),
      "session.name", "audio");
  wp_proxy_augment (WP_PROXY (f->session), WP_SESSION_FEATURES_STANDARD, NULL,
      (GAsyncReadyCallback) test_proxy_augment_finish_cb, f);
  g_main_loop_run (f->base.loop);
}

static void
config_policy_teardown (TestFixture *f, gconstpointer user_data)
{
  g_clear_object (&f->session);
  wp_base_test_fixture_teardown (&f->base);
}

static void
on_state_changed (WpEndpointLink *ep_link, WpEndpointLinkState old_state,
    WpEndpointLinkState new_state, const gchar *error, TestFixture *f)
{
  switch (new_state) {
  case WP_ENDPOINT_LINK_STATE_ACTIVE:
    break;
  case WP_ENDPOINT_LINK_STATE_ERROR:
    wp_warning ("link failed: %s", error);
    g_test_fail ();
  case WP_ENDPOINT_LINK_STATE_INACTIVE:
  case WP_ENDPOINT_LINK_STATE_PREPARING:
  default:
    return;
  }

  g_main_loop_quit (f->base.loop);
}

static void
on_link_created (WpPlugin *ctx, WpEndpointLink *ep_link,
    TestFixture *f)
{
  g_assert_nonnull (ep_link);
  g_signal_connect (ep_link, "state-changed", (GCallback) on_state_changed, f);
}

static void
playback (TestFixture *f, gconstpointer data)
{
  /* Set the configuration path */
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (f->base.core);
  g_assert_nonnull (config);
  wp_configuration_add_path (config, "config-policy/playback");

  /* Find the plugin context and handle the link-activated callback */
  g_autoptr (WpObjectManager) om = wp_object_manager_new ();
  wp_object_manager_add_interest (om, WP_TYPE_PLUGIN, NULL);
  wp_core_install_object_manager (f->base.core, om);
  g_autoptr (WpPlugin) ctx = wp_object_manager_lookup (om, WP_TYPE_PLUGIN, NULL);
  g_assert_nonnull (ctx);
  g_signal_connect (ctx, "link-created", (GCallback) on_link_created, f);

  /* Activate */
  wp_plugin_activate (ctx);

  /* Create the items and make sure a link is created */
  g_autoptr (WpSessionItem) sink = load_item (f, "fakesink", "Audio/Sink");
  g_assert_nonnull (sink);
  g_autoptr (WpSessionItem) source = load_adapter_item (f, "audiotestsrc",
      "Audio/Source", 2);
  g_assert_nonnull (source);
  g_main_loop_run (f->base.loop);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/modules/config-policy/playback", TestFixture,
      NULL, config_policy_setup, playback, config_policy_teardown);

  return g_test_run ();
}
