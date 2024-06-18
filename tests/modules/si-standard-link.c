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

  WpSessionItem *src_item;
  WpSessionItem *sink_item;

} TestFixture;

static WpSessionItem *
load_node (TestFixture * f, const gchar * factory, const gchar * media_class,
    const gchar * type)
{
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpSessionItem) adapter = NULL;

  /* create audiotestsrc adapter node */
  node = wp_node_new_from_factory (f->base.core,
      "adapter",
      wp_properties_new (
          "factory.name", factory,
          "node.name", factory,
          "media.class", media_class,
          "audio.channels", "2",
          "audio.position", "[ FL, FR ]",
          NULL));
  g_assert_nonnull (node);
  wp_object_activate (WP_OBJECT (node), WP_OBJECT_FEATURES_ALL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* create adapter */
  adapter = wp_session_item_make (f->base.core, "si-audio-adapter");
  g_assert_nonnull (adapter);
  g_assert_true (WP_IS_SI_LINKABLE (adapter));

  /* configure */
  {
    WpProperties *props = wp_properties_new_empty ();
    wp_properties_setf (props, "item.node", "%p", node);
    wp_properties_set (props, "media.class", media_class);
    wp_properties_set (props, "item.node.type", type);
    g_assert_true (wp_session_item_configure (adapter, props));
    g_assert_true (wp_session_item_is_configured (adapter));
  }

  /* activate adapter */

  wp_object_activate (WP_OBJECT (adapter),
      WP_SESSION_ITEM_FEATURE_ACTIVE,
      NULL,  (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (adapter)), ==,
      WP_SESSION_ITEM_FEATURE_ACTIVE);

  return g_steal_pointer (&adapter);
}

static void
on_plugin_loaded (WpCore * core, GAsyncResult * res, TestFixture *f)
{
  gboolean loaded;
  GError *error = NULL;

  loaded = wp_core_load_component_finish (core, res, &error);
  g_assert_no_error (error);
  g_assert_true (loaded);

  g_main_loop_quit (f->base.loop);
}

static void
test_si_standard_link_setup (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&f->base, 0);

  /* load modules */
  {
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&f->base.server);

    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), ==, 0);
    if (!test_is_spa_lib_installed (&f->base, "audiotestsrc")) {
      g_test_skip ("The pipewire audiotestsrc factory was not found");
      return;
    }
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-adapter", NULL, NULL));
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-link-factory", NULL, NULL));
  }
  {
    wp_core_load_component (f->base.core,
        "libwireplumber-module-si-audio-adapter", "module", NULL, NULL, NULL,
        (GAsyncReadyCallback) on_plugin_loaded, f);
    g_main_loop_run (f->base.loop);

    wp_core_load_component (f->base.core,
        "libwireplumber-module-si-standard-link", "module", NULL, NULL, NULL,
        (GAsyncReadyCallback) on_plugin_loaded, f);
    g_main_loop_run (f->base.loop);
  }

  if (test_is_spa_lib_installed (&f->base, "audiotestsrc"))
    f->src_item = load_node (f, "audiotestsrc", "Stream/Output/Audio", "stream");
  if (test_is_spa_lib_installed (&f->base, "support.null-audio-sink"))
    f->sink_item = load_node (f, "support.null-audio-sink", "Audio/Sink", "device");
}

static void
test_si_standard_link_teardown (TestFixture * f, gconstpointer user_data)
{
  g_clear_object (&f->sink_item);
  g_clear_object (&f->src_item);
  wp_base_test_fixture_teardown (&f->base);
}

