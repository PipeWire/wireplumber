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

#define DEFAULT_CONFIG_KEYS 1
#include "module-default-nodes/common.h"

#define NAME "default-nodes"
#define DEFAULT_SAVE_INTERVAL_MS 1000
#define DEFAULT_USE_PERSISTENT_STORAGE TRUE

enum {
  PROP_0,
  PROP_SAVE_INTERVAL_MS,
  PROP_USE_PERSISTENT_STORAGE,
};

typedef struct _WpDefaultNode WpDefaultNode;
struct _WpDefaultNode
{
  gchar *value;
  gchar *config_value;
};

struct _WpDefaultNodes
{
  WpPlugin parent;

  WpState *state;
  WpDefaultNode defaults[N_DEFAULT_NODES];
  WpObjectManager *metadatas_om;
  WpObjectManager *nodes_om;
  GSource *timeout_source;

  /* properties */
  guint save_interval_ms;
  gboolean use_persistent_storage;
};

G_DECLARE_FINAL_TYPE (WpDefaultNodes, wp_default_nodes,
                      WP, DEFAULT_NODES, WpPlugin)
G_DEFINE_TYPE (WpDefaultNodes, wp_default_nodes, WP_TYPE_PLUGIN)

static void
wp_default_nodes_init (WpDefaultNodes * self)
{
}

static void
load_state (WpDefaultNodes * self)
{
  g_autoptr (WpProperties) props = wp_state_load (self->state, NAME);
  if (!props)
    wp_warning_object (self, "could not load " NAME);
  else {
    for (gint i = 0; i < N_DEFAULT_NODES; i++) {
      const gchar *value = wp_properties_get (props, DEFAULT_CONFIG_KEY[i]);
      self->defaults[i].config_value = g_strdup (value);
    }
  }
}

static gboolean
timeout_save_state_callback (gpointer data)
{
  WpDefaultNodes *self = data;
  g_autoptr (WpProperties) props = wp_properties_new_empty ();

  for (gint i = 0; i < N_DEFAULT_NODES; i++) {
    if (self->defaults[i].config_value)
      wp_properties_set (props, DEFAULT_CONFIG_KEY[i],
          self->defaults[i].config_value);
  }

  if (!wp_state_save (self->state, NAME, props))
    wp_warning_object (self, "could not save " NAME);

  g_clear_pointer (&self->timeout_source, g_source_unref);
  return G_SOURCE_REMOVE;
}

static void
timer_start (WpDefaultNodes *self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  if (self->timeout_source || !self->use_persistent_storage)
    return;

  /* Add the timeout callback */
  wp_core_timeout_add (core, &self->timeout_source, self->save_interval_ms,
      timeout_save_state_callback, self, NULL);
}

static WpNode *
find_highest_priority_node (WpDefaultNodes * self, gint node_t)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  gint highest_prio = 0;
  WpNode *res = NULL;

  it = wp_object_manager_new_filtered_iterator (self->nodes_om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_MEDIA_CLASS, "=s", MEDIA_CLASS[node_t],
      NULL);

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpNode *node = g_value_get_object (&val);
    const gchar *prio_str = wp_pipewire_object_get_property (
        WP_PIPEWIRE_OBJECT (node), PW_KEY_PRIORITY_SESSION);
    gint prio = prio_str ? atoi (prio_str) : -1;

    if (prio > highest_prio || res == NULL) {
      highest_prio = prio;
      res = node;
    }
  }
  return res ? g_object_ref (res) : NULL;
}

static void
reevaluate_default_node (WpDefaultNodes * self, WpMetadata *m, gint node_t)
{
  g_autoptr (WpNode) node = NULL;
  const gchar *node_name = NULL;
  gchar buf[1024];

  /* Find the configured default node */
  node_name = self->defaults[node_t].config_value;
  if (node_name) {
    node = wp_object_manager_lookup (self->nodes_om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_NAME, "=s", node_name,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_MEDIA_CLASS, "=s", MEDIA_CLASS[node_t],
        NULL);
  }

  /* If not found, get the highest priority one */
  if (!node) {
    node = find_highest_priority_node (self, node_t);
    if (node)
      node_name = wp_pipewire_object_get_property (WP_PIPEWIRE_OBJECT (node),
          PW_KEY_NODE_NAME);
  }

  /* store it in the metadata if it was changed */
  if (node && node_name &&
      g_strcmp0 (node_name, self->defaults[node_t].value) != 0)
  {
    g_free (self->defaults[node_t].value);
    self->defaults[node_t].value = g_strdup (node_name);

    wp_info_object (self, "set default node for %s: %s",
        MEDIA_CLASS[node_t], node_name);

    g_snprintf (buf, sizeof(buf), "{ \"name\": \"%s\" }", node_name);
    wp_metadata_set (m, 0, DEFAULT_KEY[node_t], "Spa:String:JSON", buf);
  }
}

