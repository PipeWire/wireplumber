/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>

#include <wp/wp.h>

#include "parser-endpoint.h"
#include "parser-streams.h"
#include "context.h"

struct _WpConfigEndpointContext
{
  GObject parent;

  /* Props */
  GWeakRef core;

  WpObjectManager *om;
  GHashTable *registered_endpoints;
};

enum {
  PROP_0,
  PROP_CORE,
};

enum {
  SIGNAL_ENDPOINT_CREATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (WpConfigEndpointContext, wp_config_endpoint_context,
    G_TYPE_OBJECT)

static void
on_endpoint_created (GObject *initable, GAsyncResult *res, gpointer d)
{
  WpConfigEndpointContext *self = d;
  g_autoptr (WpBaseEndpoint) endpoint = NULL;
  g_autoptr (WpProxy) proxy = NULL;
  guint global_id = 0;
  GError *error = NULL;

  /* Get the endpoint */
  endpoint = wp_base_endpoint_new_finish (initable, res, &error);
  if (error) {
    g_warning ("Failed to create endpoint: %s", error->message);
    return;
  }

  /* Get the endpoint global id */
  g_object_get (endpoint, "node", &proxy, NULL);
  global_id = wp_proxy_get_bound_id (proxy);

  /* Register the endpoint and add it to the table */
  wp_base_endpoint_register (endpoint);
  g_hash_table_insert (self->registered_endpoints, GUINT_TO_POINTER (global_id),
      g_object_ref (endpoint));

  /* Emit the endpoint-created signal */
  g_signal_emit (self, signals[SIGNAL_ENDPOINT_CREATED], 0, endpoint);
}

static GVariant *
create_streams_variant (WpConfiguration *config, const char *streams)
{
  g_autoptr (WpConfigParser) parser = NULL;
  const struct WpParserStreamsData *streams_data = NULL;
  g_autoptr (GVariantBuilder) ba = NULL;

  if (!streams || !config)
    return NULL;

  /* Get the streams parser */
  parser = wp_configuration_get_parser (config, WP_PARSER_STREAMS_EXTENSION);
  if (!parser)
    return NULL;

  /* Get the streams data */
  streams_data = wp_config_parser_get_matched_data (parser, (gpointer)streams);
  if (!streams_data || streams_data->n_streams <= 0)
    return NULL;

  /* Build the variant array with the stream name and priority */
  ba = g_variant_builder_new (G_VARIANT_TYPE ("a(su)"));
  g_variant_builder_init (ba, G_VARIANT_TYPE_ARRAY);
  for (guint i = 0; i < streams_data->n_streams; i++)
    g_variant_builder_add (ba, "(su)", streams_data->streams[i].name,
        streams_data->streams[i].priority);

  return g_variant_new ("a(su)", ba);
}

static void
on_node_added (WpObjectManager *om, WpProxy *proxy, gpointer d)
{
  WpConfigEndpointContext *self = d;
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  g_autoptr (WpProperties) props = wp_proxy_get_properties (proxy);
  g_autoptr (WpConfigParser) parser = NULL;
  const struct WpParserEndpointData *endpoint_data = NULL;
  GVariantBuilder b;
  g_autoptr (GVariant) endpoint_props = NULL;
  const char *media_class = NULL, *name = NULL;
  g_autoptr (GVariant) streams_variant = NULL;

  /* Get the linked and ep streams data */
  parser = wp_configuration_get_parser (config, WP_PARSER_ENDPOINT_EXTENSION);
  endpoint_data = wp_config_parser_get_matched_data (parser, proxy);
  if (!endpoint_data)
    return;

  /* Set the name if it is null */
  name = endpoint_data->e.name;
  if (!name)
    name = wp_properties_get (props, PW_KEY_NODE_NAME);

  /* Set the media class if it is null */
  media_class = endpoint_data->e.media_class;
  if (!media_class)
    media_class = wp_properties_get (props, PW_KEY_MEDIA_CLASS);

  /* Create the streams variant */
  streams_variant = create_streams_variant (config, endpoint_data->e.streams);

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "name", g_variant_new_take_string (g_strdup_printf ("%s", name)));
  g_variant_builder_add (&b, "{sv}",
      "media-class", g_variant_new_string (media_class));
  g_variant_builder_add (&b, "{sv}",
      "direction", g_variant_new_uint32 (endpoint_data->e.direction));
  g_variant_builder_add (&b, "{sv}",
      "priority", g_variant_new_uint32 (endpoint_data->e.priority));
  g_variant_builder_add (&b, "{sv}",
      "node", g_variant_new_uint64 ((guint64) proxy));
  if (streams_variant)
    g_variant_builder_add (&b, "{sv}", "streams",
        g_steal_pointer (&streams_variant));
  endpoint_props = g_variant_builder_end (&b);

