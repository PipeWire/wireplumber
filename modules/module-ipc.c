/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wpipc/wpipc.h>
#include <wp/wp.h>

#define SERVER_SUSPEND_REQUEST_NAME "SUSPEND"
#define SERVER_RESUME_REQUEST_NAME "RESUME"
#define METADATA_KEY "suspend.playback"

enum {
  PROP_0,
  PROP_PATH,
};

struct _WpIpcPlugin
{
  WpPlugin parent;
  gchar *path;
  GHashTable *suspended_clients;
  struct wpipc_server *server;
  WpObjectManager *metadatas_om;
};

G_DECLARE_FINAL_TYPE (WpIpcPlugin, wp_ipc_plugin,
                      WP, IPC_PLUGIN, WpPlugin)
G_DEFINE_TYPE (WpIpcPlugin, wp_ipc_plugin, WP_TYPE_PLUGIN)

struct idle_data {
  WpIpcPlugin *self;
  char *request_name;
  int client_id;
};

static struct idle_data *
idle_data_new (WpIpcPlugin *self, const char *request_name, int client_id)
{
  struct idle_data *data = NULL;

  g_return_val_if_fail (self, NULL);
  g_return_val_if_fail (request_name, NULL);

  data = g_new0 (struct idle_data, 1);
  data->self = g_object_ref (self);
  data->request_name = g_strdup (request_name);
  data->client_id = client_id;

  return data;
}

static void
idle_data_free (gpointer p)
{
  struct idle_data *data = p;

  g_return_if_fail (data);
  g_return_if_fail (data->self);
  g_return_if_fail (data->request_name);

  g_free (data->request_name);
  g_object_unref (data->self);
  g_free (data);
}

static void
wp_ipc_plugin_init (WpIpcPlugin * self)
{
}

static void
wp_ipc_plugin_set_metadata (WpIpcPlugin * self, gboolean suspend) {
  g_autoptr (WpMetadata) metadata = NULL;

  metadata = wp_object_manager_lookup (self->metadatas_om, WP_TYPE_METADATA,
      NULL);
  if (!metadata) {
    wp_warning_object (self, "could not find default metadata");
    return;
  }

  wp_info_object (self, METADATA_KEY " metadata set to %d", suspend);
  wp_metadata_set (metadata, 0, METADATA_KEY, "Spa:Bool", suspend ? "1" : "0");
}

static gboolean
idle_request_handler (struct idle_data *data)
{
  WpIpcPlugin *self = data->self;
  gpointer key = GINT_TO_POINTER (data->client_id);

  /* Suspend */
  if (g_strcmp0 (data->request_name, SERVER_SUSPEND_REQUEST_NAME) == 0) {
    if (!g_hash_table_contains (self->suspended_clients, key))
      g_hash_table_insert (self->suspended_clients, key, NULL);
    if (g_hash_table_size (self->suspended_clients) == 1)
      wp_ipc_plugin_set_metadata (self, TRUE);
  }

  /* Resume */
  else if (g_strcmp0 (data->request_name, SERVER_RESUME_REQUEST_NAME) == 0) {
    if (g_hash_table_contains (self->suspended_clients, key))
      g_hash_table_remove (self->suspended_clients, key);
    if (g_hash_table_size (self->suspended_clients) == 0)
      wp_ipc_plugin_set_metadata (self, FALSE);
  }

  return G_SOURCE_REMOVE;
}

static bool
request_handler (struct wpipc_server *s, int client_fd,
    const char *name, const struct spa_pod *args, void *data)
{
  WpIpcPlugin * self = data;
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  if (!core)
    return wpipc_server_reply_error (s, client_fd, "core not valid");

  wp_core_idle_add (core, NULL, (GSourceFunc) idle_request_handler,
      idle_data_new (self, name, client_fd), idle_data_free);
  return wpipc_server_reply_ok (s, client_fd, NULL);
}

static void
client_handler (struct wpipc_server *s, int client_fd,
    enum wpipc_receiver_sender_state client_state, void *data)
{
  WpIpcPlugin * self = data;

  switch (client_state) {
    case WPIPC_RECEIVER_SENDER_STATE_CONNECTED:
      wp_info_object (self, "client connected %d", client_fd);
      break;
    case WPIPC_RECEIVER_SENDER_STATE_DISCONNECTED: {
      g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
      if (core)
        wp_core_idle_add (core, NULL, (GSourceFunc) idle_request_handler,
            idle_data_new (self, SERVER_RESUME_REQUEST_NAME, client_fd),
            idle_data_free);
      wp_info_object (self, "client disconnected %d", client_fd);
      break;
    }
    default:
      break;
  }
}

static void
wp_ipc_plugin_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpIpcPlugin * self = WP_IPC_PLUGIN (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);

  g_return_if_fail (self->path);

  /* Init suspended clients table */
  self->suspended_clients = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, NULL);

  /* Create the IPC server, and handle PLAY and STOP requests */
  self->server = wpipc_server_new (self->path, TRUE);
  g_return_if_fail (self->server);
  wpipc_server_set_client_handler (self->server, client_handler, self);
  wpipc_server_set_request_handler (self->server, SERVER_SUSPEND_REQUEST_NAME,
      request_handler, self);
  wpipc_server_set_request_handler (self->server, SERVER_RESUME_REQUEST_NAME,
      request_handler, self);

  /* Create the metadatas object manager */
  self->metadatas_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->metadatas_om, WP_TYPE_METADATA,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "default",
      NULL);
  wp_object_manager_request_object_features (self->metadatas_om,
      WP_TYPE_METADATA, WP_OBJECT_FEATURES_ALL);
  wp_core_install_object_manager (core, self->metadatas_om);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_ipc_plugin_disable (WpPlugin * plugin)
{
  WpIpcPlugin * self = WP_IPC_PLUGIN (plugin);

  g_clear_object (&self->metadatas_om);
  g_clear_pointer (&self->server, wpipc_server_free);
  g_clear_pointer (&self->suspended_clients, g_hash_table_unref);
}

static void
wp_ipc_plugin_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpIpcPlugin * self = WP_IPC_PLUGIN (object);

  switch (property_id) {
  case PROP_PATH:
    g_clear_pointer (&self->path, g_free);
    self->path = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_ipc_plugin_finalize (GObject * object)
{
  WpIpcPlugin * self = WP_IPC_PLUGIN (object);

  g_clear_pointer (&self->path, g_free);

  G_OBJECT_CLASS (wp_ipc_plugin_parent_class)->finalize (object);
}

static void
wp_ipc_plugin_class_init (WpIpcPluginClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_ipc_plugin_enable;
  plugin_class->disable = wp_ipc_plugin_disable;

  object_class->finalize = wp_ipc_plugin_finalize;
  object_class->set_property = wp_ipc_plugin_set_property;

  g_object_class_install_property (object_class, PROP_PATH,
      g_param_spec_string ("path", "path",
          "The path of the IPC server", NULL,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  const gchar *path = NULL;

  if (!g_variant_lookup (args, "path", "s", &path)) {
    wp_warning_object (core, "cannot load IPC module without path argument");
    return FALSE;
  }

  wp_plugin_register (g_object_new (wp_ipc_plugin_get_type (),
      "name", "ipc",
      "core", core,
      "path", path,
      NULL));
  return TRUE;
}
