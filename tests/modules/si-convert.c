/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
} TestFixture;

static void
test_si_convert_setup (TestFixture * f, gconstpointer user_data)
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
  }
  {
    g_autoptr (GError) error = NULL;
    wp_core_load_component (f->base.core,
        "libwireplumber-module-si-adapter", "module", NULL, &error);
    g_assert_no_error (error);
  }
  {
    g_autoptr (GError) error = NULL;
    wp_core_load_component (f->base.core,
        "libwireplumber-module-si-convert", "module", NULL, &error);
    g_assert_no_error (error);
  }
}

static void
test_si_convert_teardown (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_teardown (&f->base);
}

static void
test_si_convert_configure_activate (TestFixture * f,
    gconstpointer user_data)
{
  g_autoptr (WpNode) target_node = NULL;
  g_autoptr (WpSessionItem) target = NULL;
  g_autoptr (WpSessionItem) convert = NULL;

  /* create target node */

  target_node = wp_node_new_from_factory (f->base.core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", "audiotestsrc.adapter",
          NULL));
  g_assert_nonnull (target_node);
  wp_object_activate (WP_OBJECT (target_node),
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL, NULL,
      (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* create target */

  target = wp_session_item_make (f->base.core, "si-adapter");
  g_assert_nonnull (target);
  g_assert_true (WP_IS_SI_ENDPOINT (target));

  /* configure target */

  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "node",
        g_variant_new_uint64 ((guint64) target_node));
    g_assert_true (
        wp_session_item_configure (target, g_variant_builder_end (&b)));
  }

  g_assert_cmphex (wp_session_item_get_flags (target), ==,
      WP_SI_FLAG_CONFIGURED);

  /* create convert */

  convert = wp_session_item_make (f->base.core, "si-convert");
  g_assert_nonnull (convert);
  g_assert_true (WP_IS_SI_ENDPOINT (convert));

  /* configure convert */

  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "target",
        g_variant_new_uint64 ((guint64) target));
    g_variant_builder_add (&b, "{sv}", "name",
        g_variant_new_string ("convert"));
    g_assert_true (
        wp_session_item_configure (convert, g_variant_builder_end (&b)));

    g_assert_cmphex (wp_session_item_get_flags (convert), ==,
      WP_SI_FLAG_CONFIGURED);
  }

  {
    g_autoptr (GVariant) v = wp_session_item_get_configuration (convert);
    guint64 target_i;
    const gchar *name;
    g_assert_nonnull (v);
    g_assert_true (g_variant_lookup (v, "target", "t", &target_i));
    g_assert_cmpuint (target_i, ==, (guint64) target);
    g_assert_true (g_variant_lookup (v, "name", "&s", &name));
    g_assert_cmpstr (name, ==, "convert");
  }

  /* activate convert */

  {
    wp_session_item_activate (convert,
        (GAsyncReadyCallback) test_si_activate_finish_cb, f);
    g_main_loop_run (f->base.loop);
  }

  g_assert_cmphex (wp_session_item_get_flags (convert), ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_ACTIVE);

  /* deactivate  */

  wp_session_item_deactivate (convert);
}

