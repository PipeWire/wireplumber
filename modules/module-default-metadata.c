/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <errno.h>
#include <pipewire/keys.h>

#define STATE_NAME "default-metadata"
#define SAVE_INTERVAL_MS 1000

#define direction_to_dbg_string(dir) \
  ((dir == WP_DIRECTION_INPUT) ? "sink" : "source")

#define default_endpoint_key(dir) ((dir == WP_DIRECTION_INPUT) ? \
  "default.session.endpoint.sink" : "default.session.endpoint.source")

#define default_audio_node_key(dir) ((dir == WP_DIRECTION_INPUT) ? \
  "default.configured.audio.sink" : "default.configured.audio.source")

G_DECLARE_FINAL_TYPE (WpDefaultMetadata, wp_default_metadata, WP,
    DEFAULT_METADATA, WpPlugin)

struct _WpDefaultData
{
  WpDefaultMetadata *self;
  gchar *group;
  WpProperties *props;
};
typedef struct _WpDefaultData WpDefaultData;

struct _WpDefaultMetadata
{
  WpPlugin parent;
  WpState *state;
  WpDefaultData default_datas[2][2];  /* AudioNode/Endpoint Input/Output*/
  WpObjectManager *metadatas_om;
  WpObjectManager *nodes_om;
  WpObjectManager *sessions_om;
  guint metadata_id;
  GSource *timeout_source;
};

G_DEFINE_TYPE (WpDefaultMetadata, wp_default_metadata, WP_TYPE_PLUGIN)

static gboolean
timeout_save_callback (gpointer p)
{
  WpDefaultData *d = p;
  WpDefaultMetadata *self = d->self;

  if (!wp_state_save (self->state, d->group, d->props))
    wp_warning_object (self, "could not save default endpoints in %s",
        STATE_NAME);

  return G_SOURCE_REMOVE;
}

static void
timeout_save_default_data (WpDefaultMetadata *self, gboolean is_ep, guint dir,
    guint ms)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  /* Clear the current timeout callback */
  if (self->timeout_source)
    g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  /* Add the timeout callback */
  wp_core_timeout_add (core, &self->timeout_source, ms, timeout_save_callback,
      &self->default_datas[is_ep][dir], NULL);
}

