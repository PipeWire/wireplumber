/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"
#include <pipewire/extensions/session-manager/keys.h>

struct _TestSiEndpoint
{
  WpSessionItem parent;
  const gchar *name;
  const gchar *media_class;
  WpDirection direction;
};

G_DECLARE_FINAL_TYPE (TestSiEndpoint, test_si_endpoint,
                      TEST, SI_ENDPOINT, WpSessionItem)

static GVariant *
test_si_endpoint_get_registration_info (WpSiEndpoint * item)
{
  TestSiEndpoint *self = TEST_SI_ENDPOINT (item);
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(ssya{ss})"));
  g_variant_builder_add (&b, "s", self->name);
  g_variant_builder_add (&b, "s", self->media_class);
  g_variant_builder_add (&b, "y", (guchar) self->direction);
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpProperties *
test_si_endpoint_get_properties (WpSiEndpoint * item)
{
  return wp_properties_new ("test.property", "test-value", NULL);
}

static guint
test_si_endpoint_get_n_streams (WpSiEndpoint * item)
{
  return 1;
}

static WpSiStream *
test_si_endpoint_get_stream (WpSiEndpoint * item, guint index)
{
  g_return_val_if_fail (index == 0, NULL);
  return WP_SI_STREAM (item);
}

static void
test_si_endpoint_endpoint_init (WpSiEndpointInterface * iface)
{
  iface->get_registration_info = test_si_endpoint_get_registration_info;
  iface->get_properties = test_si_endpoint_get_properties;
  iface->get_n_streams = test_si_endpoint_get_n_streams;
  iface->get_stream = test_si_endpoint_get_stream;
}

static GVariant *
test_si_endpoint_get_stream_registration_info (WpSiStream * self)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(sa{ss})"));
  g_variant_builder_add (&b, "s", "default");
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpProperties *
test_si_endpoint_get_stream_properties (WpSiStream * self)
{
  return wp_properties_new ("stream.property", "test-value-2", NULL);
}

static WpSiEndpoint *
test_si_endpoint_get_stream_parent_endpoint (WpSiStream * self)
{
  return WP_SI_ENDPOINT (g_object_ref (self));
}

static void
test_si_endpoint_stream_init (WpSiStreamInterface * iface)
{
  iface->get_registration_info = test_si_endpoint_get_stream_registration_info;
  iface->get_properties = test_si_endpoint_get_stream_properties;
  iface->get_parent_endpoint = test_si_endpoint_get_stream_parent_endpoint;
}

G_DEFINE_TYPE_WITH_CODE (TestSiEndpoint, test_si_endpoint, WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ENDPOINT, test_si_endpoint_endpoint_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_STREAM, test_si_endpoint_stream_init))

static void
test_si_endpoint_init (TestSiEndpoint * self)
{
}

static void
test_si_endpoint_class_init (TestSiEndpointClass * klass)
{
}

/*******************/

typedef struct {
  WpBaseTestFixture base;

  WpObjectManager *export_om;
  WpObjectManager *proxy_om;

  WpProxy *impl_endpoint;
  WpProxy *proxy_endpoint;

  gint n_events;

} TestEndpointFixture;

static void
test_endpoint_setup (TestEndpointFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_CLIENT_CORE);
  self->export_om = wp_object_manager_new ();
  self->proxy_om = wp_object_manager_new ();
}

static void
test_endpoint_teardown (TestEndpointFixture *self, gconstpointer user_data)
{
  g_clear_object (&self->proxy_om);
  g_clear_object (&self->export_om);
  wp_base_test_fixture_teardown (&self->base);
}

