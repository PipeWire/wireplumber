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
load_endpoint (TestFixture * f, const gchar * factory, const gchar * media_class)
{
  g_autoptr (WpSessionItem) endpoint = NULL;

  /* create endpoint */

  endpoint = wp_session_item_make (f->base.core, "si-audio-endpoint");
  g_assert_nonnull (endpoint);
  g_assert_true (WP_IS_SI_ENDPOINT (endpoint));

  /* configure endpoint */
  {
    WpProperties *props = wp_properties_new_empty ();
    wp_properties_set (props, "name", factory);
    wp_properties_set (props, "media.class", media_class);
    g_assert_true (wp_session_item_configure (endpoint, props));
    g_assert_true (wp_session_item_is_configured (endpoint));
  }

  /* activate and export endpoint */

  wp_object_activate (WP_OBJECT (endpoint),
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED,
      NULL,  (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (endpoint)), ==,
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED);

  return g_steal_pointer (&endpoint);
}

static void
test_si_standard_link_setup (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&f->base, WP_BASE_TEST_FLAG_CLIENT_CORE);

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
            "libpipewire-module-link-factory", NULL, NULL));
  }
  {
    g_autoptr (GError) error = NULL;
    wp_core_load_component (f->base.core,
        "libwireplumber-module-si-audio-endpoint", "module", NULL, &error);
    g_assert_no_error (error);

    wp_core_load_component (f->base.core,
        "libwireplumber-module-si-standard-link", "module", NULL, &error);
    g_assert_no_error (error);
  }

  f->src_item = load_endpoint (f, "audiotestsrc", "Audio/Source");
  f->sink_item = load_endpoint (f, "fakesink", "Audio/Sink");
}

static void
on_core_sync_done (WpCore *core, GAsyncResult *res, TestFixture * f)
{
  g_autoptr (GError) error = NULL;
  gboolean ret = wp_core_sync_finish (core, res, &error);
  g_assert_no_error (error);
  g_assert_true (ret);
  g_main_loop_quit (f->base.loop);
}

static void
test_si_standard_link_teardown (TestFixture * f, gconstpointer user_data)
{
  wp_core_sync (f->base.core, NULL, (GAsyncReadyCallback) on_core_sync_done, f);
  g_main_loop_run (f->base.loop);
  g_clear_object (&f->sink_item);
  g_clear_object (&f->src_item);
  wp_base_test_fixture_teardown (&f->base);
}

static void
on_object_activated (WpObject *o, GAsyncResult *res, TestFixture * f)
{
  g_autoptr (GError) error = NULL;
  g_assert_true (wp_object_activate_finish (o, res, &error));
  g_assert_null (error);
  g_main_loop_quit (f->base.loop);
}

static void
test_si_standard_link_main (TestFixture * f, gconstpointer user_data)
{
  g_autoptr (WpSessionItem) link = NULL;

  /* create the link */
  link = wp_session_item_make (f->base.core, "si-standard-link");
  g_assert_nonnull (link);

  /* configure the link */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    wp_properties_setf (props, "out.item", "%p", f->src_item);
    wp_properties_setf (props, "in.item", "%p", f->sink_item);
    g_assert_true (wp_session_item_configure (link, g_steal_pointer (&props)));
  }

  /* activate */
  wp_object_activate (WP_OBJECT (link), WP_SESSION_ITEM_FEATURE_ACTIVE,
      NULL, (GAsyncReadyCallback) on_object_activated, f);
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
    test_ensure_object_manager_is_installed (om, f->base.client_core,
        f->base.loop);

    g_assert_nonnull (out_node = wp_object_manager_lookup (om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", "control.audiotestsrc",
        NULL));
    g_assert_nonnull (in_node = wp_object_manager_lookup (om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", "control.fakesink",
        NULL));
    g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 12);

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
      g_assert_nonnull (out_port = wp_object_manager_lookup (om, WP_TYPE_PORT,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", out_pt_id,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.id", "=u", out_nd_id,
          NULL));
      g_assert_nonnull (in_port = wp_object_manager_lookup (om, WP_TYPE_PORT,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", in_pt_id,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.id", "=u", in_nd_id,
          NULL));
      total_links++;
    }
    g_assert_cmpuint (total_links, ==, 2);
  }

  /* deactivate */
  wp_object_deactivate (WP_OBJECT (link), WP_SESSION_ITEM_FEATURE_ACTIVE);
  wp_core_sync (f->base.core, NULL, (GAsyncReadyCallback) on_core_sync_done, f);
  g_main_loop_run (f->base.loop);

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
    test_ensure_object_manager_is_installed (om, f->base.client_core,
        f->base.loop);

    g_assert_nonnull (out_node = wp_object_manager_lookup (om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", "control.audiotestsrc",
        NULL));
    g_assert_nonnull (in_node = wp_object_manager_lookup (om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", "control.fakesink",
        NULL));
    g_assert_nonnull (out_port = wp_object_manager_lookup (om, WP_TYPE_PORT,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.direction", "=s", "out",
        NULL));
    g_assert_nonnull (in_port = wp_object_manager_lookup (om, WP_TYPE_PORT,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.direction", "=s", "in",
        NULL));
    g_assert_null (link = wp_object_manager_lookup (om, WP_TYPE_LINK, NULL));
    g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 10);
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
