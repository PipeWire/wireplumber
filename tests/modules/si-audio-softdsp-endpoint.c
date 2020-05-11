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
  guint requested_streams = GPOINTER_TO_UINT (user_data);
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
    g_assert_true (
        wp_session_item_configure (endpoint, g_variant_builder_end (&b)));
  }

  g_assert_cmphex (wp_session_item_get_flags (endpoint), ==,
      WP_SI_FLAG_CONFIGURED);

  {
    g_autoptr (GVariant) v = wp_session_item_get_configuration (endpoint);
    guint64 adapter_i;
    g_assert_nonnull (v);
    g_assert_true (g_variant_lookup (v, "adapter", "t", &adapter_i));
    g_assert_cmpuint (adapter_i, ==, (guint64) adapter);
  }

  if (requested_streams > 1) {
    for (guint i = 0; i < requested_streams; i++) {
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

      g_assert_true (wp_session_bin_add (WP_SESSION_BIN (endpoint),
              g_steal_pointer (&stream)));
    }
  }

  /* activate audio softdsp endpoint */
  {
    wp_session_item_activate (endpoint,
        (GAsyncReadyCallback) test_si_activate_finish_cb, f);
    g_main_loop_run (f->base.loop);
  }

  g_assert_cmphex (wp_session_item_get_flags (endpoint), ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_ACTIVE);
  g_assert_cmphex (wp_session_item_get_flags (adapter), ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_ACTIVE);
  g_assert_cmphex (wp_proxy_get_features (WP_PROXY (node)), ==,
      WP_NODE_FEATURES_STANDARD);

  /* check streams */

  g_assert_cmpuint (wp_si_endpoint_get_n_streams (WP_SI_ENDPOINT (endpoint)),
      ==, requested_streams);
  if (requested_streams == 1) {
    WpSiStream *stream =
        wp_si_endpoint_get_stream (WP_SI_ENDPOINT (endpoint), 0);
    g_autoptr (GVariant) info = wp_si_stream_get_registration_info (stream);
    const gchar *stream_name;

    g_variant_get (info, "(&sa{ss})", &stream_name, NULL);
    g_assert_cmpstr (stream_name, ==, "default");
    g_assert_true ((gpointer) stream == (gpointer) adapter);
  } else {
    for (guint i = 0; i < requested_streams; i++) {
      WpSiStream *stream =
          wp_si_endpoint_get_stream (WP_SI_ENDPOINT (endpoint), i);
      g_autoptr (GVariant) info = wp_si_stream_get_registration_info (stream);
      g_autofree gchar *expected_name = g_strdup_printf ("stream-%u", i);
      const gchar *stream_name;

      g_variant_get (info, "(&sa{ss})", &stream_name, NULL);
      g_assert_cmpstr (stream_name, ==, expected_name);
      g_assert_true ((gpointer) stream != (gpointer) adapter);
    }
  }

  /* deactivate - configuration should not be altered  */

  wp_session_item_deactivate (endpoint);

  g_assert_cmphex (wp_session_item_get_flags (endpoint), ==,
      WP_SI_FLAG_CONFIGURED);
  g_assert_cmphex (wp_session_item_get_flags (adapter), ==, 0);

  {
    g_autoptr (GVariant) v = wp_session_item_get_configuration (endpoint);
    guint64 adapter_i;
    g_assert_nonnull (v);
    g_assert_true (g_variant_lookup (v, "adapter", "t", &adapter_i));
    g_assert_cmpuint (adapter_i, ==, (guint64) adapter);
  }
}