  /* Create the endpoint async */
  wp_factory_make (core, endpoint_data->e.type, WP_TYPE_BASE_ENDPOINT,
      endpoint_props, on_endpoint_created, self);
}

static void
on_node_removed (WpObjectManager *om, WpProxy *proxy, gpointer d)
{
  WpConfigEndpointContext *self = d;
  WpBaseEndpoint *endpoint = NULL;
  guint32 id = wp_proxy_get_bound_id (proxy);

  /* Get the endpoint */
  endpoint = g_hash_table_lookup (self->registered_endpoints,
      GUINT_TO_POINTER(id));
  if (!endpoint)
    return;

  /* Unregister the endpoint and remove it from the table */
  wp_base_endpoint_unregister (endpoint);
  g_hash_table_remove (self->registered_endpoints, GUINT_TO_POINTER(id));
}

static void
wp_config_endpoint_context_constructed (GObject * object)
{
  WpConfigEndpointContext *self = WP_CONFIG_ENDPOINT_CONTEXT (object);
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_return_if_fail (core);
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  g_return_if_fail (config);

  /* Add the endpoint and streams parsers */
  wp_configuration_add_extension (config, WP_PARSER_ENDPOINT_EXTENSION,
      WP_TYPE_PARSER_ENDPOINT);
  wp_configuration_add_extension (config, WP_PARSER_STREAMS_EXTENSION,
      WP_TYPE_PARSER_STREAMS);

  /* Parse the files */
  wp_configuration_reload (config, WP_PARSER_ENDPOINT_EXTENSION);
  wp_configuration_reload (config, WP_PARSER_STREAMS_EXTENSION);

  /* Install the object manager */
  wp_core_install_object_manager (core, self->om);

  G_OBJECT_CLASS (wp_config_endpoint_context_parent_class)->constructed (object);
}

static void
wp_config_endpoint_context_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpConfigEndpointContext *self = WP_CONFIG_ENDPOINT_CONTEXT (object);

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&self->core, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_config_endpoint_context_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpConfigEndpointContext *self = WP_CONFIG_ENDPOINT_CONTEXT (object);

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&self->core));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_config_endpoint_context_finalize (GObject *object)
{
  WpConfigEndpointContext *self = WP_CONFIG_ENDPOINT_CONTEXT (object);

  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  if (core) {
    g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
    wp_configuration_remove_extension (config, WP_PARSER_ENDPOINT_EXTENSION);
    wp_configuration_remove_extension (config, WP_PARSER_STREAMS_EXTENSION);
  }
  g_weak_ref_clear (&self->core);

  g_clear_object (&self->om);
  g_clear_pointer (&self->registered_endpoints, g_hash_table_unref);

  G_OBJECT_CLASS (wp_config_endpoint_context_parent_class)->finalize (object);
}

static void
wp_config_endpoint_context_init (WpConfigEndpointContext *self)
{
  self->om = wp_object_manager_new ();
  self->registered_endpoints = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) g_object_unref);

  /* Only handle augmented nodes with info set */
  wp_object_manager_add_proxy_interest (self->om, PW_TYPE_INTERFACE_Node, NULL,
      WP_PROXY_FEATURE_INFO | WP_PROXY_FEATURE_BOUND);

  /* Register the global added/removed callbacks */
  g_signal_connect(self->om, "object-added",
      (GCallback) on_node_added, self);
  g_signal_connect(self->om, "object-removed",
      (GCallback) on_node_removed, self);
}

static void
wp_config_endpoint_context_class_init (WpConfigEndpointContextClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_config_endpoint_context_constructed;
  object_class->finalize = wp_config_endpoint_context_finalize;
  object_class->set_property = wp_config_endpoint_context_set_property;
  object_class->get_property = wp_config_endpoint_context_get_property;

  /* Properties */
  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The wireplumber core",
          WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  /* Signals */
  signals[SIGNAL_ENDPOINT_CREATED] = g_signal_new ("endpoint-created",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_ENDPOINT);
}

WpConfigEndpointContext *
wp_config_endpoint_context_new (WpCore *core)
{
  return g_object_new (wp_config_endpoint_context_get_type (),
    "core", core,
    NULL);
}
