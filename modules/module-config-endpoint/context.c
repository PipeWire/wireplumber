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

G_DEFINE_QUARK (wp-module-config-endpoint-context-session, session);

struct _WpConfigEndpointContext
{
  GObject parent;

  WpObjectManager *sessions_om;
  WpObjectManager *nodes_om;
  GHashTable *endpoints;
};

enum {
  SIGNAL_ENDPOINT_CREATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (WpConfigEndpointContext, wp_config_endpoint_context,
    WP_TYPE_PLUGIN)

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

static void
endpoint_export_finish_cb (WpSessionItem * ep, GAsyncResult * res,
    WpConfigEndpointContext * self)
{
  g_autoptr (GError) error = NULL;
  gboolean export_ret = wp_session_item_export_finish (ep, res, &error);
  g_return_if_fail (error == NULL);
  g_return_if_fail (export_ret);

  /* Emit the signal */
  g_signal_emit (self, signals[SIGNAL_ENDPOINT_CREATED], 0, ep);
}

static void
endpoint_activate_finish_cb (WpSessionItem * ep, GAsyncResult * res,
    WpConfigEndpointContext * self)
{
  WpSession * session = NULL;
  g_autoptr (GError) error = NULL;
  gboolean activate_ret = wp_session_item_activate_finish (ep, res, &error);
  g_return_if_fail (error == NULL);
  g_return_if_fail (activate_ret);

  /* Get the session */
  session = g_object_get_qdata (G_OBJECT (ep), session_quark ());
  g_return_if_fail (session);

  wp_session_item_export (ep, WP_SESSION (session),
      (GAsyncReadyCallback) endpoint_export_finish_cb, self);
}

static void
on_node_added (WpObjectManager *om, WpProxy *proxy, gpointer d)
{
  WpConfigEndpointContext *self = d;
  g_autoptr (WpCore) core = wp_plugin_get_core (WP_PLUGIN (self));
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  g_autoptr (WpProperties) props = wp_proxy_get_properties (proxy);
  g_autoptr (WpSessionItem) ep = NULL;
  g_autoptr (WpSessionItem) streams_ep = NULL;
  g_autoptr (WpSession) session = NULL;
  g_autoptr (WpConfigParser) parser = NULL;
  const struct WpParserEndpointData *endpoint_data = NULL;
  const struct WpParserStreamsData *streams_data = NULL;

  /* Skip nodes with no media class (JACK Clients) */
  if (!wp_properties_get (props, PW_KEY_MEDIA_CLASS))
    return;

  /* Get the endpoint configuration data */
  parser = wp_configuration_get_parser (config, WP_PARSER_ENDPOINT_EXTENSION);
  endpoint_data = wp_config_parser_get_matched_data (parser, proxy);
  if (!endpoint_data)
    return;

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
      g_variant_new_uint64 ((guint64) proxy));

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

    wp_session_item_configure (ep, g_variant_builder_end (&b));
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

  /* Activate endpoint */
  g_object_set_qdata_full (
      G_OBJECT (streams_data ? streams_ep : ep), session_quark (),
      g_steal_pointer (&session), g_object_unref);
  wp_session_item_activate (streams_data ? streams_ep : ep,
      (GAsyncReadyCallback) endpoint_activate_finish_cb, self);

  /* Insert the endpoint */
  g_hash_table_insert (self->endpoints, proxy,
      streams_data ? g_steal_pointer (&streams_ep) : g_steal_pointer (&ep));
}

static void
wp_config_endpoint_context_activate (WpPlugin * plugin)
{
  WpConfigEndpointContext *self = WP_CONFIG_ENDPOINT_CONTEXT (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
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

  self->endpoints = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_object_unref);

  /* Install the session object manager */
  self->sessions_om = wp_object_manager_new ();
  wp_object_manager_add_interest_1 (self->sessions_om, WP_TYPE_SESSION, NULL);
  wp_object_manager_request_proxy_features (self->sessions_om, WP_TYPE_SESSION,
      WP_SESSION_FEATURES_STANDARD);
  wp_core_install_object_manager (core, self->sessions_om);

  /* Handle node-added signal and install the nodes object manager */
  self->nodes_om = wp_object_manager_new ();
  wp_object_manager_add_interest_1 (self->nodes_om, WP_TYPE_NODE, NULL);
  wp_object_manager_request_proxy_features (self->nodes_om, WP_TYPE_NODE,
      WP_PROXY_FEATURES_STANDARD);
  g_signal_connect_object (self->nodes_om, "object-added",
      G_CALLBACK (on_node_added), self, 0);
  wp_core_install_object_manager (core, self->nodes_om);
}

static void
wp_config_endpoint_context_deactivate (WpPlugin *plugin)
{
  WpConfigEndpointContext *self = WP_CONFIG_ENDPOINT_CONTEXT (plugin);

  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
  if (core) {
    g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
    wp_configuration_remove_extension (config, WP_PARSER_ENDPOINT_EXTENSION);
    wp_configuration_remove_extension (config, WP_PARSER_STREAMS_EXTENSION);
  }

  g_clear_pointer (&self->endpoints, g_hash_table_unref);
  g_clear_object (&self->sessions_om);
  g_clear_object (&self->nodes_om);
}

static void
wp_config_endpoint_context_init (WpConfigEndpointContext *self)
{
}

static void
wp_config_endpoint_context_class_init (WpConfigEndpointContextClass *klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->activate = wp_config_endpoint_context_activate;
  plugin_class->deactivate = wp_config_endpoint_context_deactivate;

  /* Signals */
  signals[SIGNAL_ENDPOINT_CREATED] = g_signal_new ("endpoint-created",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_SESSION_ITEM);
}

WpConfigEndpointContext *
wp_config_endpoint_context_new (WpModule * module)
{
  return g_object_new (wp_config_endpoint_context_get_type (),
      "module", module,
      NULL);
}