static void
test_si_audio_softdsp_endpoint_export (TestFixture * f, gconstpointer user_data)
{
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpSession) session = NULL;
  g_autoptr (WpSessionItem) item = NULL;
  g_autoptr (WpSessionItem) adapter = NULL;
  g_autoptr (WpObjectManager) clients_om = NULL;
  g_autoptr (WpClient) self_client = NULL;

  /* find self_client, to be used for verifying endpoint.client.id */

  clients_om = wp_object_manager_new ();
  wp_object_manager_add_interest_1 (clients_om, WP_TYPE_CLIENT, NULL);
  wp_object_manager_request_proxy_features (clients_om,
      WP_TYPE_CLIENT, WP_PROXY_FEATURE_BOUND);
  g_signal_connect_swapped (clients_om, "objects-changed",
      G_CALLBACK (g_main_loop_quit), f->base.loop);
  wp_core_install_object_manager (f->base.core, clients_om);
  g_main_loop_run (f->base.loop);
  g_assert_nonnull (self_client =
      wp_object_manager_lookup (clients_om, WP_TYPE_CLIENT, NULL));

  /* create item */

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


  adapter = wp_session_item_make (f->base.core, "si-adapter");
  g_assert_nonnull (adapter);
  g_assert_true (WP_IS_SI_ENDPOINT (adapter));

  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "node",
        g_variant_new_uint64 ((guint64) node));
    g_assert_true (
        wp_session_item_configure (adapter, g_variant_builder_end (&b)));
  }

  item = wp_session_item_make (f->base.core, "si-audio-softdsp-endpoint");
  g_assert_nonnull (item);
  g_assert_true (WP_IS_SI_ENDPOINT (item));

  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "adapter",
        g_variant_new_uint64 ((guint64) adapter));
    g_assert_true (
        wp_session_item_configure (item, g_variant_builder_end (&b)));
  }

  /* activate */

  {
    wp_session_item_activate (item,
        (GAsyncReadyCallback) test_si_activate_finish_cb, f);
    g_main_loop_run (f->base.loop);
  }

  /* create session */

  session = WP_SESSION (wp_impl_session_new (f->base.core));
  g_assert_nonnull (session);

  wp_proxy_augment (WP_PROXY (session), WP_SESSION_FEATURES_STANDARD, NULL,
      (GAsyncReadyCallback) test_proxy_augment_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* export */

  wp_session_item_export (item, session,
      (GAsyncReadyCallback) test_si_export_finish_cb, f);
  g_main_loop_run (f->base.loop);

  g_assert_cmphex (wp_session_item_get_flags (item), ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_ACTIVE | WP_SI_FLAG_EXPORTED);
  g_assert_cmphex (wp_session_item_get_flags (adapter), ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_ACTIVE);

  {
    g_autoptr (WpEndpoint) ep = NULL;
    g_autoptr (WpProperties) props = NULL;
    gchar *tmp;

    g_assert_nonnull (
        ep = wp_session_item_get_associated_proxy (item, WP_TYPE_ENDPOINT));
    g_assert_nonnull (props = wp_proxy_get_properties (WP_PROXY (ep)));

    g_assert_cmpstr (wp_endpoint_get_name (ep), ==, "audiotestsrc.adapter");
    g_assert_cmpstr (wp_endpoint_get_media_class (ep), ==,
        "Audio/Source");
    g_assert_cmpint (wp_endpoint_get_direction (ep), ==, WP_DIRECTION_OUTPUT);
    g_assert_cmpstr (wp_properties_get (props, "endpoint.name"), ==,
        "audiotestsrc.adapter");
    g_assert_cmpstr (wp_properties_get (props, "media.class"), ==,
        "Audio/Source");

    tmp = g_strdup_printf ("%d", wp_proxy_get_bound_id (WP_PROXY (session)));
    g_assert_cmpstr (wp_properties_get (props, "session.id"), ==, tmp);
    g_free (tmp);

    tmp = g_strdup_printf ("%d", wp_proxy_get_bound_id (WP_PROXY (node)));
    g_assert_cmpstr (wp_properties_get (props, "node.id"), ==, tmp);
    g_free (tmp);

    tmp = g_strdup_printf ("%d", wp_proxy_get_bound_id (WP_PROXY (self_client)));
    g_assert_cmpstr (wp_properties_get (props, "endpoint.client.id"), ==, tmp);
    g_free (tmp);
  }
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  /* configure-activate */

  g_test_add ("/modules/si-audio-softdsp-endpoint/configure-activate/single-stream",
      TestFixture, GUINT_TO_POINTER (1),
      test_si_audio_softdsp_endpoint_setup,
      test_si_audio_softdsp_endpoint_configure_activate,
      test_si_audio_softdsp_endpoint_teardown);

  g_test_add ("/modules/si-audio-softdsp-endpoint/configure-activate/multi-stream",
      TestFixture, GUINT_TO_POINTER (5),
      test_si_audio_softdsp_endpoint_setup,
      test_si_audio_softdsp_endpoint_configure_activate,
      test_si_audio_softdsp_endpoint_teardown);

 /* export */

 g_test_add ("/modules/si-audio-softdsp-endpoint/export",
      TestFixture, NULL,
      test_si_audio_softdsp_endpoint_setup,
      test_si_audio_softdsp_endpoint_export,
      test_si_audio_softdsp_endpoint_teardown);

  return g_test_run ();
}
