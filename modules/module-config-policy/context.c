/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>

#include <wp/wp.h>

#include "parser-endpoint-link.h"
#include "context.h"

struct link_info {
  WpEndpoint *ep;
  guint32 stream_id;
  gboolean keep;
};

static void
link_info_destroy (gpointer p)
{
  struct link_info *li = p;
  g_return_if_fail (li);
  g_clear_object (&li->ep);
  g_slice_free (struct link_info, li);
}

struct _WpConfigPolicyContext
{
  GObject parent;

  WpObjectManager *sessions_om;
};

enum {
  SIGNAL_LINK_ACTIVATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (WpConfigPolicyContext, wp_config_policy_context,
    WP_TYPE_PLUGIN)

static WpEndpoint *
wp_config_policy_context_get_data_target (WpConfigPolicyContext *self,
    WpSession *session, const struct WpParserEndpointLinkData *data,
    guint32 *stream_id)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  WpEndpoint *target = NULL;

  g_return_val_if_fail (session, NULL);

  it = wp_session_iterate_endpoints (session);

  /* If target-endpoint data was defined in the configuration file, find the
   * matching endpoint based on target-endpoint data */
  if (data->has_te) {
    guint highest_prio = 0;
    while (wp_iterator_next (it, &val)) {
      WpEndpoint *ep = g_value_get_object (&val);
      if (wp_parser_endpoint_link_matches_endpoint_data (ep,
          &data->te.endpoint_data)) {
        g_autoptr (WpProperties) props = wp_proxy_get_properties (WP_PROXY (ep));
        const char *priority = wp_properties_get (props, "endpoint.priority");
        const guint prio = atoi (priority);
        if (highest_prio <= prio) {
          highest_prio = prio;
          target = ep;
        }
      }
      g_value_unset (&val);
    }
  }

  /* Otherwise, use the default session endpoint */
  else {
    const char *type_name;
    switch (data->me.endpoint_data.direction) {
      case WP_DIRECTION_INPUT:
        type_name = "wp-session-default-endpoint-audio-source";
        break;
      case WP_DIRECTION_OUTPUT:
        type_name = "wp-session-default-endpoint-audio-sink";
        break;
      default:
        g_warn_if_reached ();
        return NULL;
    }
    while (wp_iterator_next (it, &val)) {
      WpEndpoint *ep = g_value_get_object (&val);
      guint def_id = wp_session_get_default_endpoint (session, type_name);
      if (def_id == wp_proxy_get_bound_id (WP_PROXY (ep))) {
        target = ep;
        break;
      }
      g_value_unset (&val);
    }
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
          data->te.stream) == 0) {
        *stream_id = wp_proxy_get_bound_id (stream);
        break;
      }
      if (*stream_id == SPA_ID_INVALID)
        *stream_id = wp_proxy_get_bound_id (stream);

      g_value_unset (&stream_val);
    }
  }

  return g_object_ref (target);
}

static void
wp_config_policy_context_add_link_info (WpConfigPolicyContext *self,
    GHashTable *table, WpSession *session, WpEndpoint *ep)
{
  g_autoptr (WpCore) core = wp_plugin_get_core (WP_PLUGIN (self));
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  g_autoptr (WpConfigParser) parser = NULL;
  const struct WpParserEndpointLinkData *data = NULL;
  struct link_info *res = NULL;
  g_autoptr (WpEndpoint) target = NULL;
  guint32 stream_id = SPA_ID_INVALID;

  /* Get the parser for the endpoint-link extension */
  parser = wp_configuration_get_parser (config,
      WP_PARSER_ENDPOINT_LINK_EXTENSION);

  /* Get the matched endpoint data from the parser */
  data = wp_config_parser_get_matched_data (parser, G_OBJECT (ep));
  if (!data)
    return;

  /* Get the target */
  target = wp_config_policy_context_get_data_target (self, session, data,
      &stream_id);
  if (!target)
    return;

  /* Create the link info */
  res = g_slice_new0 (struct link_info);
  res->ep = g_object_ref (ep);
  res->stream_id = stream_id;
  res->keep = data->el.keep;

  /* Get the link infos for the target, or create a new one if not found */
  GPtrArray *link_infos = g_hash_table_lookup (table, target);
  if (!link_infos) {
    link_infos = g_ptr_array_new_with_free_func (link_info_destroy);
    g_ptr_array_add (link_infos, res);
    g_hash_table_insert (table, g_steal_pointer (&target), link_infos);
  } else {
    g_ptr_array_add (link_infos, res);
  }
}

