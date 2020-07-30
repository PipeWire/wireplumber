/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include <pipewire/pipewire.h>

#include "generic-creation.h"
#include "parser-endpoint.h"
#include "parser-streams.h"

G_DEFINE_QUARK (wp-module-config-endpoint-context-session, session);
G_DEFINE_QUARK (wp-module-config-endpoint-context-monitor, monitor);
G_DEFINE_QUARK (wp-module-config-endpoint-context-endpoint, endpoint);

struct _WpGenericCreation
{
  GObject parent;

  /* properties */
  GWeakRef core;

  GPtrArray *nodes;
  WpObjectManager *sessions_om;
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

G_DEFINE_TYPE (WpGenericCreation, wp_generic_creation, G_TYPE_OBJECT)

static const struct WpParserStreamsData *
get_streams_data (WpConfiguration *config, const char *file_name)
{
  g_autoptr (WpConfigParser) parser = NULL;

  g_return_val_if_fail (config, 0);
  g_return_val_if_fail (file_name, 0);

  /* Get the streams parser */
  parser = wp_configuration_get_parser (config, WP_PARSER_STREAMS_EXTENSION);
  if (!parser)
    return 0;

  /* Get the streams data */
  return wp_config_parser_get_matched_data (parser, (gpointer)file_name);
}

static void endpoint_activate_finish_cb (WpSessionItem * ep, GAsyncResult * res,
    WpGenericCreation * self);

static void
endpoint_export_finish_cb (WpSessionItem * ep, GAsyncResult * res,
    WpGenericCreation * self)
{
  g_autoptr (GObject) node = NULL;
  WpSessionItem * monitor = NULL;
  g_autoptr (GError) error = NULL;

  if (!wp_session_item_export_finish (ep, res, &error)) {
    wp_warning_object (self, "failed to export endpoint: %s", error->message);
    return;
  }

  /* Activate monitor, if there is one and if ep is not the monitor itself */
  node = wp_session_item_get_associated_proxy (ep, WP_TYPE_NODE);
  if (node)
    monitor = g_object_get_qdata (node, monitor_quark ());
  if (monitor && ep != monitor)
    wp_session_item_activate (monitor,
        (GAsyncReadyCallback) endpoint_activate_finish_cb, self);

  /* Emit the endpoint created signal */
  g_signal_emit (self, signals[SIGNAL_ENDPOINT_CREATED], 0, ep);
}

static void
endpoint_activate_finish_cb (WpSessionItem * ep, GAsyncResult * res,
    WpGenericCreation * self)
{
  WpSession * session = NULL;
  g_autoptr (GError) error = NULL;
  gboolean activate_ret = wp_session_item_activate_finish (ep, res, &error);
  if (!activate_ret) {
    wp_warning_object (self, "failed to activate endpoint: %s", error->message);
    return;
  }

  /* Get the session */
  session = g_object_get_qdata (G_OBJECT (ep), session_quark ());
  g_return_if_fail (session);

  wp_session_item_export (ep, WP_SESSION (session),
      (GAsyncReadyCallback) endpoint_export_finish_cb, self);
}

static void
wp_generic_creation_constructed (GObject *object)
{
  WpGenericCreation *self = WP_GENERIC_CREATION (object);
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);

  /* Load the configuration files */
  wp_configuration_add_extension (config, WP_PARSER_ENDPOINT_EXTENSION,
      WP_TYPE_PARSER_ENDPOINT);
  wp_configuration_add_extension (config, WP_PARSER_STREAMS_EXTENSION,
      WP_TYPE_PARSER_STREAMS);
  wp_configuration_reload (config, WP_PARSER_ENDPOINT_EXTENSION);
  wp_configuration_reload (config, WP_PARSER_STREAMS_EXTENSION);

  /* Create the sessions object manager */
  self->sessions_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->sessions_om, WP_TYPE_SESSION, NULL);
  wp_object_manager_request_proxy_features (self->sessions_om, WP_TYPE_SESSION,
      WP_SESSION_FEATURES_STANDARD);
  wp_core_install_object_manager (core, self->sessions_om);

  G_OBJECT_CLASS (wp_generic_creation_parent_class)->constructed (object);
}

