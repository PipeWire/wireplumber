/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <spa/utils/keys.h>

#include <pipewire/pipewire.h>

#include <wp/wp.h>

#include "config-policy.h"
#include "parser-endpoint-link.h"
#include "parser-streams.h"

struct _WpConfigPolicy
{
  WpPolicy parent;

  WpConfiguration *config;

  gboolean pending_rescan;
  WpBaseEndpoint *pending_endpoint;
  WpBaseEndpoint *pending_target;
};

enum {
  PROP_0,
  PROP_CONFIG,
};

enum {
  SIGNAL_DONE,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (WpConfigPolicy, wp_config_policy, WP_TYPE_POLICY)

static void
on_endpoint_link_created (GObject *initable, GAsyncResult *res, gpointer p)
{
  WpConfigPolicy *self = p;
  g_autoptr (WpBaseEndpointLink) link = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpBaseEndpoint) src_ep = NULL;
  g_autoptr (WpBaseEndpoint) sink_ep = NULL;

  /* Get the link */
  link = wp_base_endpoint_link_new_finish(initable, res, &error);

  /* Log linking info */
  if (error) {
    g_warning ("Could not link endpoints: %s\n", error->message);
    return;
  }

  g_return_if_fail (link);
  src_ep = wp_base_endpoint_link_get_source_endpoint (link);
  sink_ep = wp_base_endpoint_link_get_sink_endpoint (link);
  g_info ("Sucessfully linked '%s' to '%s'\n", wp_base_endpoint_get_name (src_ep),
      wp_base_endpoint_get_name (sink_ep));

  /* Clear the pending target */
  g_clear_object (&self->pending_target);

  /* Emit the done signal */
  if (self->pending_endpoint) {
    gboolean is_capture =
      wp_base_endpoint_get_direction (self->pending_endpoint) == PW_DIRECTION_INPUT;
    if (self->pending_endpoint == (is_capture ? sink_ep : src_ep)) {
      g_autoptr (WpBaseEndpoint) pending_endpoint =
          g_steal_pointer (&self->pending_endpoint);
      g_signal_emit (self, signals[SIGNAL_DONE], 0, pending_endpoint, link);
    }
  }
}

static gboolean
wp_config_policy_can_link_stream (WpConfigPolicy *self, WpBaseEndpoint *target,
    const struct WpParserEndpointLinkData *data, guint32 stream_id)
{
  g_autoptr (WpConfigParser) parser = NULL;
  const struct WpParserStreamsData *streams_data = NULL;

  /* If no streams data is specified, we can link */
  if (!data->te.streams)
    return TRUE;

  /* If the endpoint is not linked, we can link */
  if (!wp_base_endpoint_is_linked (target))
    return TRUE;

  /* Get the linked stream */
  gboolean is_capture = wp_base_endpoint_get_direction (target) == PW_DIRECTION_INPUT;
  GPtrArray *links = wp_base_endpoint_get_links (target);
  WpBaseEndpointLink *l = g_ptr_array_index (links, 0);
  guint32 linked_stream = is_capture ?
      wp_base_endpoint_link_get_sink_stream (l) :
          wp_base_endpoint_link_get_source_stream (l);

  /* Check if linked stream is the same as target stream. Last one wins */
  if (linked_stream == stream_id)
    return TRUE;

  /* Get the linked stream name */
  g_autoptr (GVariant) s = wp_base_endpoint_get_stream (target, linked_stream);
  if (!s)
    return TRUE;
  const gchar *linked_stream_name;
  if (!g_variant_lookup (s, "name", "&s", &linked_stream_name))
    return TRUE;

  /* Get the target stream name */
  g_autoptr (GVariant) ts = wp_base_endpoint_get_stream (target, stream_id);
  if (!ts)
    return TRUE;
  const gchar *target_stream_name;
  if (!g_variant_lookup (ts, "name", "&s", &target_stream_name))
    return TRUE;

  /* Get the linked and ep streams data */
  parser = wp_configuration_get_parser (self->config,
      WP_PARSER_STREAMS_EXTENSION);
  streams_data = wp_config_parser_get_matched_data (parser, data->te.streams);
  if (!streams_data)
    return TRUE;
  const struct WpParserStreamsStreamData *linked_stream_data =
      wp_parser_streams_find_stream (streams_data, linked_stream_name);
  const struct WpParserStreamsStreamData *ep_stream_data =
      wp_parser_streams_find_stream (streams_data, target_stream_name);

  g_debug ("Trying to link to '%s' (%d); target is linked on '%s' (%d)",
      target_stream_name, ep_stream_data ? ep_stream_data->priority : -1,
      linked_stream_name, linked_stream_data ? linked_stream_data->priority : -1);

  /* Return false if linked stream has higher priority than ep stream */
  if (linked_stream_data && ep_stream_data) {
    if (linked_stream_data->priority > ep_stream_data->priority)
      return FALSE;
    else
      return TRUE;
  }
  if (linked_stream_data && !ep_stream_data)
    return FALSE;
  if (!linked_stream_data && ep_stream_data)
    return TRUE;

  return TRUE;
}