static void
on_metadata_changed (WpMetadata *m, guint32 subject,
    const gchar *key, const gchar *type, const gchar *value, gpointer d)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (d);
  gint node_t = -1;
  gchar name[1024];

  if (subject == 0) {
    for (gint i = 0; i < N_DEFAULT_NODES; i++) {
      if (!g_strcmp0 (key, DEFAULT_CONFIG_KEY[i])) {
        node_t = i;
        break;
      }
    }
  }

  if (node_t != -1) {
    g_clear_pointer (&self->defaults[node_t].config_value, g_free);

    if (value && !g_strcmp0 (type, "Spa:String:JSON") &&
        json_object_find (value, "name", name, sizeof(name)) == 0)
    {
      self->defaults[node_t].config_value = g_strdup (name);
    }

    wp_debug_object (m, "changed '%s' -> '%s'", key,
        self->defaults[node_t].config_value);

    /* re-evaluate the default, taking into account the new configured default;
       block recursive calls to this handler as an optimization */
    g_signal_handlers_block_by_func (m, on_metadata_changed, d);
    reevaluate_default_node (self, m, node_t);
    g_signal_handlers_unblock_by_func (m, on_metadata_changed, d);

    /* Save state after specific interval */
    timer_start (self);
  }
}

static void
on_nodes_changed (WpObjectManager * om, WpDefaultNodes * self)
{
  g_autoptr (WpMetadata) metadata = NULL;

  /* Get the metadata */
  metadata = wp_object_manager_lookup (self->metadatas_om, WP_TYPE_METADATA,
      NULL);
  if (!metadata)
    return;

  wp_trace_object (om, "nodes changed, re-evaluating defaults");
  reevaluate_default_node (self, metadata, AUDIO_SINK);
  reevaluate_default_node (self, metadata, AUDIO_SOURCE);
  reevaluate_default_node (self, metadata, VIDEO_SOURCE);
}

static void
on_metadata_added (WpObjectManager *om, WpMetadata *metadata, gpointer d)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (d);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  /* Handle the changed signal */
  g_signal_connect_object (metadata, "changed",
      G_CALLBACK (on_metadata_changed), self, 0);

  /* Create the nodes object manager */
  self->nodes_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->nodes_om, WP_TYPE_NODE, NULL);
  wp_object_manager_request_object_features (self->nodes_om, WP_TYPE_NODE,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  g_signal_connect_object (self->nodes_om, "objects-changed",
      G_CALLBACK (on_nodes_changed), self, 0);
  wp_core_install_object_manager (core, self->nodes_om);
}

static void
wp_default_nodes_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);

  if (self->use_persistent_storage) {
    self->state = wp_state_new (NAME);
    load_state (self);
  }

  /* Create the metadatas object manager */
  self->metadatas_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->metadatas_om, WP_TYPE_METADATA,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "default",
      NULL);
  wp_object_manager_request_object_features (self->metadatas_om,
      WP_TYPE_METADATA, WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->metadatas_om, "object-added",
      G_CALLBACK (on_metadata_added), self, 0);
  wp_core_install_object_manager (core, self->metadatas_om);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_default_nodes_disable (WpPlugin * plugin)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (plugin);

  /* Clear the current timeout callback */
  if (self->timeout_source)
      g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  g_clear_object (&self->metadatas_om);
  g_clear_object (&self->nodes_om);
  g_clear_object (&self->state);
}

static void
wp_default_nodes_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpDefaultNodes * self = WP_DEFAULT_NODES (object);

  switch (property_id) {
  case PROP_SAVE_INTERVAL_MS:
    self->save_interval_ms = g_value_get_uint (value);
    break;
  case PROP_USE_PERSISTENT_STORAGE:
    self->use_persistent_storage =  g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_default_nodes_class_init (WpDefaultNodesClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->set_property = wp_default_nodes_set_property;

  plugin_class->enable = wp_default_nodes_enable;
  plugin_class->disable = wp_default_nodes_disable;

  g_object_class_install_property (object_class, PROP_SAVE_INTERVAL_MS,
      g_param_spec_uint ("save-interval-ms", "save-interval-ms",
          "save-interval-ms", 1, G_MAXUINT32, DEFAULT_SAVE_INTERVAL_MS,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_USE_PERSISTENT_STORAGE,
      g_param_spec_boolean ("use-persistent-storage", "use-persistent-storage",
          "use-persistent-storage", DEFAULT_USE_PERSISTENT_STORAGE,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  guint save_interval_ms = DEFAULT_SAVE_INTERVAL_MS;
  gboolean use_persistent_storage = DEFAULT_USE_PERSISTENT_STORAGE;

  if (args) {
    g_variant_lookup (args, "save-interval-ms", "u", &save_interval_ms);
    g_variant_lookup (args, "use-persistent-storage", "b", &save_interval_ms);
  }

  wp_plugin_register (g_object_new (wp_default_nodes_get_type (),
          "name", NAME,
          "core", core,
          "save-interval-ms", save_interval_ms,
          "use-persistent-storage", use_persistent_storage,
          NULL));
  return TRUE;
}