static void
wp_generic_creation_finalize (GObject * object)
{
  WpGenericCreation *self = WP_GENERIC_CREATION (object);
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);

  /* Unload the configuration files */
  if (core) {
    g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
    wp_configuration_remove_extension (config, WP_PARSER_ENDPOINT_EXTENSION);
    wp_configuration_remove_extension (config, WP_PARSER_STREAMS_EXTENSION);
  }

  g_clear_pointer (&self->nodes, g_ptr_array_unref);
  g_clear_object (&self->sessions_om);

  G_OBJECT_CLASS (wp_generic_creation_parent_class)->finalize (object);
}

static void
wp_generic_creation_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpGenericCreation *self = WP_GENERIC_CREATION (object);

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
wp_generic_creation_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpGenericCreation *self = WP_GENERIC_CREATION (object);

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
free_node (gpointer node)
{
  /* Remove the endpoint and its monitor, if any */
  g_object_set_qdata (G_OBJECT (node), monitor_quark (), NULL);
  g_object_set_qdata (G_OBJECT (node), endpoint_quark (), NULL);

  g_object_unref (G_OBJECT (node));
}

static void
wp_generic_creation_init (WpGenericCreation *self)
{
  self->nodes = g_ptr_array_new_with_free_func (free_node);
}

static void
wp_generic_creation_class_init (WpGenericCreationClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_generic_creation_constructed;
  object_class->finalize = wp_generic_creation_finalize;
  object_class->set_property = wp_generic_creation_set_property;
  object_class->get_property = wp_generic_creation_get_property;

  /* properties */
  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The WpCore", WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  /* Signals */
  signals[SIGNAL_ENDPOINT_CREATED] = g_signal_new ("endpoint-created",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_SESSION_ITEM);
}

WpGenericCreation *
wp_generic_creation_new (WpCore *core)
{
  return g_object_new (wp_generic_creation_get_type (),
      "core", core,
      NULL);
}