static gboolean
wp_config_policy_link_endpoint_with_target (WpConfigPolicy *policy,
    WpBaseEndpoint *ep, guint32 ep_stream, WpBaseEndpoint *target,
    guint32 target_stream, const struct WpParserEndpointLinkData *data)
{
  WpConfigPolicy *self = WP_CONFIG_POLICY (policy);
  g_autoptr (WpCore) core = wp_policy_get_core (WP_POLICY (self));
  gboolean is_capture = wp_base_endpoint_get_direction (ep) == PW_DIRECTION_INPUT;
  gboolean is_linked = wp_base_endpoint_is_linked (ep);
  gboolean target_linked = wp_base_endpoint_is_linked (target);

  g_debug ("Trying to link with '%s' to target '%s', ep_capture:%d, "
      "ep_linked:%d, target_linked:%d", wp_base_endpoint_get_name (ep),
      wp_base_endpoint_get_name (target), is_capture, is_linked, target_linked);

  /* Check if the endpoint is already linked with the proper target */
  if (is_linked) {
    GPtrArray *links = wp_base_endpoint_get_links (ep);
    WpBaseEndpointLink *l = g_ptr_array_index (links, 0);
    g_autoptr (WpBaseEndpoint) src_ep = wp_base_endpoint_link_get_source_endpoint (l);
    g_autoptr (WpBaseEndpoint) sink_ep = wp_base_endpoint_link_get_sink_endpoint (l);
    WpBaseEndpoint *existing_target = is_capture ? src_ep : sink_ep;

    if (existing_target == target) {
      /* linked to correct target so do nothing */
      g_debug ("Endpoint '%s' is already linked correctly",
          wp_base_endpoint_get_name (ep));
      return FALSE;
    } else {
      /* linked to the wrong target so unlink and continue */
      g_debug ("Unlinking endpoint '%s' from its previous target",
          wp_base_endpoint_get_name (ep));
      wp_base_endpoint_link_destroy (l);
    }
  }

  /* Make sure the target is not going to be linked with another endpoint */
  if (self->pending_target == target)
    return FALSE;
  g_clear_object (&self->pending_target);
  self->pending_target = g_object_ref (target);

  /* Unlink the target links that are not kept if endpoint is capture */
  if (!is_capture && target_linked) {
    GPtrArray *links = wp_base_endpoint_get_links (target);
    for (guint i = 0; i < links->len; i++) {
      WpBaseEndpointLink *l = g_ptr_array_index (links, i);
      if (!wp_base_endpoint_link_is_kept (l))
        wp_base_endpoint_link_destroy (l);
    }
  }

  /* Link the client with the target */
  if (is_capture) {
    wp_base_endpoint_link_new (core, target, target_stream, ep, ep_stream,
        data->el.keep, on_endpoint_link_created, self);
  } else {
    wp_base_endpoint_link_new (core, ep, ep_stream, target, target_stream,
        data->el.keep, on_endpoint_link_created, self);
  }

  return TRUE;
}

static gboolean
wp_config_policy_handle_endpoint (WpPolicy *policy, WpBaseEndpoint *ep)
{
  WpConfigPolicy *self = WP_CONFIG_POLICY (policy);
  g_autoptr (WpCore) core = wp_policy_get_core (policy);
  g_autoptr (WpConfigParser) parser = NULL;
  const struct WpParserEndpointLinkData *data;
  GVariantBuilder b;
  GVariant *target_data = NULL;
  g_autoptr (WpBaseEndpoint) target = NULL;
  guint32 stream_id;
  const char *role = NULL;
  gboolean can_link;

  /* Get the parser for the endpoint-link extension */
  parser = wp_configuration_get_parser (self->config,
      WP_PARSER_ENDPOINT_LINK_EXTENSION);

  /* Get the matched endpoint data from the parser */
  data = wp_config_parser_get_matched_data (parser, G_OBJECT (ep));
  if (!data)
    return FALSE;

  /* Create the target gvariant */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "data", g_variant_new_uint64 ((guint64) data));
  role = wp_base_endpoint_get_role (ep);
  if (role)
    g_variant_builder_add (&b, "{sv}", "role", g_variant_new_string (role));
  target_data = g_variant_builder_end (&b);

  /* Find the target endpoint */
  target = wp_policy_find_endpoint (core, target_data, &stream_id);
  if (!target) {
    g_info ("Target not found for endpoint '%s'", wp_base_endpoint_get_name (ep));
    return FALSE;
  }

  /* Don't link if the target is linked with a higher priority stream */
  can_link = wp_config_policy_can_link_stream (self, target, data, stream_id);

  g_debug ("Trying to handle endpoint: %s, role:%s, can_link:%d",
      wp_base_endpoint_get_name (ep), role, can_link);

  /* Link the endpoint with its target */
  return can_link && wp_config_policy_link_endpoint_with_target (self, ep,
          WP_STREAM_ID_NONE, target, stream_id, data);
}

