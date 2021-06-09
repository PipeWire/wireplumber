/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
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
  WpNode *node;
  WpDirection direction;
  gboolean changed_properties;
  WpProxy *impl_endpoint;
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
  TestSiEndpoint *self = TEST_SI_ENDPOINT (item);
  if (self->changed_properties)
    return wp_properties_new ("test.property", "changed-value", NULL);
  else
    return wp_properties_new ("test.property", "test-value", NULL);
}

static void
test_si_endpoint_endpoint_init (WpSiEndpointInterface * iface)
{
  iface->get_registration_info = test_si_endpoint_get_registration_info;
  iface->get_properties = test_si_endpoint_get_properties;
}

G_DEFINE_TYPE_WITH_CODE (TestSiEndpoint, test_si_endpoint, WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ENDPOINT, test_si_endpoint_endpoint_init))

static void
test_si_endpoint_init (TestSiEndpoint * self)
{
}

static gpointer
si_endpoint_get_associated_proxy (WpSessionItem * item, GType proxy_type)
{
  TestSiEndpoint * self = TEST_SI_ENDPOINT (item);

  if (proxy_type == WP_TYPE_NODE)
    return self->node ? g_object_ref (self->node) : NULL;

  return NULL;
}

static void
si_endpoint_reset (WpSessionItem * item)
{
  TestSiEndpoint * self = TEST_SI_ENDPOINT (item);

  wp_object_deactivate (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED);

  g_clear_object (&self->node);

  WP_SESSION_ITEM_CLASS (test_si_endpoint_parent_class)->reset (item);
}

static void
si_endpoint_disable_active (WpSessionItem *si)
{
  TestSiEndpoint * self = TEST_SI_ENDPOINT (si);

  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
}

static void
si_endpoint_disable_exported (WpSessionItem *si)
{
  TestSiEndpoint * self = TEST_SI_ENDPOINT (si);

  g_clear_object (&self->impl_endpoint);
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_EXPORTED);
}

static void
si_endpoint_enable_active (WpSessionItem *si, WpTransition *transition)
{
  TestSiEndpoint * self = TEST_SI_ENDPOINT (si);

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
}

static void
on_impl_endpoint_activated (WpObject * object, GAsyncResult * res,
    WpTransition * transition)
{
  TestSiEndpoint *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (object, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self),
          WP_SESSION_ITEM_FEATURE_EXPORTED, 0);
}

static void
si_endpoint_enable_exported (WpSessionItem *si, WpTransition *transition)
{
  TestSiEndpoint * self = TEST_SI_ENDPOINT (si);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  self->impl_endpoint = WP_PROXY (wp_impl_endpoint_new (core,
      WP_SI_ENDPOINT (self)));
  g_signal_connect_object (self->impl_endpoint, "pw-proxy-destroyed",
      G_CALLBACK (wp_session_item_handle_proxy_destroyed), self, 0);

  wp_object_activate (WP_OBJECT (self->impl_endpoint),
      WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_impl_endpoint_activated, transition);
}