static gint
link_info_compare_func (gconstpointer a, gconstpointer b, gpointer data)
{
  struct link_info *li_a = *(struct link_info *const *)a;
  struct link_info *li_b = *(struct link_info *const *)b;
  g_autoptr (WpProperties) props_a = NULL;
  g_autoptr (WpProperties) props_b = NULL;
  guint prio_a = 0, prio_b = 0;
  const char *str = NULL;
  gint res = 0;

  res = (gint) li_a->keep - (gint) li_b->keep;
  if (res != 0)
    return res;

  /* Sort from highest priority to lowest */
  props_a = wp_proxy_get_properties (WP_PROXY (li_a->ep));
  str = wp_properties_get (props_a, "endpoint.priority");
  if (str)
    prio_a = atoi (str);
  props_b = wp_proxy_get_properties (WP_PROXY (li_b->ep));
  str = wp_properties_get (props_b, "endpoint.priority");
  if (str)
    prio_b = atoi (str);
  return prio_b - prio_a;
}

static gboolean
wp_config_policy_context_handle_pending_link (WpConfigPolicyContext *self,
    struct link_info *li, WpEndpoint *target)
{
  g_autoptr (WpProperties) props = NULL;
  const guint32 target_id = wp_proxy_get_bound_id (WP_PROXY (target));

  /* Create the link properties */
  props = wp_properties_new_empty ();
  switch (wp_endpoint_get_direction (li->ep)) {
  case WP_DIRECTION_INPUT:
    /* Capture */
    wp_properties_setf (props, "endpoint-link.output.endpoint", "%u", target_id);
    wp_properties_setf (props, "endpoint-link.output.stream", "%u", li->stream_id);
    break;
  case WP_DIRECTION_OUTPUT:
    /* Playback */
    wp_properties_setf (props, "endpoint-link.input.endpoint", "%u", target_id);
    wp_properties_setf (props, "endpoint-link.input.stream", "%u", li->stream_id);
    break;
  default:
    g_return_val_if_reached (FALSE);
  }

  /* Create the link */
  wp_endpoint_create_link (li->ep, props);
  return TRUE;
}

static void
links_table_handle_foreach (gpointer key, gpointer value, gpointer data)
{
  WpConfigPolicyContext *self = data;
  WpEndpoint *target = key;
  GPtrArray *link_infos = value;

  /* Sort the link infos by role and creation time */
  g_ptr_array_sort_with_data (link_infos, link_info_compare_func, target);

  /* Handle the first endpoint and also the ones with keep=true */
  for (guint i = 0; i < link_infos->len; i++) {
    struct link_info *li = g_ptr_array_index (link_infos, i);
    if (i == 0 || li->keep)
      wp_config_policy_context_handle_pending_link (self, li, target);
  }
}

static void
on_session_endpoints_changed (WpSession *session, WpConfigPolicyContext *self)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  wp_debug ("endpoints changed");

  /* Create the links table <target, array-of-link-info> */
  GHashTable *links_table = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, g_object_unref, (GDestroyNotify) g_ptr_array_unref);

  /* Fill the links table */
  it = wp_session_iterate_endpoints (session);
  while (wp_iterator_next (it, &val)) {
    WpEndpoint *ep = g_value_get_object (&val);
    wp_config_policy_context_add_link_info (self, links_table, session, ep);
    g_value_unset (&val);
  }

  /* Handle the links */
  g_hash_table_foreach (links_table, links_table_handle_foreach, self);

  /* Clean up */
  g_clear_pointer (&links_table, g_hash_table_unref);
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

      /* Emit the link activated signal */
      g_signal_emit (self, signals[SIGNAL_LINK_ACTIVATED], 0, ep_link);
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
  wp_object_manager_add_interest_1 (self->sessions_om, WP_TYPE_SESSION, NULL);
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
  signals[SIGNAL_LINK_ACTIVATED] = g_signal_new ("link-activated",
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
