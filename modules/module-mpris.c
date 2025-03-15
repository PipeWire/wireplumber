/* WirePlumber
 *
-- Copyright © 2025 Pauli Virtanen
--    @author Pauli Virtanen <pav@iki.fi>
 *
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>

#include <glib.h>
#include <glib-unix.h>

#include <wp/wp.h>

#include "dbus-connection-state.h"
#include "flatpak-utils.h"


WP_DEFINE_LOCAL_LOG_TOPIC ("m-mpris")

#define NAME "mpris"

#define PLAYER_TIMEOUT_MSEC	3000

struct _Item
{
  gchar *desktop_entry;
  guint32 pid;
  gchar *flatpak_app_id;
  gchar *flatpak_instance_id;
};
typedef struct _Item Item;

struct _Players
{
  grefcount rc;
  GMutex lock;
  GHashTable *items;
  GCancellable *cancellable;
  GDBusConnection *conn;
};
typedef struct _Players Players;

struct _ItemUpdate {
  Players *players;
  gchar *bus_name;
};
typedef struct _ItemUpdate ItemUpdate;

struct _WpMprisPlugin
{
  WpPlugin parent;
  WpPlugin *dbus;
  GDBusConnection *conn;
  guint name_signal;
  Players *players;
};

struct _WpMprisPluginOperation
{
  GObject parent;
  GDBusConnection *conn;
  const char *name;
  gint result;
};

enum {
  ACTION_GET_PLAYERS,
  ACTION_PAUSE,
  ACTION_MATCH_PID,
  N_SIGNALS
};

enum {
  PROP_0,
  PROP_RESULT
};

static guint signals[N_SIGNALS] = {0};

G_DECLARE_FINAL_TYPE (WpMprisPlugin, wp_mpris_plugin, WP, MPRIS_PLUGIN, WpPlugin)
G_DEFINE_TYPE (WpMprisPlugin, wp_mpris_plugin, WP_TYPE_PLUGIN)

#define WP_TYPE_MPRIS_PLUGIN_OPERATION	wp_mpris_plugin_operation_get_type ()
G_DECLARE_FINAL_TYPE (WpMprisPluginOperation, wp_mpris_plugin_operation, WP, MPRIS_PLUGIN_OPERATION, GObject)
G_DEFINE_TYPE (WpMprisPluginOperation, wp_mpris_plugin_operation, G_TYPE_OBJECT)


/*
 * Media Player items
 *
 * Since GDBus callbacks may be issued "late", use separate refcounted object.
 * Although everything likely runs from main context, add locking to be sure.
 */

static void item_free (gpointer data)
{
  Item *item = data;

  free(item->desktop_entry);
  free(item);
}

static Players *players_new (GDBusConnection *conn)
{
  Players *players;

  players = g_new0 (Players, 1);
  g_ref_count_init(&players->rc);
  g_mutex_init(&players->lock);
  players->items = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, item_free);
  players->cancellable = g_cancellable_new();
  players->conn = g_object_ref (conn);

  return players;
}

static Players *players_ref (Players *players)
{
  g_ref_count_inc(&players->rc);
  return players;
}

static void players_unref (Players *players)
{
  if (!g_ref_count_dec(&players->rc))
    return;

  g_mutex_clear (&players->lock);
  g_clear_object (&players->items);
  g_clear_object (&players->conn);
  g_clear_object (&players->cancellable);
  g_free (players);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Players, players_unref)

static Item *players_ensure_item (Players *players, const gchar *bus_name)
{
  Item *item;

  item = g_hash_table_lookup (players->items, bus_name);
  if (!item) {
    item = g_new0 (Item, 1);
    g_hash_table_insert (players->items, g_strdup (bus_name), item);
  }

  return item;
}

static ItemUpdate *item_update_new (Players *players, const gchar *bus_name)
{
  ItemUpdate *update;

  update = g_new0 (ItemUpdate, 1);
  update->players = players_ref (players);
  update->bus_name = g_strdup (bus_name);
  return update;
}

