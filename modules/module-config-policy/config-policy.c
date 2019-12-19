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

struct link_info {
  WpBaseEndpoint *ep;
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

struct _WpConfigPolicy
{
  WpPolicy parent;

  WpConfiguration *config;

  gboolean pending_rescan;
  WpBaseEndpoint *pending_endpoint;
  gboolean endpoint_handled;
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
wp_config_policy_handle_pending_link (WpConfigPolicy *self,
    struct link_info *li, WpBaseEndpoint *target)
{
  g_autoptr (WpCore) core = wp_policy_get_core (WP_POLICY (self));
  gboolean is_capture = wp_base_endpoint_get_direction (li->ep) == PW_DIRECTION_INPUT;
  gboolean is_linked = wp_base_endpoint_is_linked (li->ep);
  gboolean target_linked = wp_base_endpoint_is_linked (target);

  g_debug ("Trying to link with '%s' to target '%s', ep_capture:%d, "
      "ep_linked:%d, target_linked:%d", wp_base_endpoint_get_name (li->ep),
      wp_base_endpoint_get_name (target), is_capture, is_linked, target_linked);

  /* Check if the endpoint is already linked with the proper target */
  if (is_linked) {
    GPtrArray *links = wp_base_endpoint_get_links (li->ep);
    WpBaseEndpointLink *l = g_ptr_array_index (links, 0);
    g_autoptr (WpBaseEndpoint) src_ep = wp_base_endpoint_link_get_source_endpoint (l);
    g_autoptr (WpBaseEndpoint) sink_ep = wp_base_endpoint_link_get_sink_endpoint (l);
    WpBaseEndpoint *existing_target = is_capture ? src_ep : sink_ep;

    if (existing_target == target) {
      /* linked to correct target so do nothing */
      g_debug ("Endpoint '%s' is already linked correctly",
          wp_base_endpoint_get_name (li->ep));
      return FALSE;
    } else {
      /* linked to the wrong target so unlink and continue */
      g_debug ("Unlinking endpoint '%s' from its previous target",
          wp_base_endpoint_get_name (li->ep));
      wp_base_endpoint_link_destroy (l);
    }
  }

  /* Unlink the target links that are not kept if endpoint is playback */
  if (!is_capture && target_linked && !li->keep) {
    GPtrArray *links = wp_base_endpoint_get_links (target);
    for (guint i = 0; i < links->len; i++) {
      WpBaseEndpointLink *l = g_ptr_array_index (links, i);
      if (!wp_base_endpoint_link_is_kept (l))
        wp_base_endpoint_link_destroy (l);
    }
  }

  /* Link the client with the target */
  if (is_capture) {
    wp_base_endpoint_link_new (core, target, li->stream_id, li->ep,
        WP_STREAM_ID_NONE, li->keep, on_endpoint_link_created, self);
  } else {
    wp_base_endpoint_link_new (core, li->ep, WP_STREAM_ID_NONE, target,
        li->stream_id, li->keep, on_endpoint_link_created, self);
  }

  return TRUE;
}

static WpBaseEndpoint*
wp_config_policy_get_data_target (WpConfigPolicy *self, const char *ep_role,
    const struct WpParserEndpointLinkData *data, guint32 *stream_id)
{
  g_autoptr (WpCore) core = wp_policy_get_core (WP_POLICY (self));
  GVariantBuilder b;
  GVariant *target_data = NULL;

  g_return_val_if_fail (data, NULL);

  /* Create the target gvariant */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "data", g_variant_new_uint64 ((guint64) data));
  if (ep_role)
    g_variant_builder_add (&b, "{sv}", "role", g_variant_new_string (ep_role));
  target_data = g_variant_builder_end (&b);

