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

  WpSession *session;
  WpSessionItem *src_item;
  WpSessionItem *sink_item;
  gint activation_state;

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
    WpModule *module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-si-simple-node-endpoint", NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (module);
    module = wp_module_load (f->base.core, "C",
        "libwireplumber-module-si-standard-link", NULL, &error);
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

  f->src_item = load_item (f, "audiotestsrc", "Audio/Source");
  f->sink_item = load_item (f, "fakesink", "Audio/Sink");
}

static void
test_si_standard_link_teardown (TestFixture * f, gconstpointer user_data)
{
  g_clear_object (&f->sink_item);
  g_clear_object (&f->src_item);
  g_clear_object (&f->session);
  wp_base_test_fixture_teardown (&f->base);
}

static void
on_link_state_changed (WpEndpointLink * link, WpEndpointLinkState old,
    WpEndpointLinkState new, const gchar * error, TestFixture * f)
{
  g_assert_null (error);
  switch (f->activation_state++) {
    case 0:
      g_assert_cmpuint (old, ==, WP_ENDPOINT_LINK_STATE_INACTIVE);
      g_assert_cmpuint (new, ==, WP_ENDPOINT_LINK_STATE_PREPARING);
      break;
    case 1:
      g_assert_cmpuint (old, ==, WP_ENDPOINT_LINK_STATE_PREPARING);
      g_assert_cmpuint (new, ==, WP_ENDPOINT_LINK_STATE_ACTIVE);
      g_main_loop_quit (f->base.loop);
      break;
    case 2:
      g_assert_cmpuint (old, ==, WP_ENDPOINT_LINK_STATE_ACTIVE);
      g_assert_cmpuint (new, ==, WP_ENDPOINT_LINK_STATE_INACTIVE);
      g_main_loop_quit (f->base.loop);
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
test_si_standard_link_main (TestFixture * f, gconstpointer user_data)
{
  g_autoptr (WpSession) session_proxy = NULL;
  g_autoptr (WpEndpoint) src_ep = NULL;
  g_autoptr (WpEndpoint) sink_ep = NULL;
  g_autoptr (WpEndpointLink) ep_link = NULL;

  /* find the "audio" session from the client */
  {
    g_autoptr (WpObjectManager) om = wp_object_manager_new ();
    wp_object_manager_add_interest (om, WP_TYPE_SESSION, NULL);
    wp_object_manager_request_proxy_features (om, WP_TYPE_SESSION,
        WP_SESSION_FEATURES_STANDARD);
    test_ensure_object_manager_is_installed (om, f->base.client_core,
        f->base.loop);

    g_assert_nonnull (session_proxy =
        wp_object_manager_lookup (om, WP_TYPE_SESSION,
            WP_CONSTRAINT_TYPE_PW_PROPERTY, "session.name", "=s", "audio", NULL));
    g_assert_cmpint (wp_proxy_get_bound_id (WP_PROXY (session_proxy)), ==,
        wp_proxy_get_bound_id (WP_PROXY (f->session)));
  }

  /* find the endpoints */

  g_assert_nonnull (src_ep =  wp_session_lookup_endpoint (session_proxy,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, "endpoint.name", "=s", "audiotestsrc",
          NULL));
  g_assert_nonnull (sink_ep =  wp_session_lookup_endpoint (session_proxy,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, "endpoint.name", "=s", "fakesink",
          NULL));
  g_assert_cmpuint (wp_endpoint_get_n_streams (src_ep), ==, 1);
  g_assert_cmpuint (wp_endpoint_get_n_streams (sink_ep), ==, 1);

  /* create the link */
  {
    g_autoptr (WpProperties) props = NULL;
    g_autofree gchar * id =
        g_strdup_printf ("%u", wp_proxy_get_bound_id (WP_PROXY (sink_ep)));

    /* only the peer endpoint id is required,
       everything else will be discovered */
    props = wp_properties_new ("endpoint-link.input.endpoint", id, NULL);
    wp_endpoint_create_link (src_ep, props);
  }

  g_signal_connect_swapped (session_proxy, "links-changed",
      G_CALLBACK (g_main_loop_quit), f->base.loop);
  g_main_loop_run (f->base.loop);

  /* verify */

  g_assert_cmpuint (wp_session_get_n_links (session_proxy), ==, 1);
  g_assert_nonnull (ep_link = wp_session_lookup_link (session_proxy, NULL));

  {
    guint32 out_ep, out_stream, in_ep, in_stream;
    g_autoptr (WpEndpointStream) src_stream = NULL;
    g_autoptr (WpEndpointStream) sink_stream = NULL;

    g_assert_nonnull (src_stream = wp_endpoint_lookup_stream (src_ep, NULL));
    g_assert_nonnull (sink_stream = wp_endpoint_lookup_stream (sink_ep, NULL));

    wp_endpoint_link_get_linked_object_ids (ep_link, &out_ep, &out_stream,
        &in_ep, &in_stream);
    g_assert_cmpuint (out_ep, ==, wp_proxy_get_bound_id (WP_PROXY (src_ep)));
    g_assert_cmpuint (in_ep, ==, wp_proxy_get_bound_id (WP_PROXY (sink_ep)));
    g_assert_cmpuint (out_stream, ==, wp_proxy_get_bound_id (WP_PROXY (src_stream)));
    g_assert_cmpuint (in_stream, ==, wp_proxy_get_bound_id (WP_PROXY (sink_stream)));
  }

  {
    g_autoptr (WpProperties) p = wp_proxy_get_properties (WP_PROXY (ep_link));

    g_assert_nonnull (p);
    g_assert_nonnull (wp_properties_get (p, "endpoint-link.input.endpoint"));
    g_assert_nonnull (wp_properties_get (p, "endpoint-link.input.stream"));
    g_assert_nonnull (wp_properties_get (p, "endpoint-link.output.endpoint"));
    g_assert_nonnull (wp_properties_get (p, "endpoint-link.output.stream"));
  }

  {
    const gchar *error = NULL;
    g_assert_cmpuint (wp_endpoint_link_get_state (ep_link, &error), ==,
        WP_ENDPOINT_LINK_STATE_INACTIVE);
    g_assert_null (error);
  }

  /* activate */

  g_signal_connect (ep_link, "state-changed",
      G_CALLBACK (on_link_state_changed), f);
  wp_endpoint_link_request_state (ep_link, WP_ENDPOINT_LINK_STATE_ACTIVE);
  g_main_loop_run (f->base.loop);

  {
    const gchar *error = NULL;
    g_assert_cmpuint (wp_endpoint_link_get_state (ep_link, &error), ==,
        WP_ENDPOINT_LINK_STATE_ACTIVE);
    g_assert_null (error);
    g_assert_cmpint (f->activation_state, ==, 2);
  }

  /* verify the graph state */
  {
    guint32 out_nd_id, out_pt_id, in_nd_id, in_pt_id;
    g_autoptr (WpNode) out_node = NULL;
    g_autoptr (WpNode) in_node = NULL;
    g_autoptr (WpPort) out_port = NULL;
    g_autoptr (WpPort) in_port = NULL;
    g_autoptr (WpLink) link = NULL;
    g_autoptr (WpObjectManager) om = wp_object_manager_new ();

    wp_object_manager_add_interest (om, WP_TYPE_NODE, NULL);
    wp_object_manager_add_interest (om, WP_TYPE_PORT, NULL);
    wp_object_manager_add_interest (om, WP_TYPE_LINK, NULL);
    wp_object_manager_request_proxy_features (om, WP_TYPE_PROXY,
        WP_PROXY_FEATURES_STANDARD);
    test_ensure_object_manager_is_installed (om, f->base.client_core,
        f->base.loop);

    g_assert_nonnull (out_node = wp_object_manager_lookup (om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", "audiotestsrc",
        NULL));
    g_assert_nonnull (in_node = wp_object_manager_lookup (om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", "fakesink",
        NULL));
    g_assert_nonnull (out_port = wp_object_manager_lookup (om, WP_TYPE_PORT,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.direction", "=s", "out",
        NULL));
    g_assert_nonnull (in_port = wp_object_manager_lookup (om, WP_TYPE_PORT,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.direction", "=s", "in",
        NULL));
    g_assert_nonnull (link = wp_object_manager_lookup (om, WP_TYPE_LINK, NULL));
    g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 5);

    wp_link_get_linked_object_ids (link, &out_nd_id, &out_pt_id,
        &in_nd_id, &in_pt_id);
    g_assert_cmpuint (out_nd_id, ==, wp_proxy_get_bound_id (WP_PROXY (out_node)));
    g_assert_cmpuint (in_nd_id, ==, wp_proxy_get_bound_id (WP_PROXY (in_node)));
    g_assert_cmpuint (out_pt_id, ==, wp_proxy_get_bound_id (WP_PROXY (out_port)));
    g_assert_cmpuint (in_pt_id, ==, wp_proxy_get_bound_id (WP_PROXY (in_port)));
  }

  /* deactivate */

  wp_endpoint_link_request_state (ep_link, WP_ENDPOINT_LINK_STATE_INACTIVE);
  g_main_loop_run (f->base.loop);

  {
    const gchar *error = NULL;
    g_assert_cmpuint (wp_endpoint_link_get_state (ep_link, &error), ==,
        WP_ENDPOINT_LINK_STATE_INACTIVE);
    g_assert_null (error);
    g_assert_cmpint (f->activation_state, ==, 3);
  }

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
    wp_object_manager_request_proxy_features (om, WP_TYPE_PROXY,
        WP_PROXY_FEATURES_STANDARD);
    test_ensure_object_manager_is_installed (om, f->base.client_core,
        f->base.loop);

    g_assert_nonnull (out_node = wp_object_manager_lookup (om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", "audiotestsrc",
        NULL));
    g_assert_nonnull (in_node = wp_object_manager_lookup (om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", "fakesink",
        NULL));
    g_assert_nonnull (out_port = wp_object_manager_lookup (om, WP_TYPE_PORT,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.direction", "=s", "out",
        NULL));
    g_assert_nonnull (in_port = wp_object_manager_lookup (om, WP_TYPE_PORT,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.direction", "=s", "in",
        NULL));
    g_assert_null (link = wp_object_manager_lookup (om, WP_TYPE_LINK, NULL));
    g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 4);
  }
}