static WpEndpoint *
find_endpoint_with_endpoint_id (WpDefaultMetadata * self, guint session_id,
    guint ep_id, WpSession **session)
{
  g_autoptr (WpSession) s = NULL;
  g_autoptr (WpEndpoint) ep = NULL;

  s = wp_object_manager_lookup (self->sessions_om, WP_TYPE_SESSION,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", session_id, NULL);
  if (!s)
      return NULL;

  ep = wp_session_lookup_endpoint (s, WP_CONSTRAINT_TYPE_G_PROPERTY,
      "bound-id", "=u", ep_id, NULL);
  if (!ep)
    return NULL;

  if (session)
    *session = g_object_ref (s);

  return g_object_ref (ep);
}

static void
on_default_metadata_changed (WpMetadata *m, guint32 subject,
    const gchar *key, const gchar *type, const gchar *value, gpointer *d)
{
  WpDefaultMetadata * self = WP_DEFAULT_METADATA (d);
  guint dir = WP_DIRECTION_INPUT;
  gboolean is_default_ep = FALSE;

  /* Get the direction */
  if (!g_strcmp0 (key, default_endpoint_key (WP_DIRECTION_INPUT))) {
    dir = WP_DIRECTION_INPUT;
    is_default_ep = TRUE;
  } else if (!g_strcmp0 (key, default_endpoint_key (WP_DIRECTION_OUTPUT))) {
    dir = WP_DIRECTION_OUTPUT;
    is_default_ep = TRUE;
  } else if (!g_strcmp0 (key, default_audio_node_key (WP_DIRECTION_INPUT))) {
    dir = WP_DIRECTION_INPUT;
    is_default_ep = FALSE;
  } else if (!g_strcmp0 (key, default_audio_node_key (WP_DIRECTION_OUTPUT))) {
    dir = WP_DIRECTION_OUTPUT;
    is_default_ep = FALSE;
  } else {
    return;
  }

  /* Endpoint */
  if (is_default_ep) {
    const gchar *session_name = NULL, *ep_name = NULL;
    g_autoptr (WpSession) session = NULL;
    g_autoptr (WpEndpoint) ep = NULL;

    /* Find endpoint and session */
    ep = find_endpoint_with_endpoint_id (self, subject,
        atoi (value), &session);
    if (!ep || !session)
      return;

    /* Get endpoint name and session name */
    session_name = wp_session_get_name (session);
    ep_name = wp_endpoint_get_name (ep);
    g_return_if_fail (session_name);
    g_return_if_fail (ep_name);

    /* Set state properties */
    wp_properties_set (self->default_datas[1][dir].props,
        session_name, ep_name);
  }

  /* Audio Node */
  else {
    g_autoptr (WpNode) node = NULL;
    const gchar *node_name = NULL;

    /* Find node */
    node = wp_object_manager_lookup (self->nodes_om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", atoi (value), NULL);
    if (!node)
      return;

    /* Get node name */
    node_name = wp_pipewire_object_get_property (WP_PIPEWIRE_OBJECT (node),
        PW_KEY_NODE_NAME);
    g_return_if_fail (node_name);

    /* Set state properties */
    wp_properties_set (self->default_datas[0][dir].props,
        "audio", node_name);
  }

  /* Save state after specific interval */
  timeout_save_default_data (self, is_default_ep, dir, SAVE_INTERVAL_MS);
}

static WpEndpoint *
find_highest_priority_endpoint (WpSession * session, WpDirection dir)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  gint highest_prio = 0;
  WpEndpoint *res = NULL;

  it = wp_session_new_endpoints_filtered_iterator (session,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s",
      (dir == WP_DIRECTION_INPUT) ? "*/Sink" : "*/Source",
      NULL);

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpEndpoint *ep = g_value_get_object (&val);
    const gchar *prio_str = wp_pipewire_object_get_property (
        WP_PIPEWIRE_OBJECT (ep), "endpoint.priority");
    gint prio = atoi (prio_str);

    if (prio > highest_prio || res == NULL) {
      highest_prio = prio;
      res = ep;
    }
  }
  return res ? g_object_ref (res) : NULL;
}

static void
reevaluate_default_endpoints (WpDefaultMetadata * self, WpMetadata *m,
    WpSession *session, guint dir)
{
  g_autoptr (WpEndpoint) ep = NULL;
  const gchar *session_name = NULL, *ep_name = NULL;
  guint ep_id = 0;

  g_return_if_fail (m);
  g_return_if_fail (self->default_datas[1][dir].props);

  /* Find the default endpoint */
  session_name = wp_session_get_name (session);
  ep_name = wp_properties_get (self->default_datas[1][dir].props, session_name);
  if (ep_name) {
    ep = wp_session_lookup_endpoint (session,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "endpoint.name", "=s", ep_name,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s",
        (dir == WP_DIRECTION_INPUT) ? "*/Sink" : "*/Source", NULL);
  }

  /* If not found, use the highest priority one */
  if (!ep)
    ep = find_highest_priority_endpoint (session, dir);

  if (ep) {
    ep_id = wp_proxy_get_bound_id (WP_PROXY (ep));

    /* block the signal to avoid storing this; only selections done by the user
     * should be stored */
    g_signal_handlers_block_by_func (m, on_default_metadata_changed, self);

    /* Set default endpoint */
    g_autofree gchar *value = g_strdup_printf ("%d", ep_id);
    wp_metadata_set (m, wp_proxy_get_bound_id (WP_PROXY (session)),
        default_endpoint_key (dir), "Spa:Int", value);

    g_signal_handlers_unblock_by_func (m, on_default_metadata_changed, self);

    wp_info_object (self, "set default %s endpoint with id %d on session '%s'",
        direction_to_dbg_string (dir), ep_id, session_name);
  }
}