static const char *
wp_config_policy_get_prioritized_stream (WpPolicy *policy,
    const char *ep_stream, const char *te_stream, const char *te_streams)
{
  WpConfigPolicy *self = WP_CONFIG_POLICY (policy);

  /* The target stream has higher priority than the endpoint stream */
  const char *res = te_stream ? te_stream : ep_stream;
  if (res)
    return res;

  /* If both streams are null, and no streams file is defined, return NULL */
  if (!te_streams)
    return NULL;

  /* Otherwise get the lowest stream from streams */
  g_autoptr (WpConfigParser) parser = NULL;
  const struct WpParserStreamsData *streams = NULL;
  const struct WpParserStreamsStreamData *lowest = NULL;
  parser = wp_configuration_get_parser (self->config,
      WP_PARSER_STREAMS_EXTENSION);
  streams = wp_config_parser_get_matched_data (parser, (gpointer)te_streams);
  if (!streams)
    return NULL;
  lowest = wp_parser_streams_get_lowest_stream (streams);
  return lowest ? lowest->name : NULL;
}

static WpBaseEndpoint *
wp_config_policy_find_endpoint (WpPolicy *policy, GVariant *props,
    guint32 *stream_id)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpPolicyManager) pmgr = NULL;
  const struct WpParserEndpointLinkData *data = NULL;
  g_autoptr (GPtrArray) endpoints = NULL;
  guint i;
  WpBaseEndpoint *target = NULL;
  g_autoptr (WpProxy) proxy = NULL;
  g_autoptr (WpProperties) p = NULL;
  const char *role = NULL;

  /* Get the data from props */
  g_variant_lookup (props, "data", "t", &data);
  if (!data)
    return NULL;

  /* Get all the endpoints matching the media class */
  core = wp_policy_get_core (policy);
  pmgr = wp_policy_manager_get_instance (core);
  endpoints = wp_policy_manager_list_endpoints (pmgr,
      data->te.endpoint_data.media_class);
  if (!endpoints)
    return NULL;

  /* Get the first endpoint that matches target data */
  for (i = 0; i < endpoints->len; i++) {
    target = g_ptr_array_index (endpoints, i);
    if (wp_parser_endpoint_link_matches_endpoint_data (target,
        &data->te.endpoint_data))
      break;
  }

  /* If target did not match any data, return NULL */
  if (i >= endpoints->len)
    return NULL;

  /* Set the stream id */
  if (stream_id) {
    if (target) {
      g_variant_lookup (props, "role", "&s", &role);
      const char *prioritized = wp_config_policy_get_prioritized_stream (policy,
          role, data->te.stream, data->te.streams);
      *stream_id = prioritized ? wp_base_endpoint_find_stream (target, prioritized) :
          WP_STREAM_ID_NONE;
    } else {
      *stream_id = WP_STREAM_ID_NONE;
    }
  }

  return g_object_ref (target);
}

static gint
endpoint_creation_time_compare_func (gconstpointer a, gconstpointer b)
{
  WpBaseEndpoint *ep_a = *(WpBaseEndpoint *const *)a;
  WpBaseEndpoint *ep_b = *(WpBaseEndpoint *const *)b;

  return wp_base_endpoint_get_creation_time (ep_b) -
      wp_base_endpoint_get_creation_time (ep_a);
}

static void
wp_config_policy_sync_rescan (WpCore *core, GAsyncResult *res, gpointer data)
{
  WpConfigPolicy *self = WP_CONFIG_POLICY (data);
  g_autoptr (WpPolicyManager) pmgr = wp_policy_manager_get_instance (core);
  g_autoptr (GPtrArray) endpoints = NULL;
  WpBaseEndpoint *ep;
  gboolean handled = FALSE;

  /* Handle all endpoints when rescanning */
  endpoints = wp_policy_manager_list_endpoints (pmgr, NULL);
  if (endpoints) {
    /* Sort the endpoints by creation time */
    g_ptr_array_sort (endpoints, endpoint_creation_time_compare_func);

    for (guint i = 0; i < endpoints->len; i++) {
      ep = g_ptr_array_index (endpoints, i);
      if (wp_config_policy_handle_endpoint (WP_POLICY (self), ep))
        handled = ep == self->pending_endpoint;
    }
  }

  /* If endpoint was not handled, we are done */
  if (!handled) {
      g_signal_emit (self, signals[SIGNAL_DONE], 0, self->pending_endpoint,
          NULL);
      g_clear_object (&self->pending_endpoint);
  }

  self->pending_rescan = FALSE;
}