  /* Find the target endpoint */
  return wp_policy_find_endpoint (core, target_data, stream_id);
}

static guint
wp_config_policy_get_endpoint_lowest_priority_stream_id (WpBaseEndpoint *ep)
{
  g_autoptr (GVariant) streams = NULL;
  g_autoptr (GVariant) child = NULL;
  GVariantIter *iter;
  guint lowest_priority = G_MAXUINT;
  guint res = WP_STREAM_ID_NONE;
  guint priority;
  guint id;

  g_return_val_if_fail (ep, WP_STREAM_ID_NONE);

  streams = wp_base_endpoint_list_streams (ep);
  g_return_val_if_fail (streams, WP_STREAM_ID_NONE);

  g_variant_get (streams, "aa{sv}", &iter);
  while ((child = g_variant_iter_next_value (iter))) {
    g_variant_lookup (child, "id", "u", &id);
    g_variant_lookup (child, "priority", "u", &priority);
    if (priority <= lowest_priority) {
      lowest_priority = priority;
      res = id;
    }
  }
  g_variant_iter_free (iter);

  return res;
}

static WpBaseEndpoint *
wp_config_policy_find_endpoint (WpPolicy *policy, GVariant *props,
    guint32 *stream_id)
{
  g_autoptr (WpCore) core = wp_policy_get_core (policy);
  g_autoptr (WpPolicyManager) pmgr = wp_policy_manager_get_instance (core);
  g_autoptr (WpSession) session = wp_policy_manager_get_session (pmgr);
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

  /* If target-endpoint data was defined in the configuration file, find the
   * matching endpoint based on target-endpoint data */
  if (data->has_te) {
    /* Get all the endpoints matching the media class */
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
  }

  /* Otherwise, use the default session endpoint if the session is valid */
  else if (session) {
    /* Get the default type */
    WpDefaultEndpointType type;
    switch (data->me.endpoint_data.direction) {
      case PW_DIRECTION_INPUT:
        type = WP_DEFAULT_ENDPOINT_TYPE_AUDIO_SOURCE;
        break;
      case PW_DIRECTION_OUTPUT:
        type = WP_DEFAULT_ENDPOINT_TYPE_AUDIO_SINK;
        break;
      default:
        g_warn_if_reached ();
        return NULL;
    }

    /* Get all the endpoints */
    endpoints = wp_policy_manager_list_endpoints (pmgr, NULL);
    if (!endpoints)
      return NULL;

    /* Find the default session endpoint */
    for (i = 0; i < endpoints->len; i++) {
      target = g_ptr_array_index (endpoints, i);
      guint def_id = wp_session_get_default_endpoint (session, type);
      if (def_id == wp_base_endpoint_get_global_id (target))
        break;
    }
  }

  /* If no target data has been defined and session is not valid, return null */
  else
    return NULL;

  /* If target did not match any data, return NULL */
  if (i >= endpoints->len)
    return NULL;

  /* Set the stream id */
  if (stream_id) {
    if (target) {
      g_variant_lookup (props, "role", "&s", &role);
      /* The target stream has higher priority than the endpoint stream */
      const char *prioritized = data->te.stream ? data->te.stream : role;
      *stream_id = prioritized ?
          wp_base_endpoint_find_stream (target, prioritized) :
          wp_config_policy_get_endpoint_lowest_priority_stream_id (target);
    } else {
      *stream_id = WP_STREAM_ID_NONE;
    }
  }

  return g_object_ref (target);
}

static gint
link_info_compare_func (gconstpointer a, gconstpointer b, gpointer data)
{
  WpBaseEndpoint *target = data;
  struct link_info *li_a = *(struct link_info *const *)a;
  struct link_info *li_b = *(struct link_info *const *)b;
  g_autoptr (GVariant) stream_a = NULL;
  g_autoptr (GVariant) stream_b = NULL;
  guint priority_a = 0;
  guint priority_b = 0;
  gint res = 0;

  res = (gint) li_a->keep - (gint) li_b->keep;
  if (res != 0)
    return res;

  /* Get the role priority of a */
  stream_a = wp_base_endpoint_get_stream (target, li_a->stream_id);
  if (stream_a)
    g_variant_lookup (stream_a, "priority", "u", &priority_a);

  /* Get the role priority of b */
  stream_b = wp_base_endpoint_get_stream (target, li_b->stream_id);
  if (stream_b)
    g_variant_lookup (stream_b, "priority", "u", &priority_b);

  /* We want to sort from high priority to low priority */
  res = priority_b - priority_a;

  /* If both priorities are the same, sort by creation time */
  if (res == 0)
    res = wp_base_endpoint_get_creation_time (li_b->ep) -
      wp_base_endpoint_get_creation_time (li_a->ep);

  return res;
}

static void
wp_config_policy_add_link_info (WpConfigPolicy *self, GHashTable *table,
    WpBaseEndpoint *ep)
{
  g_autoptr (WpConfigParser) parser = NULL;
  const struct WpParserEndpointLinkData *data = NULL;
  const char *ep_role = wp_base_endpoint_get_role (ep);
  struct link_info *res = NULL;
  WpBaseEndpoint *target = NULL;
  guint32 stream_id = WP_STREAM_ID_NONE;

  /* Get the parser for the endpoint-link extension */
  parser = wp_configuration_get_parser (self->config,
      WP_PARSER_ENDPOINT_LINK_EXTENSION);

  /* Get the matched endpoint data from the parser */
  data = wp_config_parser_get_matched_data (parser, G_OBJECT (ep));
  if (!data)
    return;

  /* Get the target */
  target = wp_config_policy_get_data_target (self, ep_role, data, &stream_id);
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
    g_hash_table_insert (table, g_object_ref (target), link_infos);
  } else {
    g_ptr_array_add (link_infos, res);
  }
}