static void
reevaluate_default_audio_nodes (WpDefaultMetadata * self, WpMetadata *m,
    guint dir)
{
  g_autoptr (WpNode) node = NULL;
  const gchar *node_name = NULL;
  guint node_id = 0;

  g_return_if_fail (m);
  g_return_if_fail (self->default_datas[0][dir].props);

  /* Find the default node */
  node_name = wp_properties_get (self->default_datas[0][dir].props, "audio");
  if (node_name) {
    node = wp_object_manager_lookup (self->nodes_om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", node_name,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "=s",
        (dir == WP_DIRECTION_INPUT) ? "Audio/Sink" : "Audio/Source", NULL);
  }

  /* If not found, get the first one available */
  if (!node)
    node = wp_object_manager_lookup (self->sessions_om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "=s",
        (dir == WP_DIRECTION_INPUT) ? "Audio/Sink" : "Audio/Source", NULL);

  if (node) {
    node_id = wp_proxy_get_bound_id (WP_PROXY (node));

    /* block the signal to avoid storing this; only selections done by the user
     * should be stored */
    g_signal_handlers_block_by_func (m, on_default_metadata_changed, self);

    /* Set default node */
    g_autofree gchar *value = g_strdup_printf ("%d", node_id);
    wp_metadata_set (m, 0, default_audio_node_key (dir), "Spa:Int", value);

    g_signal_handlers_unblock_by_func (m, on_default_metadata_changed, self);

    wp_info_object (self, "set default %s audio node with id %d",
        direction_to_dbg_string (dir), node_id);
  }
}

static void
on_endpoints_changed (WpSession * session, WpDefaultMetadata * self)
{
  g_autoptr (WpMetadata) metadata = NULL;

  /* Get the metadata */
  metadata = wp_object_manager_lookup (self->metadatas_om, WP_TYPE_METADATA,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", self->metadata_id, NULL);
  if (!metadata)
    return;

  wp_trace_object (session, "endpoints changed, re-evaluating defaults");
  reevaluate_default_endpoints (self, metadata, session, WP_DIRECTION_INPUT);
  reevaluate_default_endpoints (self, metadata, session, WP_DIRECTION_OUTPUT);
}

static void
on_session_added (WpObjectManager * om, WpSession * session,
    WpDefaultMetadata * self)
{
  g_signal_connect_object (session, "endpoints-changed",
      G_CALLBACK (on_endpoints_changed), self, 0);
}

static void
on_nodes_changed (WpObjectManager * om, WpDefaultMetadata * self)
{
  g_autoptr (WpMetadata) metadata = NULL;

  /* Get the metadata */
  metadata = wp_object_manager_lookup (self->metadatas_om, WP_TYPE_METADATA,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", self->metadata_id, NULL);
  if (!metadata)
    return;

  wp_trace_object (om, "nodes changed, re-evaluating defaults");
  reevaluate_default_audio_nodes (self, metadata, WP_DIRECTION_INPUT);
  reevaluate_default_audio_nodes (self, metadata, WP_DIRECTION_OUTPUT);
}

static void
on_metadata_added (WpObjectManager *om, WpMetadata *metadata, gpointer d)
{
  WpDefaultMetadata * self = WP_DEFAULT_METADATA (d);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  g_return_if_fail (core);

  /* Only handle the first available metadata and skip the rest */
  if (self->metadata_id > 0)
    return;
  self->metadata_id = wp_proxy_get_bound_id (WP_PROXY (metadata));

  /* Handle the changed signal */
  g_signal_connect_object (metadata, "changed",
      G_CALLBACK (on_default_metadata_changed), self, 0);

  /* Create the nodes object manager */
  self->nodes_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->nodes_om, WP_TYPE_NODE, NULL);
  wp_object_manager_request_object_features (self->nodes_om, WP_TYPE_NODE,
      WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->nodes_om, "objects-changed",
      G_CALLBACK (on_nodes_changed), self, 0);
  wp_core_install_object_manager (core, self->nodes_om);

  /* Create the sessions object manager */
  self->sessions_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->sessions_om, WP_TYPE_SESSION, NULL);
  wp_object_manager_request_object_features (self->sessions_om, WP_TYPE_SESSION,
      WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->sessions_om, "object-added",
      G_CALLBACK (on_session_added), self, 0);
  wp_core_install_object_manager (core, self->sessions_om);
}

