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
test_si_node_setup (TestFixture * f, gconstpointer user_data)
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
    wp_core_load_component (f->base.core,
        "libwireplumber-module-si-node", "module", NULL, &error);
    g_assert_no_error (error);
  }
}

static void
test_si_node_teardown (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_teardown (&f->base);
}

static void
test_si_node_configure_activate (TestFixture * f, gconstpointer user_data)
{
  const TestData *data = user_data;
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpSessionItem) item = NULL;

  /* skip test if spa factory is not insalled */
  if (!test_is_spa_lib_installed (&f->base, data->factory)) {
    g_autofree gchar *msg = NULL;
    msg = g_strdup_printf ("The pipewire %s factory was not found",
        data->factory);
    g_test_skip (msg);
    return;
  }

  /* create item */

  item = wp_session_item_make (f->base.core, "si-node");
  g_assert_nonnull (item);
  g_assert_true (WP_IS_SI_LINKABLE (item));

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
    WpProperties *props = wp_properties_new_empty ();
    wp_properties_setf (props, "item.node", "%p", node);
    wp_properties_set (props, "media.class", data->media_class);
    g_assert_true (wp_session_item_configure (item, props));
    g_assert_true (wp_session_item_is_configured (item));
  }

  {
    const gchar *str = NULL;
    g_autoptr (WpProperties) props = wp_session_item_get_properties (item);
    g_assert_nonnull (props);
    str = wp_properties_get (props, "media.class");
    g_assert_nonnull (str);
    g_assert_cmpstr (data->expected_media_class, ==, str);
    str = wp_properties_get (props, "item.factory.name");
    g_assert_nonnull (str);
    g_assert_cmpstr ("si-node", ==, str);
  }

  /* activate */

  wp_object_activate (WP_OBJECT (item), WP_SESSION_ITEM_FEATURE_ACTIVE,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (item)), ==,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (node)), ==,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL | WP_NODE_FEATURE_PORTS);

  if (data->expected_direction == WP_DIRECTION_INPUT)
    g_assert_cmpuint (wp_node_get_n_input_ports (node, NULL), ==, 1);
  else
    g_assert_cmpuint (wp_node_get_n_output_ports (node, NULL), ==, 1);
  g_assert_cmpuint (wp_node_get_n_ports (node), ==, 1);

  {
    guint32 node_id, port_id, channel;
    g_autoptr (GVariant) v =
        wp_si_linkable_get_ports (WP_SI_LINKABLE (item),
        (data->expected_direction == WP_DIRECTION_INPUT) ? "input" : "output");

    g_assert_true (g_variant_is_of_type (v, G_VARIANT_TYPE ("a(uuu)")));
    g_assert_cmpint (g_variant_n_children (v), ==, 1);
    g_variant_get_child (v, 0, "(uuu)", &node_id, &port_id, &channel);
    g_assert_cmpuint (node_id, ==, wp_proxy_get_bound_id (WP_PROXY (node)));
    g_assert_cmpuint (channel, ==, 0);

    {
      g_autoptr (WpIterator) it = wp_node_new_ports_iterator (node);
      g_auto (GValue) val = G_VALUE_INIT;
      WpProxy *port;

      g_assert_true (wp_iterator_next (it, &val));
      port = g_value_get_object (&val);
      g_assert_nonnull (port);
      g_assert_cmpuint (port_id, ==, wp_proxy_get_bound_id (port));
    }
  }

  /* deactivate - configuration should not be altered  */

  wp_object_deactivate (WP_OBJECT (item), WP_SESSION_ITEM_FEATURE_ACTIVE);

  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (item)), ==, 0);
  g_assert_true (wp_session_item_is_configured (item));
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (node)), ==,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL | WP_NODE_FEATURE_PORTS);

  {
    const gchar *str = NULL;
    g_autoptr (WpProperties) props = wp_session_item_get_properties (item);
    g_assert_nonnull (props);
    str = wp_properties_get (props, "media.class");
    g_assert_nonnull (str);
    g_assert_cmpstr (data->expected_media_class, ==, str);
    str = wp_properties_get (props, "item.factory.name");
    g_assert_nonnull (str);
    g_assert_cmpstr ("si-node", ==, str);
  }

  /* reset - configuration resets */

  wp_session_item_reset (item);
  g_assert_false (wp_session_item_is_configured (item));

  {
    g_autoptr (WpProperties) props =
        wp_session_item_get_properties (item);
    g_assert_null (props);
  }
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
    "audiotestsrc", "audiotestsrc0", "Audio/Source", "Audio/Source", WP_DIRECTION_OUTPUT
  };

  /* configure-activate */

  g_test_add ("/modules/si-node/configure-activate/fakesink",
      TestFixture, &fakesink_data,
      test_si_node_setup,
      test_si_node_configure_activate,
      test_si_node_teardown);

  g_test_add ("/modules/si-node/configure-activate/fakesrc",
      TestFixture, &fakesrc_data,
      test_si_node_setup,
      test_si_node_configure_activate,
      test_si_node_teardown);

  g_test_add ("/modules/si-node/configure-activate/audiotestsrc",
      TestFixture, &audiotestsrc_data,
      test_si_node_setup,
      test_si_node_configure_activate,
      test_si_node_teardown);

  return g_test_run ();
}
