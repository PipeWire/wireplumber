/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <errno.h>

#define STATE_NAME "default-metadata"
#define SAVE_INTERVAL_MS 1000

#define direction_to_dbg_string(dir) \
  ((dir == WP_DIRECTION_INPUT) ? "sink" : "source")

#define default_endpoint_key(dir) ((dir == WP_DIRECTION_INPUT) ? \
  "default.session.endpoint.sink" : "default.session.endpoint.source")

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

static void
on_default_metadata_changed (WpMetadata *m, guint32 subject,
    const gchar *key, const gchar *type, const gchar *value, gpointer *d)
{
  WpDefaultMetadata * self = WP_DEFAULT_METADATA (d);
  g_autoptr (WpSession) session = NULL;
  g_autoptr (WpEndpoint) ep = NULL;
  const gchar *session_name = NULL, *ep_name = NULL;
  guint dir = WP_DIRECTION_INPUT;

  /* Get the direction */
  if (!g_strcmp0 (key, default_endpoint_key (WP_DIRECTION_INPUT)))
    dir = WP_DIRECTION_INPUT;
  else if (!g_strcmp0 (key, default_endpoint_key (WP_DIRECTION_OUTPUT)))
    dir = WP_DIRECTION_OUTPUT;
  else
    return;

  /* Find the session */
  session = wp_object_manager_lookup (self->sessions_om, WP_TYPE_SESSION,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", subject, NULL);
  if (!session)
    return;

  /* Find the endpoint */
  ep = wp_session_lookup_endpoint (session, WP_CONSTRAINT_TYPE_G_PROPERTY,
      "bound-id", "=u", atoi (value), NULL);
  if (!ep)
    return;

  /* Get the session name and endpoint name */
  session_name = wp_session_get_name (session);
  g_return_if_fail (session_name);
  ep_name = wp_endpoint_get_name (ep);
  g_return_if_fail (ep_name);

  /* Set the property and save state */
  wp_properties_set (self->default_endpoints[dir].props, session_name, ep_name);
  timeout_save_default_endpoints (self, dir, SAVE_INTERVAL_MS);
}

static guint32
find_highest_prio (WpSession * session, WpDirection dir)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  gint highest_prio = 0;
  guint32 id = 0;

  it = wp_session_iterate_endpoints_filtered (session,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s",
      (dir == WP_DIRECTION_INPUT) ? "*/Sink" : "*/Source",
      NULL);

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpProxy *ep = g_value_get_object (&val);
    const gchar *prio_str = wp_pipewire_object_get_property (
        WP_PIPEWIRE_OBJECT (ep), "endpoint.priority");
    gint prio = atoi (prio_str);

    if (prio > highest_prio || id == 0) {
      highest_prio = prio;
      id = wp_proxy_get_bound_id (ep);
    }
  }
  return id;
}

static void
reevaluate_default_endpoints (WpDefaultMetadata * self, WpMetadata *m,
    WpSession *session, guint dir)
{
  guint32 ep_id = 0;
  const gchar *session_name = NULL, *ep_name = NULL;

  g_return_if_fail (m);
  g_return_if_fail (self->default_endpoints[dir].props);

  /* Find the default endpoint */
  session_name = wp_session_get_name (session);
  ep_name = wp_properties_get (self->default_endpoints[dir].props, session_name);
  if (ep_name) {
    g_autoptr (WpEndpoint) ep = wp_session_lookup_endpoint (session,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "endpoint.name", "=s", ep_name,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s",
        (dir == WP_DIRECTION_INPUT) ? "*/Sink" : "*/Source", NULL);
    if (ep)
      ep_id = wp_proxy_get_bound_id (WP_PROXY (ep));
  }

  /* If not found, use the highest priority one */
  if (ep_id == 0)
    ep_id = find_highest_prio (session, dir);

  if (ep_id != 0) {
    /* block the signal to avoid storing this; only selections done by the user
     * should be stored */
    g_autofree gchar *value = g_strdup_printf ("%d", ep_id);
    g_signal_handlers_block_by_func (m, on_default_metadata_changed, self);
    wp_metadata_set (m, wp_proxy_get_bound_id (WP_PROXY (session)),
        default_endpoint_key (dir), "Spa:Int", value);
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