void
wp_generic_creation_add_node (WpGenericCreation * self, WpNode *node)
{
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  g_autoptr (WpProperties) props = wp_proxy_get_properties (WP_PROXY (node));
  g_autoptr (WpSessionItem) ep = NULL;
  g_autoptr (WpSessionItem) streams_ep = NULL;
  g_autoptr (WpSession) session = NULL;
  g_autoptr (WpConfigParser) parser = NULL;
  const struct WpParserEndpointData *endpoint_data = NULL;
  const struct WpParserStreamsData *streams_data = NULL;
  WpDirection direction = WP_DIRECTION_INPUT;

  /* Skip nodes with no media class (JACK Clients) */
  if (!wp_properties_get (props, PW_KEY_MEDIA_CLASS))
    return;

  /* Get the endpoint configuration data */
  parser = wp_configuration_get_parser (config, WP_PARSER_ENDPOINT_EXTENSION);
  endpoint_data = wp_config_parser_get_matched_data (parser, node);
  if (!endpoint_data)
    return;

  wp_info_object (self, "node %u " WP_OBJECT_FORMAT " matches %s",
      wp_proxy_get_bound_id (WP_PROXY (node)), WP_OBJECT_ARGS (node),
      endpoint_data->filename);

  /* Get the session */
  session = wp_object_manager_lookup (self->sessions_om, WP_TYPE_SESSION,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "session.name", "=s",
      endpoint_data->e.session, NULL);
  if (!session) {
    wp_warning_object (self, "could not find session for endpoint");
    return;
  }

  /* Get the streams data */
  streams_data = endpoint_data->e.streams ?
      get_streams_data (config, endpoint_data->e.streams) : NULL;

  /* Create the endpoint */
  ep = wp_session_item_make (core, endpoint_data->e.type);
  if (!ep) {
    wp_warning_object (self, "could not create endpoint of type %s",
        endpoint_data->e.type);
    return;
  }

  /* Configure the endpoint */
  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "node",
      g_variant_new_uint64 ((guint64) node));

    if (endpoint_data->e.c.name)
      g_variant_builder_add (&b, "{sv}", "name",
          g_variant_new_string (endpoint_data->e.c.name));

    if (endpoint_data->e.c.media_class)
      g_variant_builder_add (&b, "{sv}", "media-class",
          g_variant_new_string (endpoint_data->e.c.media_class));

    if (endpoint_data->e.c.role)
      g_variant_builder_add (&b, "{sv}", "role",
          g_variant_new_string (endpoint_data->e.c.role));

    g_variant_builder_add (&b, "{sv}", "priority",
        g_variant_new_uint32 (endpoint_data->e.c.priority));

    g_variant_builder_add (&b, "{sv}", "enable-control-port",
        g_variant_new_boolean (endpoint_data->e.c.enable_control_port));

    g_variant_builder_add (&b, "{sv}", "enable-monitor",
        g_variant_new_boolean (endpoint_data->e.c.enable_monitor));

    g_variant_builder_add (&b, "{sv}", "preferred-n-channels",
        g_variant_new_uint32 (endpoint_data->e.c.preferred_n_channels));

    wp_session_item_configure (ep, g_variant_builder_end (&b));
  }

  /* Get the endpoint direction */
  {
    g_autoptr (GVariant) ep_config = wp_session_item_get_configuration (ep);
    if (!g_variant_lookup (ep_config, "direction", "y", &direction)) {
      wp_warning_object (self, "could not get endpoint direction");
      return;
    }
  }

  /* TODO: for now we always create softdsp audio endpoints if streams data is
   * valid. However, this will need to change once we have video endpoints. */
  if (streams_data) {
    /* Create the steams endpoint */
    streams_ep = wp_session_item_make (core, "si-audio-softdsp-endpoint");

    /* Configure the streams endpoint */
    {
      g_auto (GVariantBuilder) b =
          G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add (&b, "{sv}", "adapter",
          g_variant_new_uint64 ((guint64) ep));
      wp_session_item_configure (streams_ep, g_variant_builder_end (&b));
    }

    /* Add the streams */
    for (guint i = 0; i < streams_data->n_streams; i++) {
      const struct WpParserStreamsStreamData *sd = streams_data->streams + i;
      g_autoptr (WpSessionItem) stream =
          wp_session_item_make (core, "si-convert");
      {
        g_auto (GVariantBuilder) b =
            G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add (&b, "{sv}", "target",
            g_variant_new_uint64 ((guint64) ep));
        g_variant_builder_add (&b, "{sv}", "name",
            g_variant_new_string (sd->name));
        g_variant_builder_add (&b, "{sv}", "enable-control-port",
            g_variant_new_boolean (sd->enable_control_port));
        wp_session_item_configure (stream, g_variant_builder_end (&b));
      }

      wp_session_bin_add (WP_SESSION_BIN (streams_ep), g_steal_pointer (&stream));
    }
  }

  /* Create monitor endpoint if input direction and enable_monitor is true */
  if (endpoint_data->e.c.enable_monitor && direction == WP_DIRECTION_INPUT) {
    g_autoptr (WpSessionItem) monitor_ep =
        wp_session_item_make (core, "si-monitor-endpoint");

    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_return_if_fail (ep);
    g_return_if_fail (monitor_ep);
    g_variant_builder_add (&b, "{sv}", "adapter",
        g_variant_new_uint64 ((guint64) ep));
    wp_session_item_configure (monitor_ep, g_variant_builder_end (&b));

    /* Set session */
    g_object_set_qdata_full (G_OBJECT (monitor_ep), session_quark (),
        g_object_ref (session), g_object_unref);

    /* Keep a reference in the node */
    g_object_set_qdata_full (G_OBJECT (node), monitor_quark (),
        g_steal_pointer (&monitor_ep), g_object_unref);
  }

  /* Set session */
  g_object_set_qdata_full (
      G_OBJECT (streams_data ? streams_ep : ep), session_quark (),
      g_steal_pointer (&session), g_object_unref);

  /* Activate endpoint */
  wp_session_item_activate (streams_data ? streams_ep : ep,
      (GAsyncReadyCallback) endpoint_activate_finish_cb, self);

  /* Insert the endpoint */
  g_object_set_qdata_full (G_OBJECT (node), endpoint_quark (),
      streams_data ? g_steal_pointer (&streams_ep) : g_steal_pointer (&ep),
      g_object_unref);

  /* Add the node in the array */
  g_ptr_array_add (self->nodes, g_object_ref (node));
}

void
wp_generic_creation_remove_node (WpGenericCreation * self, WpNode *node)
{
  g_ptr_array_remove (self->nodes, node);
}
