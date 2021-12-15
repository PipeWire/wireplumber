/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/keys.h>
#include "module-default-nodes/common.h"

struct _WpDefaultNodesApi
{
  WpPlugin parent;

  gchar *defaults[N_DEFAULT_NODES];
  WpObjectManager *om;
};

enum {
  ACTION_GET_DEFAULT_NODE,
  SIGNAL_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

G_DECLARE_FINAL_TYPE (WpDefaultNodesApi, wp_default_nodes_api,
                      WP, DEFAULT_NODES_API, WpPlugin)
G_DEFINE_TYPE (WpDefaultNodesApi, wp_default_nodes_api, WP_TYPE_PLUGIN)

static void
wp_default_nodes_api_init (WpDefaultNodesApi * self)
{
}

static void
sync_changed_notification (WpCore * core, GAsyncResult * res,
    WpDefaultNodesApi * self)
{
  g_autoptr (GError) error = NULL;
  if (!wp_core_sync_finish (core, res, &error)) {
    wp_warning_object (self, "core sync error: %s", error->message);
    return;
  }

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
  return;
}

static void
schedule_changed_notification (WpDefaultNodesApi *self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);
  wp_core_sync_closure (core, NULL, g_cclosure_new_object (
      G_CALLBACK (sync_changed_notification), G_OBJECT (self)));
}

static void
on_metadata_changed (WpMetadata *m, guint32 subject,
    const gchar *key, const gchar *type, const gchar *value, gpointer d)
{
  WpDefaultNodesApi * self = WP_DEFAULT_NODES_API (d);
  gint node_t = -1;
  gchar name[1024];

  if (subject == 0) {
    for (gint i = 0; i < N_DEFAULT_NODES; i++) {
      if (!g_strcmp0 (key, DEFAULT_KEY[i])) {
        node_t = i;
        break;
      }
    }
  }

  if (node_t != -1) {
    g_clear_pointer (&self->defaults[node_t], g_free);

    if (value && !g_strcmp0 (type, "Spa:String:JSON") &&
        json_object_find (value, "name", name, sizeof(name)) == 0)
    {
      self->defaults[node_t] = g_strdup (name);
    }

    wp_debug_object (m, "changed '%s' -> '%s'", key, self->defaults[node_t]);

    schedule_changed_notification (self);
  }
}

static void
on_metadata_added (WpObjectManager *om, WpObject *obj, WpDefaultNodesApi * self)
{
  if (WP_IS_METADATA (obj)) {
    g_autoptr (WpIterator) it = wp_metadata_new_iterator (WP_METADATA (obj), 0);
    g_auto (GValue) val = G_VALUE_INIT;

    for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      guint32 subject;
      const gchar *key, *type, *value;
      wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
      on_metadata_changed (WP_METADATA (obj), subject, key, type, value, self);
    }

    g_signal_connect_object (obj, "changed",
        G_CALLBACK (on_metadata_changed), self, 0);
  }
}

static void
on_om_installed (WpObjectManager * om, WpDefaultNodesApi * self)
{
  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_default_nodes_api_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpDefaultNodesApi * self = WP_DEFAULT_NODES_API (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);

  /* Create the metadata object manager */
  self->om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->om, WP_TYPE_METADATA,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "default",
      NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_NODE, NULL);
  wp_object_manager_request_object_features (self->om,
      WP_TYPE_METADATA, WP_OBJECT_FEATURES_ALL);
  wp_object_manager_request_object_features (self->om,
      WP_TYPE_NODE, WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  g_signal_connect_object (self->om, "object-added",
      G_CALLBACK (on_metadata_added), self, 0);
  g_signal_connect_object (self->om, "installed",
      G_CALLBACK (on_om_installed), self, 0);
  wp_core_install_object_manager (core, self->om);
}

static void
wp_default_nodes_api_disable (WpPlugin * plugin)
{
  WpDefaultNodesApi * self = WP_DEFAULT_NODES_API (plugin);

  for (guint i = 0; i < N_DEFAULT_NODES; i++)
    g_clear_pointer (&self->defaults[i], g_free);
  g_clear_object (&self->om);
}

static guint
wp_default_nodes_api_get_default_node (WpDefaultNodesApi * self,
    const gchar * media_class)
{
  gint node_t = -1;
  for (gint i = 0; i < N_DEFAULT_NODES; i++) {
    if (!g_strcmp0 (media_class, NODE_TYPE_STR[i])) {
      node_t = i;
      break;
    }
  }

  if (node_t != -1 && self->defaults[node_t]) {
    g_autoptr (WpNode) node = wp_object_manager_lookup (self->om,
        WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY,
        PW_KEY_NODE_NAME, "=s", self->defaults[node_t],
        NULL);
    if (node)
      return wp_proxy_get_bound_id (WP_PROXY (node));
  }
  return SPA_ID_INVALID;
}

static void
wp_default_nodes_api_class_init (WpDefaultNodesApiClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_default_nodes_api_enable;
  plugin_class->disable = wp_default_nodes_api_disable;

  signals[ACTION_GET_DEFAULT_NODE] = g_signal_new_class_handler (
      "get-default-node", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_default_nodes_api_get_default_node,
      NULL, NULL, NULL,
      G_TYPE_UINT, 1, G_TYPE_STRING);

  signals[SIGNAL_CHANGED] = g_signal_new (
      "changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_default_nodes_api_get_type (),
          "name", "default-nodes-api",
          "core", core,
          NULL));
  return TRUE;
}