static void
test_si_convert_export (TestFixture * f, gconstpointer user_data)
{
  g_autoptr (WpNode) target_node = NULL;
  g_autoptr (WpSessionItem) target = NULL;
  g_autoptr (WpSessionItem) convert = NULL;
  g_autoptr (WpSession) session = NULL;
  g_autoptr (WpObjectManager) clients_om = NULL;
  g_autoptr (WpClient) self_client = NULL;

  /* find self_client, to be used for verifying endpoint.client.id */

  clients_om = wp_object_manager_new ();
  wp_object_manager_add_interest (clients_om, WP_TYPE_CLIENT, NULL);
  wp_object_manager_request_object_features (clients_om,
      WP_TYPE_CLIENT, WP_PROXY_FEATURE_BOUND);
  g_signal_connect_swapped (clients_om, "objects-changed",
      G_CALLBACK (g_main_loop_quit), f->base.loop);
  wp_core_install_object_manager (f->base.core, clients_om);
  g_main_loop_run (f->base.loop);
  g_assert_nonnull (self_client =
      wp_object_manager_lookup (clients_om, WP_TYPE_CLIENT, NULL));

  /* create target node */

  target_node = wp_node_new_from_factory (f->base.core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", "audiotestsrc.adapter",
          NULL));
  g_assert_nonnull (target_node);
  wp_object_activate (WP_OBJECT (target_node),
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL, NULL,
      (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* create target */

  target = wp_session_item_make (f->base.core, "si-adapter");
  g_assert_nonnull (target);
  g_assert_true (WP_IS_SI_ENDPOINT (target));

  /* configure target */

  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "node",
        g_variant_new_uint64 ((guint64) target_node));
    g_assert_true (
        wp_session_item_configure (target, g_variant_builder_end (&b)));
    g_assert_cmphex (wp_session_item_get_flags (target), ==,
      WP_SI_FLAG_CONFIGURED);
  }

  /* create convert */

  convert = wp_session_item_make (f->base.core, "si-convert");
  g_assert_nonnull (convert);

  /* configure convert */
  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "target",
        g_variant_new_uint64 ((guint64) target));
    g_variant_builder_add (&b, "{sv}", "name",
        g_variant_new_string ("convert"));
    g_assert_true (
        wp_session_item_configure (convert, g_variant_builder_end (&b)));
    g_assert_cmphex (wp_session_item_get_flags (convert), ==,
      WP_SI_FLAG_CONFIGURED);
  }

  /* activate convert */

  {
    wp_session_item_activate (convert,
        (GAsyncReadyCallback) test_si_activate_finish_cb, f);
    g_main_loop_run (f->base.loop);
  }

  /* create session */

  session = WP_SESSION (wp_impl_session_new (f->base.core));
  g_assert_nonnull (session);

  wp_object_activate (WP_OBJECT (session), WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* export */

  wp_session_item_export (convert, session,
      (GAsyncReadyCallback) test_si_export_finish_cb, f);
  g_main_loop_run (f->base.loop);

  g_assert_cmphex (wp_session_item_get_flags (convert), ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_ACTIVE | WP_SI_FLAG_EXPORTED);

  {
    g_autoptr (WpEndpoint) ep = NULL;
    g_autoptr (WpProperties) props = NULL;
    gchar *tmp;

    g_assert_nonnull (
        ep = wp_session_item_get_associated_proxy (convert, WP_TYPE_ENDPOINT));
    g_assert_nonnull (
        props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (ep)));

    g_assert_cmpstr (wp_endpoint_get_name (ep), ==, "convert");
    g_assert_cmpstr (wp_endpoint_get_media_class (ep), ==,
        "Audio/Convert");
    g_assert_cmpint (wp_endpoint_get_direction (ep), ==, WP_DIRECTION_OUTPUT);
    g_assert_cmpstr (wp_properties_get (props, "endpoint.name"), ==,
        "convert");
    g_assert_cmpstr (wp_properties_get (props, "media.class"), ==,
        "Audio/Convert");

    tmp = g_strdup_printf ("%d", wp_proxy_get_bound_id (WP_PROXY (session)));
    g_assert_cmpstr (wp_properties_get (props, "session.id"), ==, tmp);
    g_free (tmp);
  }
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  /* configure-activate */

  g_test_add ("/modules/si-convert/configure-activate",
      TestFixture, NULL,
      test_si_convert_setup,
      test_si_convert_configure_activate,
      test_si_convert_teardown);

 /* export */

 g_test_add ("/modules/si-convert/export",
      TestFixture, NULL,
      test_si_convert_setup,
      test_si_convert_export,
      test_si_convert_teardown);

  return g_test_run ();
}
