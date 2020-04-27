/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
} TestFixture;

typedef struct {
  const gchar *factory;
  const gchar *name;
  const gchar *media_class;
  const gchar *expected_media_class;
  WpDirection expected_direction;
} TestData;

static void
test_si_audio_softdsp_endpoint_setup (TestFixture * f, gconstpointer user_data)
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
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-spa-node-factory", NULL, NULL));
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-adapter", NULL, NULL));
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
}

static void
test_si_audio_softdsp_endpoint_teardown (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_teardown (&f->base);
}

static void
test_si_audio_softdsp_endpoint_configure_activate (TestFixture * f,
    gconstpointer user_data)
{
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpSessionItem) adapter = NULL;
  g_autoptr (WpSessionItem) endpoint = NULL;

  /* create audiotestsrc adapter node */
  node = wp_node_new_from_factory (f->base.core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", "audiotestsrc.adapter",
          NULL));
  g_assert_nonnull (node);
  wp_proxy_augment (WP_PROXY (node), WP_PROXY_FEATURES_STANDARD, NULL,
      (GAsyncReadyCallback) test_proxy_augment_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* create adapter */
  adapter = wp_session_item_make (f->base.core, "si-adapter");
  g_assert_nonnull (adapter);
  g_assert_true (WP_IS_SI_ENDPOINT (adapter));

  /* configure adapter */
  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "node",
        g_variant_new_uint64 ((guint64) node));
    g_assert_true (
        wp_session_item_configure (adapter, g_variant_builder_end (&b)));
  }

  g_assert_cmphex (wp_session_item_get_flags (adapter), ==,
      WP_SI_FLAG_CONFIGURED);

  {
    g_autoptr (GVariant) v = wp_session_item_get_configuration (adapter);
    guint64 node_i;
    const gchar *str;
    guchar dir;
    guint32 prio;
    guint32 channels;
    g_assert_nonnull (v);
    g_assert_true (g_variant_lookup (v, "node", "t", &node_i));
    g_assert_cmpuint (node_i, ==, (guint64) node);
    g_assert_true (g_variant_lookup (v, "name", "&s", &str));
    g_assert_cmpstr (str, ==, "audiotestsrc.adapter");
    g_assert_true (g_variant_lookup (v, "media-class", "&s", &str));
    g_assert_cmpstr (str, ==, "Audio/Source");
    g_assert_true (g_variant_lookup (v, "direction", "y", &dir));
    g_assert_cmpuint (dir, ==, WP_DIRECTION_OUTPUT);
    g_assert_true (g_variant_lookup (v, "priority", "u", &prio));
    g_assert_cmpuint (prio, ==, 0);
    g_assert_true (g_variant_lookup (v, "channels", "u", &channels));
    g_assert_cmpuint (channels, ==, 0);
  }

  /* create audio softdsp endpoint */
  endpoint = wp_session_item_make (f->base.core, "si-audio-softdsp-endpoint");
  g_assert_nonnull (endpoint);
  g_assert_true (WP_IS_SI_ENDPOINT (endpoint));

  /* configure audio softdsp endpoint */
  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "adapter",
        g_variant_new_uint64 ((guint64) adapter));
    g_variant_builder_add (&b, "{sv}", "num-streams",
        g_variant_new_uint32 (4));
    g_assert_true (
        wp_session_item_configure (endpoint, g_variant_builder_end (&b)));
  }

  g_assert_cmphex (wp_session_item_get_flags (endpoint), ==,
      WP_SI_FLAG_CONFIGURED);

  {
    g_autoptr (GVariant) v = wp_session_item_get_configuration (endpoint);
    guint64 adapter_i;
    guint32 ns;
    g_assert_nonnull (v);
    g_assert_true (g_variant_lookup (v, "adapter", "t", &adapter_i));
    g_assert_cmpuint (adapter_i, ==, (guint64) adapter);
    g_assert_true (g_variant_lookup (v, "num-streams", "u", &ns));
    g_assert_cmpuint (ns, ==, 4);
  }

  /* activate audio softdsp endpoint */
  {
    wp_session_item_activate (endpoint,
        (GAsyncReadyCallback) test_si_activate_finish_cb, f);
    g_main_loop_run (f->base.loop);
  }

  g_assert_cmphex (wp_session_item_get_flags (endpoint), ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_ACTIVE);

  /* deactivate - configuration should not be altered  */

  wp_session_item_deactivate (endpoint);

  g_assert_cmphex (wp_session_item_get_flags (endpoint), ==,
      WP_SI_FLAG_CONFIGURED);

  {
    g_autoptr (GVariant) v = wp_session_item_get_configuration (endpoint);
    guint64 adapter_i;
    guint32 ns;
    g_assert_nonnull (v);
    g_assert_true (g_variant_lookup (v, "adapter", "t", &adapter_i));
    g_assert_cmpuint (adapter_i, ==, (guint64) adapter);
    g_assert_true (g_variant_lookup (v, "num-streams", "u", &ns));
    g_assert_cmpuint (ns, ==, 4);
  }
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  pw_init (NULL, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  /* configure-activate */

  g_test_add ("/modules/si-audio-softdsp-endpoint/configure-activate",
      TestFixture, NULL,
      test_si_audio_softdsp_endpoint_setup,
      test_si_audio_softdsp_endpoint_configure_activate,
      test_si_audio_softdsp_endpoint_teardown);

  return g_test_run ();
}
