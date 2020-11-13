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
test_si_simple_node_endpoint_setup (TestFixture * f, gconstpointer user_data)
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
  }
  {
    g_autoptr (GError) error = NULL;
    WpModule *module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-si-simple-node-endpoint", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
  }
}

static void
test_si_simple_node_endpoint_teardown (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_teardown (&f->base);
}

static void
test_si_simple_node_endpoint_configure_activate (TestFixture * f,
    gconstpointer user_data)
{
  const TestData *data = user_data;
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpSessionItem) item = NULL;
  WpSiStream *stream;

  /* create item */

  item = wp_session_item_make (f->base.core, "si-simple-node-endpoint");
  g_assert_nonnull (item);
  g_assert_true (WP_IS_SI_ENDPOINT (item));
  g_assert_true (WP_IS_SI_STREAM (item));
  g_assert_true (WP_IS_SI_PORT_INFO (item));

  node = wp_node_new_from_factory (f->base.core,
      "spa-node-factory",
      wp_properties_new (
          "factory.name", data->factory,
          "node.name", data->name,
          NULL));
  g_assert_nonnull (node);

  wp_object_activate (WP_OBJECT (node), WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* configure */

  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "node",
        g_variant_new_uint64 ((guint64) node));
    if (data->media_class) {
      g_variant_builder_add (&b, "{sv}", "media-class",
          g_variant_new_string (data->media_class));
    }
    g_assert_true (
        wp_session_item_configure (item, g_variant_builder_end (&b)));
  }

  g_assert_cmphex (wp_session_item_get_flags (item), ==,
      WP_SI_FLAG_CONFIGURED);

  {
    g_autoptr (GVariant) v = wp_session_item_get_configuration (item);
    guint64 node_i;
    const gchar *str;
    guint32 prio;
    guchar dir;
    g_assert_nonnull (v);
    g_assert_true (g_variant_lookup (v, "node", "t", &node_i));
    g_assert_cmpuint (node_i, ==, (guint64) node);
    g_assert_true (g_variant_lookup (v, "name", "&s", &str));
    g_assert_cmpstr (str, ==, data->name);
    g_assert_true (g_variant_lookup (v, "media-class", "&s", &str));
    g_assert_cmpstr (str, ==, data->expected_media_class);
    g_assert_true (g_variant_lookup (v, "role", "&s", &str));
    g_assert_cmpstr (str, ==, "");
    g_assert_true (g_variant_lookup (v, "priority", "u", &prio));
    g_assert_cmpuint (prio, ==, 0);
    g_assert_true (g_variant_lookup (v, "direction", "y", &dir));
    g_assert_cmpuint (dir, ==, data->expected_direction);
  }

  /* activate */

  wp_session_item_activate (item,
      (GAsyncReadyCallback) test_si_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  g_assert_cmphex (wp_session_item_get_flags (item), ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_ACTIVE);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (node)), ==,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL | WP_NODE_FEATURE_PORTS);

  g_assert_cmpint (
      wp_si_endpoint_get_n_streams (WP_SI_ENDPOINT (item)), ==, 1);
  g_assert_nonnull (
      stream = wp_si_endpoint_get_stream (WP_SI_ENDPOINT (item), 0));

  if (data->expected_direction == WP_DIRECTION_INPUT)
    g_assert_cmpuint (wp_node_get_n_input_ports (node, NULL), ==, 1);
  else
    g_assert_cmpuint (wp_node_get_n_output_ports (node, NULL), ==, 1);
  g_assert_cmpuint (wp_node_get_n_ports (node), ==, 1);

  {
    guint32 node_id, port_id, channel;
    g_autoptr (GVariant) v =
        wp_si_port_info_get_ports (WP_SI_PORT_INFO (stream), NULL);

    g_assert_true (g_variant_is_of_type (v, G_VARIANT_TYPE ("a(uuu)")));
    g_assert_cmpint (g_variant_n_children (v), ==, 1);
    g_variant_get_child (v, 0, "(uuu)", &node_id, &port_id, &channel);
    g_assert_cmpuint (node_id, ==, wp_proxy_get_bound_id (WP_PROXY (node)));
    g_assert_cmpuint (channel, ==, 0);

    {
      g_autoptr (WpIterator) it = wp_node_iterate_ports (node);
      g_auto (GValue) val = G_VALUE_INIT;
      WpProxy *port;

      g_assert_true (wp_iterator_next (it, &val));
      g_assert_nonnull (port = g_value_get_object (&val));
      g_assert_cmpuint (port_id, ==, wp_proxy_get_bound_id (port));
    }
  }

  /* deactivate - configuration should not be altered  */

  wp_session_item_deactivate (item);

  g_assert_cmphex (wp_session_item_get_flags (item), ==,
      WP_SI_FLAG_CONFIGURED);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (node)), ==,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL | WP_NODE_FEATURE_PORTS);

  {
    g_autoptr (GVariant) v = wp_session_item_get_configuration (item);
    guint64 node_i;
    const gchar *str;
    guint32 prio;
    guchar dir;
    g_assert_nonnull (v);
    g_assert_true (g_variant_lookup (v, "node", "t", &node_i));
    g_assert_cmpuint (node_i, ==, (guint64) node);
    g_assert_true (g_variant_lookup (v, "name", "&s", &str));
    g_assert_cmpstr (str, ==, data->name);
    g_assert_true (g_variant_lookup (v, "media-class", "&s", &str));
    g_assert_cmpstr (str, ==, data->expected_media_class);
    g_assert_true (g_variant_lookup (v, "role", "&s", &str));
    g_assert_cmpstr (str, ==, "");
    g_assert_true (g_variant_lookup (v, "priority", "u", &prio));
    g_assert_cmpuint (prio, ==, 0);
    g_assert_true (g_variant_lookup (v, "direction", "y", &dir));
    g_assert_cmpuint (dir, ==, data->expected_direction);
  }

  /* reset - configuration resets */

  wp_session_item_reset (item);
  g_assert_cmphex (wp_session_item_get_flags (item), ==, 0);

  {
    g_autoptr (GVariant) v = wp_session_item_get_configuration (item);
    guint64 node_i;
    const gchar *str;
    guint32 prio;
    guchar dir;
    g_assert_nonnull (v);
    g_assert_true (g_variant_lookup (v, "node", "t", &node_i));
    g_assert_cmpuint (node_i, ==, 0);
    g_assert_true (g_variant_lookup (v, "name", "&s", &str));
    g_assert_cmpstr (str, ==, "");
    g_assert_true (g_variant_lookup (v, "media-class", "&s", &str));
    g_assert_cmpstr (str, ==, "");
    g_assert_true (g_variant_lookup (v, "role", "&s", &str));
    g_assert_cmpstr (str, ==, "");
    g_assert_true (g_variant_lookup (v, "priority", "u", &prio));
    g_assert_cmpuint (prio, ==, 0);
    g_assert_true (g_variant_lookup (v, "direction", "y", &dir));
    g_assert_cmpuint (dir, ==, WP_DIRECTION_INPUT);
  }
}