static void
test_endpoint_basic_impl_object_added (WpObjectManager *om,
    WpEndpoint *endpoint, TestEndpointFixture *fixture)
{
  g_debug ("impl object added");

  g_assert_true (WP_IS_ENDPOINT (endpoint));
  g_assert_cmpstr (G_OBJECT_TYPE_NAME (endpoint), ==, "WpImplEndpoint");

  g_assert_null (fixture->impl_endpoint);
  fixture->impl_endpoint = WP_PROXY (endpoint);

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_endpoint_basic_impl_object_removed (WpObjectManager *om,
    WpEndpoint *endpoint, TestEndpointFixture *fixture)
{
  g_debug ("impl object removed");

  g_assert_true (WP_IS_ENDPOINT (endpoint));
  g_assert_cmpstr (G_OBJECT_TYPE_NAME (endpoint), ==, "WpImplEndpoint");

  g_assert_nonnull (fixture->impl_endpoint);
  fixture->impl_endpoint = NULL;

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_endpoint_basic_proxy_object_added (WpObjectManager *om,
    WpEndpoint *endpoint, TestEndpointFixture *fixture)
{
  g_debug ("proxy object added");

  g_assert_true (WP_IS_ENDPOINT (endpoint));
  g_assert_cmpstr (G_OBJECT_TYPE_NAME (endpoint), ==, "WpEndpoint");

  g_assert_null (fixture->proxy_endpoint);
  fixture->proxy_endpoint = WP_PROXY (endpoint);

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_endpoint_basic_proxy_object_removed (WpObjectManager *om,
    WpEndpoint *endpoint, TestEndpointFixture *fixture)
{
  g_debug ("proxy object removed");

  g_assert_true (WP_IS_ENDPOINT (endpoint));
  g_assert_cmpstr (G_OBJECT_TYPE_NAME (endpoint), ==, "WpEndpoint");

  g_assert_nonnull (fixture->proxy_endpoint);
  fixture->proxy_endpoint = NULL;

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_endpoint_basic_activate_done (WpSessionItem * item, GAsyncResult * res,
    TestEndpointFixture *fixture)
{
  g_autoptr (GError) error = NULL;

  g_debug ("activate done");

  g_assert_true (wp_session_item_activate_finish (item, res, &error));
  g_assert_no_error (error);
}

static void
test_endpoint_basic_export_done (WpSessionItem * item, GAsyncResult * res,
    TestEndpointFixture *fixture)
{
  g_autoptr (GError) error = NULL;

  g_debug ("export done");

  g_assert_true (wp_session_item_export_finish (item, res, &error));
  g_assert_no_error (error);

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_endpoint_basic_session_bound (WpProxy * session, GAsyncResult * res,
    TestEndpointFixture *fixture)
{
  g_autoptr (GError) error = NULL;

  g_debug ("session export done");

  g_assert_true (wp_proxy_augment_finish (session, res, &error));
  g_assert_no_error (error);

  g_assert_true (WP_IS_IMPL_SESSION (session));

  g_main_loop_quit (fixture->base.loop);
}

#if 0
static void
test_endpoint_basic_control_changed (WpEndpoint * endpoint,
    const gchar * id_name, TestEndpointFixture *fixture)
{
  g_debug ("endpoint changed: %s (%s)", G_OBJECT_TYPE_NAME (endpoint), id_name);

  g_assert_true (WP_IS_ENDPOINT (endpoint));

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_endpoint_basic_notify_properties (WpEndpoint * endpoint, GParamSpec * param,
    TestEndpointFixture *fixture)
{
  g_debug ("properties changed: %s", G_OBJECT_TYPE_NAME (endpoint));

  g_assert_true (WP_IS_ENDPOINT (endpoint));

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->base.loop);
}
#endif

static void
test_endpoint_basic (TestEndpointFixture *fixture, gconstpointer data)
{
  g_autoptr (TestSiEndpoint) endpoint = NULL;
  g_autoptr (WpImplSession) session = NULL;
  // gfloat float_value;
  // gboolean boolean_value;
  // g_autoptr (WpSpaPod) ctrl = NULL;

  /* set up the export side */
  g_signal_connect (fixture->export_om, "object-added",
      (GCallback) test_endpoint_basic_impl_object_added, fixture);
  g_signal_connect (fixture->export_om, "object-removed",
      (GCallback) test_endpoint_basic_impl_object_removed, fixture);
  wp_object_manager_add_interest (fixture->export_om, WP_TYPE_ENDPOINT, NULL);
  wp_object_manager_request_proxy_features (fixture->export_om,
      WP_TYPE_ENDPOINT, WP_PROXY_FEATURES_STANDARD | WP_PROXY_FEATURE_PROPS);
  wp_core_install_object_manager (fixture->base.core, fixture->export_om);

  /* set up the proxy side */
  g_signal_connect (fixture->proxy_om, "object-added",
      (GCallback) test_endpoint_basic_proxy_object_added, fixture);
  g_signal_connect (fixture->proxy_om, "object-removed",
      (GCallback) test_endpoint_basic_proxy_object_removed, fixture);
  wp_object_manager_add_interest (fixture->proxy_om, WP_TYPE_ENDPOINT, NULL);
  wp_object_manager_request_proxy_features (fixture->proxy_om, WP_TYPE_ENDPOINT,
      WP_PROXY_FEATURES_STANDARD | WP_PROXY_FEATURE_PROPS);
  wp_core_install_object_manager (fixture->base.client_core, fixture->proxy_om);

  /* create session */
  session = wp_impl_session_new (fixture->base.core);
  wp_proxy_augment (WP_PROXY (session), WP_PROXY_FEATURE_BOUND, NULL,
      (GAsyncReadyCallback) test_endpoint_basic_session_bound, fixture);

  /* run until session is bound */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (wp_proxy_get_features (WP_PROXY (session)), &,
      WP_PROXY_FEATURE_BOUND);
  g_assert_cmpint (wp_proxy_get_bound_id (WP_PROXY (session)), >, 0);

  /* create endpoint */
  endpoint = g_object_new (test_si_endpoint_get_type (), NULL);
  endpoint->name = "test-endpoint";
  endpoint->media_class = "Audio/Source";
  endpoint->direction = WP_DIRECTION_OUTPUT;
  wp_session_item_activate (WP_SESSION_ITEM (endpoint),
      (GAsyncReadyCallback) test_endpoint_basic_activate_done, fixture);
  g_assert_cmpint (wp_session_item_get_flags (WP_SESSION_ITEM (endpoint)),
      &, WP_SI_FLAG_ACTIVE);
  wp_session_item_export (WP_SESSION_ITEM (endpoint), WP_SESSION (session),
      (GAsyncReadyCallback) test_endpoint_basic_export_done, fixture);

  /* run until objects are created and features are cached */
  fixture->n_events = 0;
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 3);
  g_assert_nonnull (fixture->impl_endpoint);
  g_assert_nonnull (fixture->proxy_endpoint);

  /* test round 1: verify the values on the proxy */

  g_assert_cmphex (wp_proxy_get_features (fixture->proxy_endpoint), ==,
      WP_PROXY_FEATURE_PW_PROXY |
      WP_PROXY_FEATURE_INFO |
      WP_PROXY_FEATURE_BOUND |
      WP_PROXY_FEATURE_PROPS);

  g_assert_cmpuint (wp_proxy_get_bound_id (fixture->proxy_endpoint), ==,
      wp_proxy_get_bound_id (fixture->impl_endpoint));

  g_assert_cmpstr (wp_proxy_get_property (fixture->proxy_endpoint,
          "test.property"), ==, "test-value");

  {
    g_autoptr (WpProperties) props =
        wp_proxy_get_global_properties (fixture->proxy_endpoint);
    g_autofree gchar * session_id = g_strdup_printf ("%u",
        wp_proxy_get_bound_id (WP_PROXY (session)));

    g_assert_cmpstr (wp_properties_get (props, PW_KEY_ENDPOINT_NAME), ==,
        "test-endpoint");
    g_assert_cmpstr (wp_properties_get (props, PW_KEY_MEDIA_CLASS), ==,
        "Audio/Source");
    g_assert_cmpstr (wp_properties_get (props, PW_KEY_SESSION_ID), ==,
        session_id);
  }

  g_assert_cmpstr ("test-endpoint", ==,
      wp_endpoint_get_name (WP_ENDPOINT (fixture->proxy_endpoint)));
  g_assert_cmpstr ("Audio/Source", ==,
      wp_endpoint_get_media_class (WP_ENDPOINT (fixture->proxy_endpoint)));
  g_assert_cmpint (WP_DIRECTION_OUTPUT, ==,
      wp_endpoint_get_direction (WP_ENDPOINT (fixture->proxy_endpoint)));

#if 0
  g_assert_true (wp_endpoint_get_control_float (
          WP_ENDPOINT (fixture->proxy_endpoint),
          WP_ENDPOINT_CONTROL_VOLUME, &float_value));
  g_assert_true (wp_endpoint_get_control_boolean (
          WP_ENDPOINT (fixture->proxy_endpoint),
          WP_ENDPOINT_CONTROL_MUTE, &boolean_value));
  g_assert_cmpfloat_with_epsilon (float_value, 0.7f, 0.001);
  g_assert_cmpint (boolean_value, ==, TRUE);

  /* setup change signals */
  g_signal_connect (fixture->proxy_endpoint, "control-changed",
      (GCallback) test_endpoint_basic_control_changed, fixture);
  g_signal_connect (endpoint, "control-changed",
      (GCallback) test_endpoint_basic_control_changed, fixture);
  g_signal_connect (fixture->proxy_endpoint, "notify::properties",
      (GCallback) test_endpoint_basic_notify_properties, fixture);
  g_signal_connect (endpoint, "notify::properties",
      (GCallback) test_endpoint_basic_notify_properties, fixture);

  /* change control on the proxy */
  ctrl = wp_spa_pod_new_float (1.0f);
  g_assert_true (wp_endpoint_set_control (WP_ENDPOINT (fixture->proxy_endpoint),
      "volume", ctrl));

  /* run until the change is on both sides */
  fixture->n_events = 0;
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 2);

  /* test round 2: verify the value change on both sides */

  ctrl = wp_endpoint_get_control (WP_ENDPOINT (fixture->proxy_endpoint), "volume");
  g_assert_nonnull (ctrl);
  g_assert_true (wp_spa_pod_get_float (ctrl, &float_value));
  ctrl = wp_endpoint_get_control (WP_ENDPOINT (fixture->proxy_endpoint), "mute");
  g_assert_nonnull (ctrl);
  g_assert_true (wp_spa_pod_get_boolean (ctrl, &boolean_value));
  g_assert_cmpfloat_with_epsilon (float_value, 1.0f, 0.001);
  g_assert_cmpint (boolean_value, ==, TRUE);

  ctrl = wp_endpoint_get_control (WP_ENDPOINT (endpoint), "volume");
  g_assert_nonnull (ctrl);
  g_assert_true (wp_spa_pod_get_float (ctrl, &float_value));
  ctrl = wp_endpoint_get_control (WP_ENDPOINT (endpoint), "mute");
  g_assert_nonnull (ctrl);
  g_assert_true (wp_spa_pod_get_boolean (ctrl, &boolean_value));
  g_assert_cmpfloat_with_epsilon (float_value, 1.0f, 0.001);
  g_assert_cmpint (boolean_value, ==, TRUE);

  /* change control on the impl */
  fixture->n_events = 0;
  ctrl = wp_spa_pod_new_boolean (FALSE);
  g_assert_true (wp_endpoint_set_control (WP_ENDPOINT (endpoint), "mute", ctrl));

  /* run until the change is on both sides */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 2);

  /* test round 3: verify the value change on both sides */

  ctrl = wp_endpoint_get_control (WP_ENDPOINT (fixture->proxy_endpoint), "volume");
  g_assert_nonnull (ctrl);
  g_assert_true (wp_spa_pod_get_float (ctrl, &float_value));
  ctrl = wp_endpoint_get_control (WP_ENDPOINT (fixture->proxy_endpoint), "mute");
  g_assert_nonnull (ctrl);
  g_assert_true (wp_spa_pod_get_boolean (ctrl, &boolean_value));
  g_assert_cmpfloat_with_epsilon (float_value, 1.0f, 0.001);
  g_assert_cmpint (boolean_value, ==, FALSE);

  ctrl = wp_endpoint_get_control (WP_ENDPOINT (endpoint), "volume");
  g_assert_nonnull (ctrl);
  g_assert_true (wp_spa_pod_get_float (ctrl, &float_value));
  ctrl = wp_endpoint_get_control (WP_ENDPOINT (endpoint), "mute");
  g_assert_nonnull (ctrl);
  g_assert_true (wp_spa_pod_get_boolean (ctrl, &boolean_value));
  g_assert_cmpfloat_with_epsilon (float_value, 1.0f, 0.001);
  g_assert_cmpint (boolean_value, ==, FALSE);

  /* change a property on the impl */
  fixture->n_events = 0;
  wp_impl_endpoint_set_property (endpoint, "test.property", "changed-value");

  /* run until the change is on both sides */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 2);

  /* test round 4: verify the property change on both sides */

  {
    g_autoptr (WpProperties) props =
        wp_proxy_get_properties (WP_PROXY (endpoint));
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "changed-value");
  }
  {
    g_autoptr (WpProperties) props =
        wp_proxy_get_properties (fixture->proxy_endpoint);
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "changed-value");
  }
#endif

  /* destroy impl endpoint */
  fixture->n_events = 0;
  g_clear_object (&endpoint);

  /* run until objects are destroyed */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 2);
  g_assert_null (fixture->impl_endpoint);
  g_assert_null (fixture->proxy_endpoint);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/endpoint/basic", TestEndpointFixture, NULL,
      test_endpoint_setup, test_endpoint_basic, test_endpoint_teardown);

  return g_test_run ();
}
