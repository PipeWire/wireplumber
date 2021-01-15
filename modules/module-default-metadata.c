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
  "default.audio.sink" : "default.audio.source")

G_DECLARE_FINAL_TYPE (WpDefaultMetadata, wp_default_metadata, WP,
    DEFAULT_METADATA, WpPlugin)

struct _WpDefaultEndpoints
{
  WpDefaultMetadata *self;
  gchar *group;
  WpProperties *props;
};
typedef struct _WpDefaultEndpoints WpDefaultEndpoints;

struct _WpDefaultMetadata
{
  WpPlugin parent;
  WpState *state;
  WpDefaultEndpoints default_endpoints[2];
  WpObjectManager *metadatas_om;
  WpObjectManager *sessions_om;
  guint metadata_id;
  GSource *timeout_source;
};

G_DEFINE_TYPE (WpDefaultMetadata, wp_default_metadata, WP_TYPE_PLUGIN)

static gboolean
timeout_save_callback (gpointer p)
{
  WpDefaultEndpoints *d = p;
  WpDefaultMetadata *self = d->self;

  if (!wp_state_save (self->state, d->group, d->props))
    wp_warning_object (self, "could not save default endpoints in %s",
        STATE_NAME);

  return G_SOURCE_REMOVE;
}

static void
timeout_save_default_endpoints (WpDefaultMetadata *self, guint dir, guint ms)
{
  g_autoptr (WpCore) core = wp_plugin_get_core (WP_PLUGIN (self));
  g_return_if_fail (core);

  /* Clear the current timeout callback */
  if (self->timeout_source)
    g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  /* Add the timeout callback */
  wp_core_timeout_add (core, &self->timeout_source, ms, timeout_save_callback,
      self->default_endpoints + dir, NULL);
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

static WpEndpoint *
find_endpoint_with_node_id (WpDefaultMetadata * self, guint node_id,
    WpSession **session)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) value = G_VALUE_INIT;

  it = wp_object_manager_iterate (self->sessions_om);
  for (; wp_iterator_next (it, &value); g_value_unset (&value)) {
    WpSession *s = g_value_get_object (&value);
    g_autoptr (WpEndpoint) ep = NULL;
    ep = wp_session_lookup_endpoint (s, WP_CONSTRAINT_TYPE_PW_PROPERTY,
        PW_KEY_NODE_ID, "=u", node_id, NULL);
    if (ep) {
      if (session)
        *session = g_object_ref (s);
      return g_object_ref (ep);
    }
  }

  return NULL;
}

static void
on_default_metadata_changed (WpMetadata *m, guint32 subject,
    const gchar *key, const gchar *type, const gchar *value, gpointer *d)
{
  WpDefaultMetadata * self = WP_DEFAULT_METADATA (d);
  g_autoptr (WpSession) session = NULL;
  g_autoptr (WpEndpoint) ep = NULL;
  const gchar *session_name = NULL, *ep_name = NULL;
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

  /* Get the edpoint and session */
  ep = is_default_ep ?
      find_endpoint_with_endpoint_id (self, subject, atoi (value), &session) :
      find_endpoint_with_node_id (self, atoi (value), &session);
  if (!ep || !session)
    return;

  /* Update the default node when default endpoint changes, and vice versa */
  g_signal_handlers_block_by_func (m, on_default_metadata_changed, self);
  if (is_default_ep) {
    const gchar *n_id = NULL, *mc = NULL;
    n_id = wp_pipewire_object_get_property (WP_PIPEWIRE_OBJECT (ep),
        PW_KEY_NODE_ID);
    mc = wp_endpoint_get_media_class (ep);
    if (n_id && g_str_has_prefix (mc, "Audio/"))
      wp_metadata_set (m, 0, default_audio_node_key (dir), "Spa:Int", n_id);
  } else {
    g_autofree gchar *v = g_strdup_printf ("%d",
        wp_proxy_get_bound_id (WP_PROXY (ep)));
    wp_metadata_set (m, wp_proxy_get_bound_id (WP_PROXY (session)),
        default_endpoint_key (dir), "Spa:Int", v);
  }
  g_signal_handlers_unblock_by_func (m, on_default_metadata_changed, self);

  /* Get the session name and endpoint name */
  session_name = wp_session_get_name (session);
  g_return_if_fail (session_name);
  ep_name = wp_endpoint_get_name (ep);
  g_return_if_fail (ep_name);

  /* Set the property and save state */
  wp_properties_set (self->default_endpoints[dir].props, session_name, ep_name);
  timeout_save_default_endpoints (self, dir, SAVE_INTERVAL_MS);
}