static void item_update_free (ItemUpdate *update)
{
  players_unref (update->players);
  g_free (update->bus_name);
  g_free (update);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ItemUpdate, item_update_free)

static void item_get_flatpak_app_id (ItemUpdate *update, Item *item)
{
  spa_autofree char *app_id = NULL;
  spa_autofree char *instance_id = NULL;
  int res;

  g_clear_pointer (&item->flatpak_app_id, g_free);
  g_clear_pointer (&item->flatpak_instance_id, g_free);

  if (!item->pid)
    return;

  res = pw_check_flatpak (item->pid, &app_id, &instance_id, NULL);
  if (res < 0) {
    wp_info ("%p: failed to get Flatpak status for '%s': %d (%s)", update->players, update->bus_name,
             -res, spa_strerror (res));
    return;
  }

  if (app_id)
    item->flatpak_app_id = g_strdup (app_id);

  if (instance_id)
    item->flatpak_instance_id = g_strdup (instance_id);

  wp_debug ("%p: player '%s' Flatpak App Id = %s, Instance Id = %s", update->players, update->bus_name,
            item->flatpak_app_id ? item->flatpak_app_id : "-",
            item->flatpak_instance_id ? item->flatpak_instance_id : "-");
}

static void item_pid_cb (GObject *source_object, GAsyncResult* res, gpointer data)
{
  g_autoptr (ItemUpdate) update = data;
  g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&update->players->lock);
  Item *item;
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GError) error = NULL;

  result = g_dbus_connection_call_finish (update->players->conn, res, &error);
  if (!result) {
    wp_info ("%p: failed to get PID for '%s': %s", update->players, update->bus_name, error->message);
    return;
  }

  item = players_ensure_item (update->players, update->bus_name);
  g_variant_get (result, "(u)", &item->pid);

  wp_debug ("%p: player '%s' PID = %u", update->players, update->bus_name, item->pid);

  item_get_flatpak_app_id (update, item);
}

static void item_desktop_entry_cb (GObject *source_object, GAsyncResult* res, gpointer data)
{
  g_autoptr (ItemUpdate) update = data;
  g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&update->players->lock);
  Item *item;
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GVariant) value = NULL;
  g_autoptr (GError) error = NULL;

  result = g_dbus_connection_call_finish (update->players->conn, res, &error);
  if (!result) {
    wp_info ("%p: failed to get DesktopEntry for '%s': %s", update->players, update->bus_name, error->message);
    return;
  }

  g_variant_get (result, "(v)", &value);
  if (!g_str_equal(g_variant_get_type_string (value), "s")) {
    wp_info ("%p: bad value for DesktopEntry for '%s'", update->players, update->bus_name);
    return;
  }

  item = players_ensure_item (update->players, update->bus_name);
  g_clear_pointer (&item->desktop_entry, g_free);
  g_variant_get (value, "s", &item->desktop_entry);

  wp_debug ("%p: player '%s' DesktopEntry = %s", update->players, update->bus_name, item->desktop_entry);
}

static void players_add (Players *players, const gchar *bus_name)
{
  g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&players->lock);
  Item *item;

  if (g_cancellable_is_cancelled (players->cancellable))
    return;

  wp_debug ("%p: add player '%s'", players, bus_name);

  item = players_ensure_item (players, bus_name);
  g_clear_pointer (&item->desktop_entry, g_free);
  item->pid = 0;

  g_dbus_connection_call (players->conn,
                          "org.freedesktop.DBus", "/org/freedesktop/DBus",
                          "org.freedesktop.DBus", "GetConnectionUnixProcessID",
                          g_variant_new ("(s)", bus_name), G_VARIANT_TYPE ("(u)"),
                          G_DBUS_CALL_FLAGS_NO_AUTO_START, PLAYER_TIMEOUT_MSEC,
                          players->cancellable, item_pid_cb, item_update_new (players, bus_name));

  g_dbus_connection_call (players->conn,
                          bus_name, "/org/mpris/MediaPlayer2",
                          "org.freedesktop.DBus.Properties", "Get",
                          g_variant_new ("(ss)", "org.mpris.MediaPlayer2", "DesktopEntry"), G_VARIANT_TYPE ("(v)"),
                          G_DBUS_CALL_FLAGS_NO_AUTO_START, PLAYER_TIMEOUT_MSEC,
                          players->cancellable, item_desktop_entry_cb, item_update_new (players, bus_name));
}

