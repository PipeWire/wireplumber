/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager.h>

#include <wp/wp.h>

#include "parser-endpoint-link.h"
#include "context.h"

struct _WpConfigPolicyContext
{
  GObject parent;

  WpObjectManager *sessions_om;
};

enum {
  SIGNAL_LINK_CREATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (WpConfigPolicyContext, wp_config_policy_context,
    WP_TYPE_PLUGIN)

static WpEndpoint *
wp_config_policy_context_get_endpoint_target (WpConfigPolicyContext *self,
    WpSession *session, WpEndpoint *ep, guint32 *stream_id)
{
  g_autoptr (WpProperties) ep_props = wp_proxy_get_properties (WP_PROXY (ep));
  const gchar *node_target = NULL;
  g_autoptr (WpEndpoint) target = NULL;
  const gchar *stream_name = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  g_return_val_if_fail (session, NULL);
  g_return_val_if_fail (ep, NULL);

  /* Check if the node target property is set, and use that target */
  node_target = wp_properties_get (ep_props, PW_KEY_NODE_TARGET);
  if (node_target) {
    WpDirection target_dir =
        wp_endpoint_get_direction (ep) == WP_DIRECTION_INPUT ?
            WP_DIRECTION_OUTPUT : WP_DIRECTION_INPUT;
    for (it = wp_session_iterate_endpoints (session);
           wp_iterator_next (it, &val);
           g_value_unset (&val)) {
      WpEndpoint *candidate = g_value_get_object (&val);
      g_autoptr (WpProperties) candidate_props =
          wp_proxy_get_properties (WP_PROXY (candidate));
      const gchar *candidate_node_id = wp_properties_get (candidate_props,
          PW_KEY_NODE_ID);
      if (wp_endpoint_get_direction (candidate) == target_dir &&
          candidate_node_id && atoi (candidate_node_id) == atoi (node_target)) {
        target = g_object_ref (candidate);
        g_value_unset (&val);
        break;
      }
    }
  }

  /* Otherwise, check the endpoint-link configuration files */
  else {
    g_autoptr (WpCore) core = wp_plugin_get_core (WP_PLUGIN (self));
    g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
    g_autoptr (WpConfigParser) parser = wp_configuration_get_parser (config,
        WP_PARSER_ENDPOINT_LINK_EXTENSION);
    const struct WpParserEndpointLinkData *data =
        wp_config_parser_get_matched_data (parser, G_OBJECT (ep));
    if (!data)
      return NULL;

    /* If target-endpoint data was defined in the configuration file, find the
     * matching endpoint based on target-endpoint data */
    if (data->has_te) {
      guint highest_prio = 0;
      for (it = wp_session_iterate_endpoints (session);
           wp_iterator_next (it, &val);
           g_value_unset (&val)) {
        WpEndpoint *candidate = g_value_get_object (&val);
        if (wp_parser_endpoint_link_matches_endpoint_data (candidate,
            &data->te.endpoint_data)) {
          g_autoptr (WpProperties) props =
              wp_proxy_get_properties (WP_PROXY (candidate));
          const char *priority = wp_properties_get (props, "endpoint.priority");
          const guint prio = atoi (priority);
          if (highest_prio <= prio) {
            highest_prio = prio;
            target = g_object_ref (candidate);
          }
        }
      }
    }

    /* Otherwise, use the default session endpoint */
    else {
      guint def_id = 0;
      switch (data->me.endpoint_data.direction) {
        case WP_DIRECTION_INPUT:
          def_id = wp_session_get_default_endpoint (session, "Wp:defaultSource");
          break;
        case WP_DIRECTION_OUTPUT:
          def_id = wp_session_get_default_endpoint (session, "Wp:defaultSink");
          break;
        default:
          g_return_val_if_reached (NULL);
      }
      target = wp_session_lookup_endpoint (session,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", def_id, NULL);
    }

    /* Use the stream name from the configuration file */
    stream_name = data->te.stream;
  }

  if (!target)
    return NULL;

  /* Find the stream that matches the data name, otherwise use the first one */
  if (stream_id) {
    g_autoptr (WpIterator) stream_it = wp_endpoint_iterate_streams (target);
    g_auto (GValue) stream_val = G_VALUE_INIT;
    *stream_id = SPA_ID_INVALID;
    while (wp_iterator_next (stream_it, &stream_val)) {
      WpProxy *stream = g_value_get_object (&stream_val);
      if (g_strcmp0 (wp_endpoint_stream_get_name (WP_ENDPOINT_STREAM (stream)),
          stream_name) == 0) {
        *stream_id = wp_proxy_get_bound_id (stream);
        break;
      }
      if (*stream_id == SPA_ID_INVALID)
        *stream_id = wp_proxy_get_bound_id (stream);

      g_value_unset (&stream_val);
    }
  }

  return g_steal_pointer (&target);
}

static WpEndpointLink *
wp_config_policy_context_get_endpoint_link (WpConfigPolicyContext *self,
    WpSession *session, WpEndpoint *ep, WpEndpoint *target)
{
  WpEndpointLink *link = NULL;
  const guint32 ep_id = wp_proxy_get_bound_id (WP_PROXY (ep));
  const guint32 target_id = wp_proxy_get_bound_id (WP_PROXY (target));

  switch (wp_endpoint_get_direction (ep)) {
  case WP_DIRECTION_INPUT:
    /* Capture */
    link = wp_session_lookup_link (session,
        WP_CONSTRAINT_TYPE_PW_PROPERTY,
        PW_KEY_ENDPOINT_LINK_INPUT_ENDPOINT, "=u", ep_id,
        WP_CONSTRAINT_TYPE_PW_PROPERTY,
        PW_KEY_ENDPOINT_LINK_OUTPUT_ENDPOINT, "=u", target_id, NULL);
    break;
  case WP_DIRECTION_OUTPUT:
    /* Playback */
    link = wp_session_lookup_link (session,
        WP_CONSTRAINT_TYPE_PW_PROPERTY,
        PW_KEY_ENDPOINT_LINK_OUTPUT_ENDPOINT, "=u", ep_id,
        WP_CONSTRAINT_TYPE_PW_PROPERTY,
        PW_KEY_ENDPOINT_LINK_INPUT_ENDPOINT, "=u", target_id, NULL);
    break;
  default:
    g_return_val_if_reached (NULL);
  }

  return link;
}

static void
wp_config_policy_context_link_endpoint (WpConfigPolicyContext *self,
    WpEndpoint *ep, WpEndpoint *target, guint32 target_stream_id)
{
  g_autoptr (WpProperties) props = NULL;
  const guint32 target_id = wp_proxy_get_bound_id (WP_PROXY (target));

  /* Create the link properties */
  props = wp_properties_new_empty ();
  switch (wp_endpoint_get_direction (ep)) {
  case WP_DIRECTION_INPUT:
    /* Capture */
    wp_properties_setf (props, PW_KEY_ENDPOINT_LINK_OUTPUT_ENDPOINT, "%u", target_id);
    wp_properties_setf (props, PW_KEY_ENDPOINT_LINK_OUTPUT_STREAM, "%u", target_stream_id);
    break;
  case WP_DIRECTION_OUTPUT:
    /* Playback */
    wp_properties_setf (props, PW_KEY_ENDPOINT_LINK_INPUT_ENDPOINT, "%u", target_id);
    wp_properties_setf (props, PW_KEY_ENDPOINT_LINK_INPUT_STREAM, "%u", target_stream_id);
    break;
  default:
    g_return_if_reached ();
  }

  /* Create the link */
  wp_endpoint_create_link (ep, props);
}

static void
wp_config_policy_context_handle_endpoint (WpConfigPolicyContext *self,
    WpSession *session, WpEndpoint *ep)
{
  g_autoptr (WpEndpoint) target = NULL;
  g_autoptr (WpEndpointLink) link = NULL;
  guint32 target_stream_id = SPA_ID_INVALID;

  /* Get the endpoint target */
  target = wp_config_policy_context_get_endpoint_target (self, session, ep,
      &target_stream_id);
  if (!target)
    return;

  /* Return if the endpoint is already linked with that target */
  link = wp_config_policy_context_get_endpoint_link (self, session, ep, target);
  if (link)
    return;

  /* Link endpoint with target */
  wp_config_policy_context_link_endpoint (self, ep, target, target_stream_id);
}

static void
on_session_endpoints_changed (WpSession *session, WpConfigPolicyContext *self)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  wp_debug ("endpoints changed");

  /* Handle all endpoints */
  it = wp_session_iterate_endpoints (session);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpEndpoint *ep = g_value_get_object (&val);
    wp_config_policy_context_handle_endpoint (self, session, ep);
  }
}