static void
wp_config_policy_rescan (WpConfigPolicy *self, WpBaseEndpoint *ep)
{
  if (self->pending_rescan)
    return;

  /* Check if there is a pending link while a new endpoint is added/removed */
  if (self->pending_endpoint) {
    g_warning ("Not handling endpoint '%s' beacause of pending link",
        wp_base_endpoint_get_name (ep));
    return;
  }

  g_autoptr (WpCore) core = wp_policy_get_core (WP_POLICY (self));
  if (!core)
    return;

  self->pending_endpoint = g_object_ref (ep);
  wp_core_sync (core, NULL, (GAsyncReadyCallback)wp_config_policy_sync_rescan,
      self);
  self->pending_rescan = TRUE;
}

static void
wp_config_policy_endpoint_added (WpPolicy *policy, WpBaseEndpoint *ep)
{
  WpConfigPolicy *self = WP_CONFIG_POLICY (policy);
  wp_config_policy_rescan (self, ep);
}

static void
wp_config_policy_endpoint_removed (WpPolicy *policy, WpBaseEndpoint *ep)
{
  WpConfigPolicy *self = WP_CONFIG_POLICY (policy);
  wp_config_policy_rescan (self, ep);
}

static void
wp_config_policy_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpConfigPolicy *self = WP_CONFIG_POLICY (object);

  switch (property_id) {
  case PROP_CONFIG:
    self->config = g_value_dup_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_config_policy_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpConfigPolicy *self = WP_CONFIG_POLICY (object);

  switch (property_id) {
  case PROP_CONFIG:
    g_value_take_object (value, g_object_ref (self->config));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_config_policy_constructed (GObject * object)
{
  WpConfigPolicy *self = WP_CONFIG_POLICY (object);

  /* Add the parsers */
  wp_configuration_add_extension (self->config,
      WP_PARSER_ENDPOINT_LINK_EXTENSION, WP_TYPE_PARSER_ENDPOINT_LINK);
  wp_configuration_add_extension (self->config,
      WP_PARSER_STREAMS_EXTENSION, WP_TYPE_PARSER_STREAMS);

  /* Parse the file */
  wp_configuration_reload (self->config, WP_PARSER_ENDPOINT_LINK_EXTENSION);
  wp_configuration_reload (self->config, WP_PARSER_STREAMS_EXTENSION);

  G_OBJECT_CLASS (wp_config_policy_parent_class)->constructed (object);
}

static void
wp_config_policy_finalize (GObject *object)
{
  WpConfigPolicy *self = WP_CONFIG_POLICY (object);

  /* Remove the extensions from the configuration */
  wp_configuration_remove_extension (self->config,
      WP_PARSER_ENDPOINT_LINK_EXTENSION);
  wp_configuration_remove_extension (self->config,
      WP_PARSER_STREAMS_EXTENSION);

  /* Clear the configuration */
  g_clear_object (&self->config);

  G_OBJECT_CLASS (wp_config_policy_parent_class)->finalize (object);
}

static void
wp_config_policy_init (WpConfigPolicy *self)
{
  self->pending_rescan = FALSE;
}

static void
wp_config_policy_class_init (WpConfigPolicyClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPolicyClass *policy_class = (WpPolicyClass *) klass;

  object_class->constructed = wp_config_policy_constructed;
  object_class->finalize = wp_config_policy_finalize;
  object_class->set_property = wp_config_policy_set_property;
  object_class->get_property = wp_config_policy_get_property;

  policy_class->endpoint_added = wp_config_policy_endpoint_added;
  policy_class->endpoint_removed = wp_config_policy_endpoint_removed;
  policy_class->find_endpoint = wp_config_policy_find_endpoint;

  /* Properties */
  g_object_class_install_property (object_class, PROP_CONFIG,
      g_param_spec_object ("configuration", "configuration",
      "The configuration this policy is based on", WP_TYPE_CONFIGURATION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  /* Signals */
  signals[SIGNAL_DONE] = g_signal_new ("done",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 2, WP_TYPE_BASE_ENDPOINT, WP_TYPE_BASE_ENDPOINT_LINK);
}

WpConfigPolicy *
wp_config_policy_new (WpConfiguration *config)
{
  return g_object_new (wp_config_policy_get_type (),
      "rank", WP_POLICY_RANK_UPSTREAM,
      "configuration", config,
      NULL);
}