static void players_remove (Players *players, const gchar *bus_name)
{
  g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&players->lock);

  wp_debug ("%p: remove player '%s'", players, bus_name);
  g_hash_table_remove (players->items, bus_name);
}


/*
 * Media Player monitoring
 */

static void on_name_owner_changed(GDBusConnection* connection,
                                  const gchar* sender_name,
                                  const gchar* object_path,
                                  const gchar* interface_name,
                                  const gchar* signal_name,
                                  GVariant* parameters,
                                  gpointer user_data)
{
  Players *players = user_data;
  const gchar *bus_name;
  const gchar *old_owner;
  const gchar *new_owner;

  g_variant_get (parameters, "(&s&s&s)", &bus_name, &old_owner, &new_owner);

  if (!g_str_has_prefix (bus_name, "org.mpris.MediaPlayer2."))
    return;

  if (strlen (new_owner) == 0)
    players_remove (players, bus_name);
  else
    players_add (players, bus_name);
}

static void list_names_cb (GObject *source_object, GAsyncResult* res, gpointer data)
{
  g_autoptr (Players) players = data;
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GError) error = NULL;
  GVariantIter *iter;
  const gchar *bus_name;

  result = g_dbus_connection_call_finish (players->conn, res, &error);
  if (!result) {
    wp_info ("%p: failed to ListNames: %s", players, error->message);
    return;
  }

  g_variant_get (result, "(as)", &iter);
  while (g_variant_iter_loop (iter, "&s", &bus_name)) {
    if (!g_str_has_prefix (bus_name, "org.mpris.MediaPlayer2."))
      continue;

    players_add (players, bus_name);
  }
  g_variant_iter_free (iter);
}

static void do_list_names (Players *players)
{
  g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&players->lock);

  if (g_cancellable_is_cancelled (players->cancellable))
    return;

  g_dbus_connection_call (players->conn,
                          "org.freedesktop.DBus", "/org/freedesktop/DBus",
                          "org.freedesktop.DBus", "ListNames",
                          NULL, G_VARIANT_TYPE ("(as)"),
                          G_DBUS_CALL_FLAGS_NO_AUTO_START, -1,
                          players->cancellable, list_names_cb, players_ref (players));
}

static void clear_state (WpMprisPlugin *self)
{
  if (self->conn) {
    if (self->name_signal) {
      g_dbus_connection_signal_unsubscribe (self->conn, self->name_signal);
      self->name_signal = 0;
    }
    g_clear_object (&self->conn);
  }

  if (self->players) {
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&self->players->lock);

    g_cancellable_cancel (self->players->cancellable);
  }
  g_clear_pointer (&self->players, players_unref);
}

static void
on_dbus_state_changed (GObject * dbus, GParamSpec * spec,
    WpMprisPlugin *self)
{
  WpDBusConnectionState state = -1;
  g_object_get (dbus, "state", &state, NULL);

  switch (state) {
    case WP_DBUS_CONNECTION_STATE_CONNECTED: {
      g_autoptr (GDBusConnection) conn = NULL;

      g_object_get (dbus, "connection", &conn, NULL);
      g_return_if_fail (conn);
      g_return_if_fail (!self->conn);
      g_return_if_fail (!self->players);
      g_return_if_fail (!self->name_signal);

      self->players = players_new (conn);
      self->conn = g_object_ref (conn);
      self->name_signal = g_dbus_connection_signal_subscribe (conn,
          "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
          "/org/freedesktop/DBus", "org.mpris.MediaPlayer2",
          G_DBUS_SIGNAL_FLAGS_MATCH_ARG0_NAMESPACE, on_name_owner_changed,
          players_ref (self->players), (GDestroyNotify) players_unref);

      do_list_names (self->players);
      break;
    }

    case WP_DBUS_CONNECTION_STATE_CONNECTING:
    case WP_DBUS_CONNECTION_STATE_CLOSED:
      clear_state (self);
      break;

    default:
      g_assert_not_reached ();
  }
}