static void
wp_default_metadata_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpDefaultMetadata * self = WP_DEFAULT_METADATA (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));

  g_return_if_fail (core);

  /* Create the metadatas object manager */
  self->metadata_id = 0;
  self->metadatas_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->metadatas_om, WP_TYPE_METADATA, NULL);
  wp_object_manager_request_object_features (self->metadatas_om,
      WP_TYPE_METADATA, WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->metadatas_om, "object-added",
      G_CALLBACK (on_metadata_added), self, 0);
  wp_core_install_object_manager (core, self->metadatas_om);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_default_metadata_disable (WpPlugin * plugin)
{
  WpDefaultMetadata * self = WP_DEFAULT_METADATA (plugin);

  g_clear_object (&self->metadatas_om);
  g_clear_object (&self->nodes_om);
  g_clear_object (&self->sessions_om);
}

static void
unload_default_data (WpDefaultMetadata * self, WpDefaultData * d)
{
  g_clear_pointer (&d->props, wp_properties_unref);
  g_clear_pointer (&d->group, g_free);
  d->self = NULL;
}

static void
load_default_data (WpDefaultMetadata * self, WpDefaultData * d,
    const gchar *group)
{
  d->self = self;
  d->group = g_strdup (group);
  d->props = wp_state_load (self->state, d->group);
  if (!d->props)
    wp_warning_object ("could not load default endpoints from %s",
        STATE_NAME);
}

static void
wp_default_metadata_finalize (GObject * object)
{
  WpDefaultMetadata * self = WP_DEFAULT_METADATA (object);

  /* Clear the current timeout callback */
  if (self->timeout_source)
      g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  unload_default_data (self, &self->default_datas[0][WP_DIRECTION_INPUT]);
  unload_default_data (self, &self->default_datas[0][WP_DIRECTION_OUTPUT]);
  unload_default_data (self, &self->default_datas[1][WP_DIRECTION_INPUT]);
  unload_default_data (self, &self->default_datas[1][WP_DIRECTION_OUTPUT]);
  g_clear_object (&self->state);

  G_OBJECT_CLASS (wp_default_metadata_parent_class)->finalize (object);
}

static void
wp_default_metadata_init (WpDefaultMetadata * self)
{
   self->state = wp_state_new (STATE_NAME);
   load_default_data (self, &self->default_datas[0][WP_DIRECTION_INPUT],
       default_audio_node_key (WP_DIRECTION_INPUT));
   load_default_data (self, &self->default_datas[0][WP_DIRECTION_OUTPUT],
       default_audio_node_key (WP_DIRECTION_OUTPUT));
   load_default_data (self, &self->default_datas[1][WP_DIRECTION_INPUT],
       default_endpoint_key (WP_DIRECTION_INPUT));
   load_default_data (self, &self->default_datas[1][WP_DIRECTION_OUTPUT],
       default_endpoint_key (WP_DIRECTION_OUTPUT));
}

static void
wp_default_metadata_class_init (WpDefaultMetadataClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_default_metadata_finalize;

  plugin_class->enable = wp_default_metadata_enable;
  plugin_class->disable = wp_default_metadata_disable;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_default_metadata_get_type (),
          "name", "default-metadata",
          "core", core,
          NULL));
  return TRUE;
}