static void
on_link_destroyed (WpEndpointLink * link, TestFixture * f)
{
  f->activation_state = 10;
}

static void
test_si_standard_link_destroy (TestFixture * f, gconstpointer user_data)
{
  g_autoptr (WpSession) session_proxy = NULL;
  g_autoptr (WpEndpoint) src_ep = NULL;
  g_autoptr (WpEndpoint) sink_ep = NULL;
  g_autoptr (WpEndpointLink) ep_link = NULL;

  /* find the "audio" session from the client */
  {
    g_autoptr (WpObjectManager) om = wp_object_manager_new ();
    wp_object_manager_add_interest (om, WP_TYPE_SESSION, NULL);
    wp_object_manager_request_proxy_features (om, WP_TYPE_SESSION,
        WP_SESSION_FEATURES_STANDARD);
    test_ensure_object_manager_is_installed (om, f->base.client_core,
        f->base.loop);

    g_assert_nonnull (session_proxy =
        wp_object_manager_lookup (om, WP_TYPE_SESSION,
            WP_CONSTRAINT_TYPE_PW_PROPERTY, "session.name", "=s", "audio", NULL));
    g_assert_cmpint (wp_proxy_get_bound_id (WP_PROXY (session_proxy)), ==,
        wp_proxy_get_bound_id (WP_PROXY (f->session)));
  }

  /* find the endpoints */

  g_assert_nonnull (src_ep =  wp_session_lookup_endpoint (session_proxy,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, "endpoint.name", "=s", "audiotestsrc",
          NULL));
  g_assert_nonnull (sink_ep =  wp_session_lookup_endpoint (session_proxy,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, "endpoint.name", "=s", "fakesink",
          NULL));
  g_assert_cmpuint (wp_endpoint_get_n_streams (src_ep), ==, 1);
  g_assert_cmpuint (wp_endpoint_get_n_streams (sink_ep), ==, 1);

  /* create the link */
  {
    g_autoptr (WpProperties) props = NULL;
    g_autofree gchar * id =
        g_strdup_printf ("%u", wp_proxy_get_bound_id (WP_PROXY (sink_ep)));

    /* only the peer endpoint id is required,
       everything else will be discovered */
    props = wp_properties_new ("endpoint-link.input.endpoint", id, NULL);
    wp_endpoint_create_link (src_ep, props);
  }

  g_signal_connect_swapped (session_proxy, "links-changed",
      G_CALLBACK (g_main_loop_quit), f->base.loop);
  g_main_loop_run (f->base.loop);

  /* verify */

  g_assert_cmpuint (wp_session_get_n_links (session_proxy), ==, 1);
  g_assert_nonnull (ep_link = wp_session_lookup_link (session_proxy, NULL));
  g_assert_cmpuint (wp_endpoint_link_get_state (ep_link, NULL), ==,
      WP_ENDPOINT_LINK_STATE_INACTIVE);

  /* activate */

  g_signal_connect (ep_link, "state-changed",
      G_CALLBACK (on_link_state_changed), f);
  wp_endpoint_link_request_state (ep_link, WP_ENDPOINT_LINK_STATE_ACTIVE);
  g_main_loop_run (f->base.loop);
  g_assert_cmpuint (wp_endpoint_link_get_state (ep_link, NULL), ==,
      WP_ENDPOINT_LINK_STATE_ACTIVE);

  /* destroy */

  g_signal_connect (ep_link, "pw-proxy-destroyed",
      G_CALLBACK (on_link_destroyed), f);
  wp_proxy_request_destroy (WP_PROXY (ep_link));

  /* loop will quit because the "links-changed" signal from the session
     is still connected to quit() from earlier */
  g_main_loop_run (f->base.loop);

  g_assert_cmpint (f->activation_state, ==, 10);
  g_assert_cmpuint (wp_session_get_n_links (session_proxy), ==, 0);
  g_assert_cmpuint (wp_proxy_get_bound_id (WP_PROXY (ep_link)), ==, (guint) -1);

  /* verify the link was also destroyed on the session manager core */
  {
    g_autoptr (WpObjectManager) om = wp_object_manager_new ();

    wp_object_manager_add_interest (om, WP_TYPE_ENDPOINT_LINK, NULL);
    test_ensure_object_manager_is_installed (om, f->base.core, f->base.loop);

    g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 0);
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

  g_test_add ("/modules/si-standard-link/destroy",
      TestFixture, NULL,
      test_si_standard_link_setup,
      test_si_standard_link_destroy,
      test_si_standard_link_teardown);

  return g_test_run ();
}