/*
 * WpMprisPluginOperation
 */

static void
wp_mpris_plugin_operation_init (WpMprisPluginOperation * self)
{
}

static void
wp_mpris_plugin_operation_finalize (GObject *object)
{
  WpMprisPluginOperation *self = WP_MPRIS_PLUGIN_OPERATION (object);

  g_clear_object (&self->conn);
}

static void
wp_mpris_plugin_operation_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpMprisPluginOperation *self = WP_MPRIS_PLUGIN_OPERATION (object);

  switch (property_id) {
  case PROP_RESULT:
    g_value_set_int (value, self->result);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_mpris_plugin_operation_class_init (WpMprisPluginOperationClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_mpris_plugin_operation_finalize;
  object_class->get_property = wp_mpris_plugin_operation_get_property;

  g_object_class_install_property (object_class, PROP_RESULT,
      g_param_spec_int ("result", "result",
          "Result from the operation (0 if not completed)", G_MININT, G_MAXINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static WpMprisPluginOperation *
wp_mpris_plugin_operation_new (GDBusConnection *conn, const gchar *name)
{
  WpMprisPluginOperation * self;

  self = g_object_new (WP_TYPE_MPRIS_PLUGIN_OPERATION, NULL);
  self->name = name;
  if (conn)
    self->conn = g_object_ref (conn);

  return self;
}

static void
wp_mpris_plugin_operation_complete (WpMprisPluginOperation * self, gint result)
{
  g_return_if_fail(result);
  g_return_if_fail(!self->result);
  self->result = result;
  g_object_notify (G_OBJECT (self), "result");
}


/*
 * WpMprisPlugin
 */

static void
wp_mpris_plugin_init (WpMprisPlugin * self)
{
}

static void
wp_mpris_plugin_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpMprisPlugin *self = WP_MPRIS_PLUGIN (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  g_return_if_fail(!self->dbus);

  self->dbus = wp_plugin_find (core, "dbus-connection");
  if (!self->dbus) {
    wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT,
        "dbus-connection module must be loaded before mpris"));
    return;
  }

  g_signal_connect_object (self->dbus, "notify::state",
       G_CALLBACK (on_dbus_state_changed), self, 0);
  on_dbus_state_changed (G_OBJECT (self->dbus), NULL, self);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_mpris_plugin_disable (WpPlugin * plugin)
{
  WpMprisPlugin *self = WP_MPRIS_PLUGIN (plugin);

  clear_state(self);
  if (self->dbus)
    g_signal_handlers_disconnect_by_data (self->dbus, self);
  g_clear_object(&self->dbus);

  wp_object_update_features (WP_OBJECT (self), 0, WP_PLUGIN_FEATURE_ENABLED);
}

static gpointer
wp_mpris_plugin_get_players (WpMprisPlugin *self)
{
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_ARRAY);

  g_variant_builder_init (&b, G_VARIANT_TYPE ("av"));

  if (self->players) {
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&self->players->lock);
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, self->players->items);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
      const gchar *bus_name = key;
      const Item *item = value;
      g_auto (GVariantDict) dict = G_VARIANT_DICT_INIT (NULL);

      g_variant_dict_insert (&dict, "name", "s", bus_name);
      if (item->pid)
        g_variant_dict_insert (&dict, "pid", "u", item->pid);
      if (item->desktop_entry)
        g_variant_dict_insert (&dict, "desktop-entry", "s", item->desktop_entry);
      if (item->flatpak_app_id)
        g_variant_dict_insert (&dict, "flatpak-app-id", "s", item->flatpak_app_id);
      if (item->flatpak_instance_id)
        g_variant_dict_insert (&dict, "flatpak-instance-id", "s", item->flatpak_instance_id);

      g_variant_builder_add (&b, "v", g_variant_dict_end (&dict));
    }
  }

  return g_variant_builder_end (&b);
}