static void
test_si_simple_node_endpoint_export (TestFixture * f, gconstpointer user_data)
{
  const TestData *data = user_data;
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpSession) session = NULL;
  g_autoptr (WpSessionItem) item = NULL;
  g_autoptr (WpObjectManager) clients_om = NULL;
  g_autoptr (WpClient) self_client = NULL;
  WpSiStream *stream;

  /* find self_client, to be used for verifying endpoint.client.id */

  clients_om = wp_object_manager_new ();
  wp_object_manager_add_interest (clients_om, WP_TYPE_CLIENT, NULL);
  wp_object_manager_request_object_features (clients_om,
      WP_TYPE_CLIENT, WP_PROXY_FEATURE_BOUND);
  g_signal_connect_swapped (clients_om, "objects-changed",
      G_CALLBACK (g_main_loop_quit), f->base.loop);
  wp_core_install_object_manager (f->base.core, clients_om);
  g_main_loop_run (f->base.loop);
  g_assert_nonnull (self_client = wp_object_manager_lookup (clients_om,
          WP_TYPE_CLIENT, NULL));

  /* create item */

  item = wp_session_item_make (f->base.core, "si-simple-node-endpoint");
  g_assert_nonnull (item);

  node = wp_node_new_from_factory (f->base.core,
      "spa-node-factory",
      wp_properties_new (
          "factory.name", data->factory,
          "node.name", data->name,
          NULL));
  g_assert_nonnull (node);

  wp_object_activate (WP_OBJECT (node), WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* configure */

  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "node",
        g_variant_new_uint64 ((guint64) node));
    if (data->media_class) {
      g_variant_builder_add (&b, "{sv}", "media-class",
          g_variant_new_string (data->media_class));
    }
    g_variant_builder_add (&b, "{sv}", "role", g_variant_new_string ("test"));
    g_variant_builder_add (&b, "{sv}", "priority", g_variant_new_uint32 (10));
    g_assert_true (
        wp_session_item_configure (item, g_variant_builder_end (&b)));
  }

  /* activate */

  wp_session_item_activate (item,
      (GAsyncReadyCallback) test_si_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  g_assert_cmpint (
      wp_si_endpoint_get_n_streams (WP_SI_ENDPOINT (item)), ==, 1);
  g_assert_nonnull (
      stream = wp_si_endpoint_get_stream (WP_SI_ENDPOINT (item), 0));

  /* create session */

  session = WP_SESSION (wp_impl_session_new (f->base.core));
  g_assert_nonnull (session);

  wp_object_activate (WP_OBJECT (session), WP_OBJECT_FEATURES_ALL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* export */

  wp_session_item_export (item, session,
      (GAsyncReadyCallback) test_si_export_finish_cb, f);
  g_main_loop_run (f->base.loop);

  g_assert_cmphex (wp_session_item_get_flags (item), ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_ACTIVE | WP_SI_FLAG_EXPORTED);

  {
    g_autoptr (WpEndpoint) ep = NULL;
    g_autoptr (WpProperties) props = NULL;
    gchar *tmp;

    g_assert_nonnull (
        ep = wp_session_item_get_associated_proxy (item, WP_TYPE_ENDPOINT));
    g_assert_nonnull (
        props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (ep)));

    g_assert_cmpstr (wp_endpoint_get_name (ep), ==, data->name);
    g_assert_cmpstr (wp_endpoint_get_media_class (ep), ==,
        data->expected_media_class);
    g_assert_cmpint (wp_endpoint_get_direction (ep), ==,
        data->expected_direction);
    g_assert_cmpstr (wp_properties_get (props, "endpoint.name"), ==,
        data->name);
    g_assert_cmpstr (wp_properties_get (props, "media.class"), ==,
        data->expected_media_class);
    g_assert_cmpstr (wp_properties_get (props, "media.role"), ==, "test");
    g_assert_cmpstr (wp_properties_get (props, "endpoint.priority"), ==, "10");

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

  {
    g_autoptr (WpEndpointStream) epstr = NULL;
    g_autoptr (WpProperties) props = NULL;
    gchar *tmp;

    g_assert_nonnull (
        epstr = wp_session_item_get_associated_proxy (WP_SESSION_ITEM (stream),
                WP_TYPE_ENDPOINT_STREAM));
    g_assert_nonnull (
        props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (epstr)));

    g_assert_cmpstr (wp_endpoint_stream_get_name (epstr), ==, "default");
    g_assert_cmpstr (wp_properties_get (props, "endpoint-stream.name"), ==,
        "default");

    tmp = g_strdup_printf ("%d", wp_session_item_get_associated_proxy_id (
            WP_SESSION_ITEM (stream), WP_TYPE_ENDPOINT));
    g_assert_cmpstr (wp_properties_get (props, "endpoint.id"), ==, tmp);
    g_free (tmp);
  }

  wp_session_item_reset (item);
  g_assert_cmphex (wp_session_item_get_flags (item), ==, 0);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  /* data */

  const TestData fakesink_data = {
    "fakesink", "fakesink0", "Fake/Sink", "Fake/Sink", WP_DIRECTION_INPUT
  };
  const TestData fakesrc_data = {
    "fakesrc", "fakesrc0", "Fake/Source", "Fake/Source", WP_DIRECTION_OUTPUT
  };
  const TestData audiotestsrc_data = {
    "audiotestsrc", "audiotestsrc0", NULL, "Audio/Source", WP_DIRECTION_OUTPUT
  };

  /* configure-activate */

  g_test_add ("/modules/si-simple-node-endpoint/configure-activate/fakesink",
      TestFixture, &fakesink_data,
      test_si_simple_node_endpoint_setup,
      test_si_simple_node_endpoint_configure_activate,
      test_si_simple_node_endpoint_teardown);

  g_test_add ("/modules/si-simple-node-endpoint/configure-activate/fakesrc",
      TestFixture, &fakesrc_data,
      test_si_simple_node_endpoint_setup,
      test_si_simple_node_endpoint_configure_activate,
      test_si_simple_node_endpoint_teardown);

  g_test_add ("/modules/si-simple-node-endpoint/configure-activate/audiotestsrc",
      TestFixture, &audiotestsrc_data,
      test_si_simple_node_endpoint_setup,
      test_si_simple_node_endpoint_configure_activate,
      test_si_simple_node_endpoint_teardown);

  /* export */

  g_test_add ("/modules/si-simple-node-endpoint/export/fakesink",
      TestFixture, &fakesink_data,
      test_si_simple_node_endpoint_setup,
      test_si_simple_node_endpoint_export,
      test_si_simple_node_endpoint_teardown);

  g_test_add ("/modules/si-simple-node-endpoint/export/fakesrc",
      TestFixture, &fakesrc_data,
      test_si_simple_node_endpoint_setup,
      test_si_simple_node_endpoint_export,
      test_si_simple_node_endpoint_teardown);

  g_test_add ("/modules/si-simple-node-endpoint/export/audiotestsrc",
      TestFixture, &audiotestsrc_data,
      test_si_simple_node_endpoint_setup,
      test_si_simple_node_endpoint_export,
      test_si_simple_node_endpoint_teardown);

  return g_test_run ();
}