static void
test_si_endpoint_class_init (TestSiEndpointClass * klass)
{
  WpSessionItemClass *item_class = (WpSessionItemClass *) klass;

  item_class->reset = si_endpoint_reset;
  item_class->get_associated_proxy = si_endpoint_get_associated_proxy;
  item_class->disable_active = si_endpoint_disable_active;
  item_class->disable_exported = si_endpoint_disable_exported;
  item_class->enable_active = si_endpoint_enable_active;
  item_class->enable_exported = si_endpoint_enable_exported;
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
test_endpoint_impl_object_added (WpObjectManager *om,
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
test_endpoint_impl_object_removed (WpObjectManager *om,
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
test_endpoint_proxy_object_added (WpObjectManager *om,
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
test_endpoint_proxy_object_removed (WpObjectManager *om,
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
test_endpoint_activate_done (WpObject * object, GAsyncResult * res,
    TestEndpointFixture *fixture)
{
  g_autoptr (GError) error = NULL;

  g_debug ("activate done");

  g_assert_true (wp_object_activate_finish (object, res, &error));
  g_assert_no_error (error);

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_endpoint_params_changed (WpPipewireObject * proxy,
    const gchar * param_name, TestEndpointFixture *fixture)
{
  wp_debug_object (proxy, "params changed: %s", param_name);

  /* only count changes of id 2 (Props); PipeWire 0.3.22+git changed
     behaviour and emits changes to PropInfo as well then the Props change */
  if (!g_strcmp0 (param_name, "Props") && ++fixture->n_events == 3)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_endpoint_notify_properties (WpEndpoint * endpoint, GParamSpec * param,
    TestEndpointFixture *fixture)
{
  g_debug ("properties changed: %s", G_OBJECT_TYPE_NAME (endpoint));

  g_assert_true (WP_IS_ENDPOINT (endpoint));

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_endpoint_no_props (TestEndpointFixture *fixture, gconstpointer data)
{
  g_autoptr (TestSiEndpoint) endpoint = NULL;

  /* set up the export side */
  g_signal_connect (fixture->export_om, "object-added",
      (GCallback) test_endpoint_impl_object_added, fixture);
  g_signal_connect (fixture->export_om, "object-removed",
      (GCallback) test_endpoint_impl_object_removed, fixture);
  wp_object_manager_add_interest (fixture->export_om, WP_TYPE_ENDPOINT, NULL);
  wp_object_manager_request_object_features (fixture->export_om,
      WP_TYPE_ENDPOINT, WP_OBJECT_FEATURES_ALL);
  wp_core_install_object_manager (fixture->base.core, fixture->export_om);

  /* set up the proxy side */
  g_signal_connect (fixture->proxy_om, "object-added",
      (GCallback) test_endpoint_proxy_object_added, fixture);
  g_signal_connect (fixture->proxy_om, "object-removed",
      (GCallback) test_endpoint_proxy_object_removed, fixture);
  wp_object_manager_add_interest (fixture->proxy_om, WP_TYPE_ENDPOINT, NULL);
  wp_object_manager_request_object_features (fixture->proxy_om,
      WP_TYPE_ENDPOINT, WP_OBJECT_FEATURES_ALL);
  wp_core_install_object_manager (fixture->base.client_core, fixture->proxy_om);

  /* create endpoint */
  endpoint = g_object_new (test_si_endpoint_get_type (),
      "core", fixture->base.core, NULL);
  endpoint->name = "test-endpoint";
  endpoint->media_class = "Audio/Source";
  endpoint->direction = WP_DIRECTION_OUTPUT;
  wp_object_activate (WP_OBJECT (endpoint),
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED,
      NULL, (GAsyncReadyCallback) test_endpoint_activate_done, fixture);

  /* run until objects are created and features are cached */
  fixture->n_events = 0;
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (wp_object_get_active_features (WP_OBJECT (endpoint)), ==,
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED);
  g_assert_cmpint (fixture->n_events, ==, 3);
  g_assert_nonnull (fixture->impl_endpoint);
  g_assert_nonnull (fixture->proxy_endpoint);

  /* verify the values on the proxy */

  g_assert_cmphex (
      wp_object_get_active_features (WP_OBJECT (fixture->proxy_endpoint)), ==,
      wp_object_get_supported_features (WP_OBJECT (fixture->proxy_endpoint)));
  g_assert_cmpuint (wp_proxy_get_bound_id (fixture->proxy_endpoint), ==,
      wp_proxy_get_bound_id (fixture->impl_endpoint));

  g_assert_cmpstr (wp_pipewire_object_get_property (
          WP_PIPEWIRE_OBJECT (fixture->proxy_endpoint), "test.property"),
      ==, "test-value");

  {
    g_autoptr (WpProperties) props = wp_global_proxy_get_global_properties (
        WP_GLOBAL_PROXY (fixture->proxy_endpoint));

    g_assert_cmpstr (wp_properties_get (props, PW_KEY_ENDPOINT_NAME), ==,
        "test-endpoint");
    g_assert_cmpstr (wp_properties_get (props, PW_KEY_MEDIA_CLASS), ==,
        "Audio/Source");
  }

  g_assert_cmpstr ("test-endpoint", ==,
      wp_endpoint_get_name (WP_ENDPOINT (fixture->proxy_endpoint)));
  g_assert_cmpstr ("Audio/Source", ==,
      wp_endpoint_get_media_class (WP_ENDPOINT (fixture->proxy_endpoint)));
  g_assert_cmpint (WP_DIRECTION_OUTPUT, ==,
      wp_endpoint_get_direction (WP_ENDPOINT (fixture->proxy_endpoint)));

  /* test property changes */
  g_signal_connect (fixture->proxy_endpoint, "notify::properties",
      (GCallback) test_endpoint_notify_properties, fixture);
  g_signal_connect (fixture->impl_endpoint, "notify::properties",
      (GCallback) test_endpoint_notify_properties, fixture);

  /* change a property on the impl */
  fixture->n_events = 0;
  endpoint->changed_properties = TRUE;
  g_signal_emit_by_name (endpoint, "endpoint-properties-changed");

  /* run until the change is on both sides */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 2);

  /* verify the property change on both sides */
  {
    g_autoptr (WpProperties) props = wp_pipewire_object_get_properties (
        WP_PIPEWIRE_OBJECT (fixture->impl_endpoint));
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "changed-value");
  }
  {
    g_autoptr (WpProperties) props = wp_pipewire_object_get_properties (
        WP_PIPEWIRE_OBJECT (fixture->proxy_endpoint));
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "changed-value");
  }

  /* destroy impl endpoint */
  fixture->n_events = 0;
  g_clear_object (&endpoint);

  /* run until objects are destroyed */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 2);
  g_assert_null (fixture->impl_endpoint);
  g_assert_null (fixture->proxy_endpoint);
}

static void
test_endpoint_with_props (TestEndpointFixture *fixture, gconstpointer data)
{
  g_autoptr (TestSiEndpoint) endpoint = NULL;

  /* load modules */
  {
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&fixture->base.server);

    g_assert_cmpint (pw_context_add_spa_lib (fixture->base.server.context,
            "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), ==, 0);
    g_assert_nonnull (pw_context_load_module (fixture->base.server.context,
            "libpipewire-module-adapter", NULL, NULL));
  }

  /* set up the export side */
  g_signal_connect (fixture->export_om, "object-added",
      (GCallback) test_endpoint_impl_object_added, fixture);
  g_signal_connect (fixture->export_om, "object-removed",
      (GCallback) test_endpoint_impl_object_removed, fixture);
  wp_object_manager_add_interest (fixture->export_om, WP_TYPE_ENDPOINT, NULL);
  wp_object_manager_request_object_features (fixture->export_om,
      WP_TYPE_ENDPOINT, WP_OBJECT_FEATURES_ALL);
  wp_core_install_object_manager (fixture->base.core, fixture->export_om);

  /* set up the proxy side */
  g_signal_connect (fixture->proxy_om, "object-added",
      (GCallback) test_endpoint_proxy_object_added, fixture);
  g_signal_connect (fixture->proxy_om, "object-removed",
      (GCallback) test_endpoint_proxy_object_removed, fixture);
  wp_object_manager_add_interest (fixture->proxy_om, WP_TYPE_ENDPOINT, NULL);
  wp_object_manager_request_object_features (fixture->proxy_om,
      WP_TYPE_ENDPOINT, WP_OBJECT_FEATURES_ALL);
  wp_core_install_object_manager (fixture->base.client_core, fixture->proxy_om);

  /* create endpoint */
  endpoint = g_object_new (test_si_endpoint_get_type (),
      "core", fixture->base.core, NULL);
  endpoint->name = "test-endpoint";
  endpoint->media_class = "Audio/Source";
  endpoint->direction = WP_DIRECTION_OUTPUT;

  /* associate a node that has props */
  endpoint->node = wp_node_new_from_factory (fixture->base.core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", "audiotestsrc.adapter",
          NULL));
  g_assert_nonnull (endpoint->node);
  wp_object_activate (WP_OBJECT (endpoint->node),
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL, NULL,
      (GAsyncReadyCallback) test_object_activate_finish_cb, fixture);
  g_main_loop_run (fixture->base.loop);

  g_assert_cmpint (wp_object_get_active_features (WP_OBJECT (endpoint->node)),
      ==, WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);

  /* activate & export the endpoint */
  wp_object_activate (WP_OBJECT (endpoint),
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED,
      NULL, (GAsyncReadyCallback) test_endpoint_activate_done, fixture);

  /* run until objects are created and features are cached */
  fixture->n_events = 0;
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (wp_object_get_active_features (WP_OBJECT (endpoint)), ==,
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED);
  g_assert_cmpint (fixture->n_events, ==, 3);
  g_assert_nonnull (fixture->impl_endpoint);
  g_assert_nonnull (fixture->proxy_endpoint);

  /* verify features; the endpoint must have also augmented the node */
  g_assert_cmpint (
      wp_object_get_active_features (WP_OBJECT (endpoint->node)), &,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL | WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS);
  g_assert_cmphex (
      wp_object_get_active_features (WP_OBJECT (fixture->proxy_endpoint)), &,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL | WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS);

  /* verify props */
  {
    g_autoptr (WpIterator) iterator = NULL;
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpSpaPod) pod = NULL;
    gfloat float_value = 0.0f;
    gboolean boolean_value = TRUE;

    iterator = wp_pipewire_object_enum_params_sync (
        WP_PIPEWIRE_OBJECT (fixture->proxy_endpoint), "Props", NULL);
    g_assert_nonnull (iterator);

    g_assert_true (wp_iterator_next (iterator, &item));
    g_assert_nonnull ((pod = g_value_dup_boxed (&item)));

    g_assert_true (wp_spa_pod_get_object (pod, NULL,
            "volume", "f", &float_value,
            "mute", "b", &boolean_value,
            NULL));
    g_assert_cmpfloat_with_epsilon (float_value, 1.0f, 0.001);
    g_assert_cmpint (boolean_value, ==, FALSE);
  }

  /* setup change signals */
  g_signal_connect (fixture->proxy_endpoint, "params-changed",
      (GCallback) test_endpoint_params_changed, fixture);
  g_signal_connect (fixture->impl_endpoint, "params-changed",
      (GCallback) test_endpoint_params_changed, fixture);
  g_signal_connect (endpoint->node, "params-changed",
      (GCallback) test_endpoint_params_changed, fixture);

  /* change control on the proxy */
  fixture->n_events = 0;
  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (fixture->proxy_endpoint),
      "Props", 0,
      wp_spa_pod_new_object ("Spa:Pod:Object:Param:Props", "Props",
          "volume", "f", 0.7f, NULL));

  /* run until the change is on all sides */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 3);

  /* verify the value change on all sides */
  {
    g_autoptr (WpIterator) iterator = NULL;
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpSpaPod) pod = NULL;
    gfloat float_value = 0.0f;
    gboolean boolean_value = TRUE;

    iterator = wp_pipewire_object_enum_params_sync (
        WP_PIPEWIRE_OBJECT (fixture->proxy_endpoint), "Props", NULL);
    g_assert_nonnull (iterator);

    g_assert_true (wp_iterator_next (iterator, &item));
    g_assert_nonnull ((pod = g_value_dup_boxed (&item)));

    g_assert_true (wp_spa_pod_get_object (pod, NULL,
            "volume", "f", &float_value,
            "mute", "b", &boolean_value,
            NULL));
    g_assert_cmpfloat_with_epsilon (float_value, 0.7f, 0.001);
    g_assert_cmpint (boolean_value, ==, FALSE);
  }
  {
    g_autoptr (WpIterator) iterator = NULL;
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpSpaPod) pod = NULL;
    gfloat float_value = 0.0f;
    gboolean boolean_value = TRUE;

    iterator = wp_pipewire_object_enum_params_sync (
        WP_PIPEWIRE_OBJECT (fixture->impl_endpoint), "Props", NULL);
    g_assert_nonnull (iterator);

    g_assert_true (wp_iterator_next (iterator, &item));
    g_assert_nonnull ((pod = g_value_dup_boxed (&item)));

    g_assert_true (wp_spa_pod_get_object (pod, NULL,
            "volume", "f", &float_value,
            "mute", "b", &boolean_value,
            NULL));
    g_assert_cmpfloat_with_epsilon (float_value, 0.7f, 0.001);
    g_assert_cmpint (boolean_value, ==, FALSE);
  }
  {
    g_autoptr (WpIterator) iterator = NULL;
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpSpaPod) pod = NULL;
    gfloat float_value = 0.0f;
    gboolean boolean_value = TRUE;

    iterator = wp_pipewire_object_enum_params_sync (
        WP_PIPEWIRE_OBJECT (endpoint->node), "Props", NULL);
    g_assert_nonnull (iterator);

    g_assert_true (wp_iterator_next (iterator, &item));
    g_assert_nonnull ((pod = g_value_dup_boxed (&item)));

    g_assert_true (wp_spa_pod_get_object (pod, NULL,
            "volume", "f", &float_value,
            "mute", "b", &boolean_value,
            NULL));
    g_assert_cmpfloat_with_epsilon (float_value, 0.7f, 0.001);
    g_assert_cmpint (boolean_value, ==, FALSE);
  }

  /* change control on the impl */
  fixture->n_events = 0;
  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (fixture->impl_endpoint),
      "Props", 0,
      wp_spa_pod_new_object ("Spa:Pod:Object:Param:Props", "Props",
          "mute", "b", TRUE, NULL));

  /* run until the change is on all sides */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 3);

  /* verify the value change on all sides */
  {
    g_autoptr (WpIterator) iterator = NULL;
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpSpaPod) pod = NULL;
    gfloat float_value = 0.0f;
    gboolean boolean_value = TRUE;

    iterator = wp_pipewire_object_enum_params_sync (
        WP_PIPEWIRE_OBJECT (fixture->proxy_endpoint), "Props", NULL);
    g_assert_nonnull (iterator);

    g_assert_true (wp_iterator_next (iterator, &item));
    g_assert_nonnull ((pod = g_value_dup_boxed (&item)));

    g_assert_true (wp_spa_pod_get_object (pod, NULL,
            "volume", "f", &float_value,
            "mute", "b", &boolean_value,
            NULL));
    g_assert_cmpfloat_with_epsilon (float_value, 0.7f, 0.001);
    g_assert_cmpint (boolean_value, ==, TRUE);
  }
  {
    g_autoptr (WpIterator) iterator = NULL;
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpSpaPod) pod = NULL;
    gfloat float_value = 0.0f;
    gboolean boolean_value = TRUE;

    iterator = wp_pipewire_object_enum_params_sync (
        WP_PIPEWIRE_OBJECT (fixture->impl_endpoint), "Props", NULL);
    g_assert_nonnull (iterator);

    g_assert_true (wp_iterator_next (iterator, &item));
    g_assert_nonnull ((pod = g_value_dup_boxed (&item)));

    g_assert_true (wp_spa_pod_get_object (pod, NULL,
            "volume", "f", &float_value,
            "mute", "b", &boolean_value,
            NULL));
    g_assert_cmpfloat_with_epsilon (float_value, 0.7f, 0.001);
    g_assert_cmpint (boolean_value, ==, TRUE);
  }
  {
    g_autoptr (WpIterator) iterator = NULL;
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpSpaPod) pod = NULL;
    gfloat float_value = 0.0f;
    gboolean boolean_value = TRUE;

    iterator = wp_pipewire_object_enum_params_sync (
        WP_PIPEWIRE_OBJECT (endpoint->node), "Props", NULL);
    g_assert_nonnull (iterator);

    g_assert_true (wp_iterator_next (iterator, &item));
    g_assert_nonnull ((pod = g_value_dup_boxed (&item)));

    g_assert_true (wp_spa_pod_get_object (pod, NULL,
            "volume", "f", &float_value,
            "mute", "b", &boolean_value,
            NULL));
    g_assert_cmpfloat_with_epsilon (float_value, 0.7f, 0.001);
    g_assert_cmpint (boolean_value, ==, TRUE);
  }

  /* change control on the node */
  fixture->n_events = 0;
  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (endpoint->node),
      "Props", 0,
      wp_spa_pod_new_object ("Spa:Pod:Object:Param:Props", "Props",
          "volume", "f", 0.2f, NULL));

  /* run until the change is on all sides */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 3);

  /* verify the value change on all sides */
  {
    g_autoptr (WpIterator) iterator = NULL;
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpSpaPod) pod = NULL;
    gfloat float_value = 0.0f;
    gboolean boolean_value = TRUE;

    iterator = wp_pipewire_object_enum_params_sync (
        WP_PIPEWIRE_OBJECT (fixture->proxy_endpoint), "Props", NULL);
    g_assert_nonnull (iterator);

    g_assert_true (wp_iterator_next (iterator, &item));
    g_assert_nonnull ((pod = g_value_dup_boxed (&item)));

    g_assert_true (wp_spa_pod_get_object (pod, NULL,
            "volume", "f", &float_value,
            "mute", "b", &boolean_value,
            NULL));
    g_assert_cmpfloat_with_epsilon (float_value, 0.2f, 0.001);
    g_assert_cmpint (boolean_value, ==, TRUE);
  }
  {
    g_autoptr (WpIterator) iterator = NULL;
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpSpaPod) pod = NULL;
    gfloat float_value = 0.0f;
    gboolean boolean_value = TRUE;

    iterator = wp_pipewire_object_enum_params_sync (
        WP_PIPEWIRE_OBJECT (fixture->impl_endpoint), "Props", NULL);
    g_assert_nonnull (iterator);

    g_assert_true (wp_iterator_next (iterator, &item));
    g_assert_nonnull ((pod = g_value_dup_boxed (&item)));

    g_assert_true (wp_spa_pod_get_object (pod, NULL,
            "volume", "f", &float_value,
            "mute", "b", &boolean_value,
            NULL));
    g_assert_cmpfloat_with_epsilon (float_value, 0.2f, 0.001);
    g_assert_cmpint (boolean_value, ==, TRUE);
  }
  {
    g_autoptr (WpIterator) iterator = NULL;
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpSpaPod) pod = NULL;
    gfloat float_value = 0.0f;
    gboolean boolean_value = TRUE;

    iterator = wp_pipewire_object_enum_params_sync (
        WP_PIPEWIRE_OBJECT (endpoint->node), "Props", NULL);
    g_assert_nonnull (iterator);

    g_assert_true (wp_iterator_next (iterator, &item));
    g_assert_nonnull ((pod = g_value_dup_boxed (&item)));

    g_assert_true (wp_spa_pod_get_object (pod, NULL,
            "volume", "f", &float_value,
            "mute", "b", &boolean_value,
            NULL));
    g_assert_cmpfloat_with_epsilon (float_value, 0.2f, 0.001);
    g_assert_cmpint (boolean_value, ==, TRUE);
  }

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

  g_test_add ("/wp/endpoint/no-props", TestEndpointFixture, NULL,
      test_endpoint_setup, test_endpoint_no_props, test_endpoint_teardown);
  g_test_add ("/wp/endpoint/with-props", TestEndpointFixture, NULL,
      test_endpoint_setup, test_endpoint_with_props, test_endpoint_teardown);

  return g_test_run ();
}