static void operation_complete (GObject *source_object, GAsyncResult* res, gpointer data)
{
  g_autoptr (WpMprisPluginOperation) op = data;
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GError) error = NULL;

  result = g_dbus_connection_call_finish (op->conn, res, &error);
  if (!result) {
    wp_info ("operation %s failed: %s", op->name ? op->name : "<?>", error->message);
    wp_mpris_plugin_operation_complete (op, -EIO);
    return;
  }

  wp_mpris_plugin_operation_complete (op, 1);
}

static WpMprisPluginOperation *
wp_mpris_plugin_pause (WpMprisPlugin *self, const gchar *bus_name)
{
  g_autoptr (GVariant) result = NULL;
  WpMprisPluginOperation *op;

  op = wp_mpris_plugin_operation_new (self->conn, "Pause");

  if (!self->conn) {
    wp_mpris_plugin_operation_complete (op, -EIO);
    return op;
  }

  g_dbus_connection_call (self->conn,
      bus_name, "/org/mpris/MediaPlayer2",
      "org.mpris.MediaPlayer2.Player", "Pause",
      NULL, NULL, G_DBUS_CALL_FLAGS_NONE, PLAYER_TIMEOUT_MSEC,
      NULL, operation_complete, g_object_ref (op));

  return op;
}

static pid_t get_parent_pid(pid_t pid)
{
  g_autofree gchar *path = NULL;
  g_autofree gchar *stat = NULL;
  int ppid;

  path = g_strdup_printf ("/proc/%d/stat", (int)pid);
  if (!g_file_get_contents (path, &stat, NULL, NULL))
    return 0;

  if (sscanf (stat, "%*d %*s %*s %d", &ppid) == 1)
    return ppid;

  return 0;
}

static gboolean
match_pid(pid_t parent, pid_t child)
{
  pid_t p = child;
  int j;

  for (j = 0; j < 100 && p > 1; ++j, p = get_parent_pid(p)) {
    if (parent == p) {
      wp_trace ("matched pid: %d is %d-child of %d", (int)child, j, (int)parent);
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean
wp_mpris_plugin_match_pid (WpMprisPlugin *self, gint parent, gint child)
{
  return match_pid(parent, child);
}

static void
wp_mpris_plugin_class_init (WpMprisPluginClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_mpris_plugin_enable;
  plugin_class->disable = wp_mpris_plugin_disable;

  signals[ACTION_GET_PLAYERS] = g_signal_new_class_handler (
      "get-players", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_mpris_plugin_get_players,
      NULL, NULL, NULL, G_TYPE_VARIANT, 0);
  signals[ACTION_PAUSE] = g_signal_new_class_handler (
      "pause", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_mpris_plugin_pause,
      NULL, NULL, NULL, WP_TYPE_MPRIS_PLUGIN_OPERATION, 1, G_TYPE_STRING);
  signals[ACTION_MATCH_PID] = g_signal_new_class_handler (
      "match-pid", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_mpris_plugin_match_pid,
      NULL, NULL, NULL, G_TYPE_BOOLEAN, 2, G_TYPE_INT, G_TYPE_INT);
}


/*
 * Module
 */

WP_PLUGIN_EXPORT GObject *
wireplumber__module_init (WpCore * core, WpSpaJson * args, GError ** error)
{
  return G_OBJECT (g_object_new (wp_mpris_plugin_get_type (),
      "name", NAME,
      "core", core,
      NULL));
}