static WpEndpoint *
find_highest_priority_endpoint (WpSession * session, WpDirection dir)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  gint highest_prio = 0;
  WpEndpoint *res = NULL;

  it = wp_session_iterate_endpoints_filtered (session,
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
  const gchar *session_name = NULL, *ep_name = NULL, *n_id = NULL, *mc = NULL;
  guint ep_id = 0;

  g_return_if_fail (m);
  g_return_if_fail (self->default_endpoints[dir].props);

  /* Find the default endpoint */
  session_name = wp_session_get_name (session);
  ep_name = wp_properties_get (self->default_endpoints[dir].props, session_name);
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
    n_id = wp_pipewire_object_get_property (WP_PIPEWIRE_OBJECT (ep),
        PW_KEY_NODE_ID);
    mc = wp_endpoint_get_media_class (ep);

    /* block the signal to avoid storing this; only selections done by the user
     * should be stored */
    g_signal_handlers_block_by_func (m, on_default_metadata_changed, self);

    /* Set default endpoint */
    g_autofree gchar *value = g_strdup_printf ("%d", ep_id);
    wp_metadata_set (m, wp_proxy_get_bound_id (WP_PROXY (session)),
        default_endpoint_key (dir), "Spa:Int", value);

    /* Also set the default node if audio endpoint and node Id is present */
    if (n_id && g_str_has_prefix (mc, "Audio/"))
      wp_metadata_set (m, 0, default_audio_node_key (dir), "Spa:Int", n_id);

    g_signal_handlers_unblock_by_func (m, on_default_metadata_changed, self);

    wp_info_object (self, "set default %s endpoint with id %d on session '%s'",
        direction_to_dbg_string (dir), ep_id, session_name);
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
on_metadata_added (WpObjectManager *om, WpMetadata *metadata, gpointer d)
{
  WpDefaultMetadata * self = WP_DEFAULT_METADATA (d);
  g_autoptr (WpCore) core = wp_plugin_get_core (WP_PLUGIN (self));

  g_return_if_fail (core);

  /* Only handle the first available metadata and skip the rest */
  if (self->metadata_id > 0)
    return;
  self->metadata_id = wp_proxy_get_bound_id (WP_PROXY (metadata));

  /* Handle the changed signal */
  g_signal_connect_object (metadata, "changed",
      G_CALLBACK (on_default_metadata_changed), self, 0);

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
wp_default_metadata_activate (WpPlugin * plugin)
{
  WpDefaultMetadata * self = WP_DEFAULT_METADATA (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);

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
}

static void
wp_default_metadata_deactivate (WpPlugin * plugin)
{
  WpDefaultMetadata * self = WP_DEFAULT_METADATA (plugin);

  g_clear_object (&self->metadatas_om);
  g_clear_object (&self->sessions_om);
}

static void
unload_default_endpoints (WpDefaultMetadata * self, WpDefaultEndpoints * d)
{
  g_clear_pointer (&d->props, wp_properties_unref);
  g_clear_pointer (&d->group, g_free);
  d->self = NULL;
}

static void
load_default_endpoints (WpDefaultMetadata * self, WpDefaultEndpoints * d,
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

  unload_default_endpoints (self, self->default_endpoints + WP_DIRECTION_INPUT);
  unload_default_endpoints (self, self->default_endpoints + WP_DIRECTION_OUTPUT);
  g_clear_object (&self->state);

  G_OBJECT_CLASS (wp_default_metadata_parent_class)->finalize (object);
}

static void
wp_default_metadata_init (WpDefaultMetadata * self)
{
   self->state = wp_state_new (STATE_NAME);
   load_default_endpoints (self, self->default_endpoints + WP_DIRECTION_INPUT,
       default_endpoint_key (WP_DIRECTION_INPUT));
   load_default_endpoints (self, self->default_endpoints + WP_DIRECTION_OUTPUT,
       default_endpoint_key (WP_DIRECTION_OUTPUT));
}

static void
wp_default_metadata_class_init (WpDefaultMetadataClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_default_metadata_finalize;

  plugin_class->activate = wp_default_metadata_activate;
  plugin_class->deactivate = wp_default_metadata_deactivate;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  wp_plugin_register (g_object_new (wp_default_metadata_get_type (),
          "name", "default-metadata",
          "module", module,
          NULL));
}