static void
on_session_links_changed (WpSession *session, WpConfigPolicyContext *self)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  const gchar *error = NULL;

  /* Always activate all inactive links */
  it = wp_session_iterate_links (session);
  while (wp_iterator_next (it, &val)) {
    WpEndpointLink *ep_link = g_value_get_object (&val);
    if (wp_endpoint_link_get_state (ep_link, &error) ==
        WP_ENDPOINT_LINK_STATE_INACTIVE) {
      wp_endpoint_link_request_state (ep_link, WP_ENDPOINT_LINK_STATE_ACTIVE);

      /* Emit the link created signal */
      g_signal_emit (self, signals[SIGNAL_LINK_CREATED], 0, ep_link);
    }
    g_value_unset (&val);
  }
}

static void
on_session_added (WpObjectManager *om, WpProxy *proxy,
    WpConfigPolicyContext *self)
{
  /* Handle links-changed and endpoints-changed callbacks on all sessions*/
  g_signal_connect_object (proxy, "endpoints-changed",
      G_CALLBACK (on_session_endpoints_changed), self, 0);
  g_signal_connect_object (proxy, "links-changed",
      G_CALLBACK (on_session_links_changed), self, 0);
}

static void
wp_config_policy_context_activate (WpPlugin * plugin)
{
  WpConfigPolicyContext *self = WP_CONFIG_POLICY_CONTEXT (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
  g_return_if_fail (core);
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  g_return_if_fail (config);

  /* Add the endpoint-link parser */
  wp_configuration_add_extension (config, WP_PARSER_ENDPOINT_LINK_EXTENSION,
      WP_TYPE_PARSER_ENDPOINT_LINK);

  /* Parse the files */
  wp_configuration_reload (config, WP_PARSER_ENDPOINT_LINK_EXTENSION);

  /* Install the session object manager */
  self->sessions_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->sessions_om, WP_TYPE_SESSION, NULL);
  wp_object_manager_request_proxy_features (self->sessions_om, WP_TYPE_SESSION,
      WP_SESSION_FEATURES_STANDARD);
  g_signal_connect_object (self->sessions_om, "object-added",
      G_CALLBACK (on_session_added), self, 0);
  wp_core_install_object_manager (core, self->sessions_om);
}

static void
wp_config_policy_context_deactivate (WpPlugin *plugin)
{
  WpConfigPolicyContext *self = WP_CONFIG_POLICY_CONTEXT (plugin);

  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
  if (core) {
    g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
    wp_configuration_remove_extension (config, WP_PARSER_ENDPOINT_LINK_EXTENSION);
  }

  g_clear_object (&self->sessions_om);
}

static void
wp_config_policy_context_init (WpConfigPolicyContext *self)
{
}

static void
wp_config_policy_context_class_init (WpConfigPolicyContextClass *klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->activate = wp_config_policy_context_activate;
  plugin_class->deactivate = wp_config_policy_context_deactivate;

  /* Signals */
  signals[SIGNAL_LINK_CREATED] = g_signal_new ("link-created",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_ENDPOINT_LINK);
}

WpConfigPolicyContext *
wp_config_policy_context_new (WpModule * module)
{
  return g_object_new (wp_config_policy_context_get_type (),
      "module", module,
      NULL);
}