static void
links_table_handle_foreach (gpointer key, gpointer value, gpointer data)
{
  WpConfigPolicy *self = data;
  WpBaseEndpoint *target = key;
  GPtrArray *link_infos = value;

  /* Sort the link infos by role and creation time */
  g_ptr_array_sort_with_data (link_infos, link_info_compare_func, target);

  /* Handle the first endpoint and also the ones with keep=true */
  for (guint i = 0; i < link_infos->len; i++) {
    struct link_info *li = g_ptr_array_index (link_infos, i);
    if (i == 0 || li->keep)
      if (wp_config_policy_handle_pending_link (self, li, target))
        self->endpoint_handled = li->ep == self->pending_endpoint;
  }
}

static void
wp_config_policy_sync_rescan (WpCore *core, GAsyncResult *res, gpointer data)
{
  WpConfigPolicy *self = WP_CONFIG_POLICY (data);
  g_autoptr (WpPolicyManager) pmgr = wp_policy_manager_get_instance (core);
  g_autoptr (GPtrArray) endpoints = NULL;

  g_debug ("rescanning");

  /* Set handle to false to know if pending endpoint was handled in this loop */
  self->endpoint_handled = FALSE;

  /* Handle all endpoints when rescanning */
  endpoints = wp_policy_manager_list_endpoints (pmgr, NULL);
  if (endpoints) {
    /* Create the links table <target, array-of-link-info> */
    GHashTable *links_table = g_hash_table_new_full (g_direct_hash,
        g_direct_equal, g_object_unref, (GDestroyNotify) g_ptr_array_unref);

    /* Fill the links table */
    for (guint i = 0; i < endpoints->len; i++) {
      WpBaseEndpoint *ep = g_ptr_array_index (endpoints, i);
      wp_config_policy_add_link_info (self, links_table, ep);
    }

    /* Handle the links */
    g_hash_table_foreach (links_table, links_table_handle_foreach, self);

    /* Clean up */
    g_clear_pointer (&links_table, g_hash_table_unref);
  }

  /* If endpoint was not handled, we are done */
  if (!self->endpoint_handled) {
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

  /* Parse the file */
  wp_configuration_reload (self->config, WP_PARSER_ENDPOINT_LINK_EXTENSION);

  G_OBJECT_CLASS (wp_config_policy_parent_class)->constructed (object);
}

static void
wp_config_policy_finalize (GObject *object)
{
  WpConfigPolicy *self = WP_CONFIG_POLICY (object);

  /* Remove the extensions from the configuration */
  wp_configuration_remove_extension (self->config,
      WP_PARSER_ENDPOINT_LINK_EXTENSION);

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