static void
test_si_standard_link_main (TestFixture * f, gconstpointer user_data)
{
  g_autoptr (WpSessionItem) link = NULL;

  /* skip the test if audiotestsrc could not be loaded */
  if (!f->src_item) {
    g_test_skip ("The pipewire audiotestsrc factory was not found");
    return;
  }

  /* skip the test if null-audio-sink could not be loaded */
  if (!f->sink_item) {
    g_test_skip ("The pipewire null-audio-sink factory was not found");
    return;
  }

  /* create the link */
  link = wp_session_item_make (f->base.core, "si-standard-link");
  g_assert_nonnull (link);

  /* configure the link */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    wp_properties_setf (props, "out.item", "%p", f->src_item);
    wp_properties_setf (props, "in.item", "%p", f->sink_item);
    wp_properties_set (props, "out.item.port.context", "output");
    wp_properties_set (props, "in.item.port.context", "input");
    g_assert_true (wp_session_item_configure (link, g_steal_pointer (&props)));
  }

  /* activate */
  wp_object_activate (WP_OBJECT (link), WP_SESSION_ITEM_FEATURE_ACTIVE,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* verify the graph state */
  {
    g_autoptr (WpNode) out_node = NULL;
    g_autoptr (WpNode) in_node = NULL;
    g_autoptr (WpIterator) it = NULL;
    g_auto (GValue) val = G_VALUE_INIT;
    g_autoptr (WpObjectManager) om = wp_object_manager_new ();
    guint total_links = 0;

    wp_object_manager_add_interest (om, WP_TYPE_NODE, NULL);
    wp_object_manager_add_interest (om, WP_TYPE_PORT, NULL);
    wp_object_manager_add_interest (om, WP_TYPE_LINK, NULL);
    wp_object_manager_request_object_features (om, WP_TYPE_PROXY,
        WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
    test_ensure_object_manager_is_installed (om, f->base.core,
        f->base.loop);

    out_node = wp_object_manager_lookup (om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", "audiotestsrc",
        NULL);
    g_assert_nonnull (out_node);
    in_node = wp_object_manager_lookup (om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", "support.null-audio-sink",
        NULL);
    g_assert_nonnull (in_node);
    g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 8);

    it = wp_object_manager_new_filtered_iterator (om, WP_TYPE_LINK, NULL);
    for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      guint32 out_nd_id, out_pt_id, in_nd_id, in_pt_id;
      g_autoptr (WpPort) out_port = NULL;
      g_autoptr (WpPort) in_port = NULL;
      WpLink *link = g_value_get_object (&val);
      wp_link_get_linked_object_ids (link, &out_nd_id, &out_pt_id, &in_nd_id,
          &in_pt_id);
      g_assert_cmpuint (out_nd_id, ==, wp_proxy_get_bound_id (WP_PROXY (out_node)));
      g_assert_cmpuint (in_nd_id, ==, wp_proxy_get_bound_id (WP_PROXY (in_node)));
      out_port = wp_object_manager_lookup (om, WP_TYPE_PORT,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", out_pt_id,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.id", "=u", out_nd_id,
          NULL);
      g_assert_nonnull (out_port);
      in_port = wp_object_manager_lookup (om, WP_TYPE_PORT,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", in_pt_id,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.id", "=u", in_nd_id,
          NULL);
      g_assert_nonnull (in_port);
      total_links++;
    }
    g_assert_cmpuint (total_links, ==, 2);
  }

  /* deactivate */
  wp_object_deactivate (WP_OBJECT (link), WP_SESSION_ITEM_FEATURE_ACTIVE);

  /* verify the graph state */
  {
    g_autoptr (WpNode) out_node = NULL;
    g_autoptr (WpNode) in_node = NULL;
    g_autoptr (WpPort) out_port = NULL;
    g_autoptr (WpPort) in_port = NULL;
    g_autoptr (WpLink) link = NULL;
    g_autoptr (WpObjectManager) om = wp_object_manager_new ();

    wp_object_manager_add_interest (om, WP_TYPE_NODE, NULL);
    wp_object_manager_add_interest (om, WP_TYPE_PORT, NULL);
    wp_object_manager_add_interest (om, WP_TYPE_LINK, NULL);
    wp_object_manager_request_object_features (om, WP_TYPE_PROXY,
        WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
    test_ensure_object_manager_is_installed (om, f->base.core,
        f->base.loop);

    out_node = wp_object_manager_lookup (om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", "audiotestsrc",
        NULL);
    g_assert_nonnull (out_node);
    in_node = wp_object_manager_lookup (om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", "support.null-audio-sink",
        NULL);
    g_assert_nonnull (in_node);
    out_port = wp_object_manager_lookup (om, WP_TYPE_PORT,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.direction", "=s", "out",
        NULL);
    g_assert_nonnull (out_port);
    in_port = wp_object_manager_lookup (om, WP_TYPE_PORT,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.direction", "=s", "in",
        NULL);
    g_assert_nonnull (in_port);
    link = wp_object_manager_lookup (om, WP_TYPE_LINK, NULL);
    g_assert_null (link);
    g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 6);
  }
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/modules/si-standard-link/main",
      TestFixture, NULL,
      test_si_standard_link_setup,
      test_si_standard_link_main,
      test_si_standard_link_teardown);

  return g_test_run ();
}
