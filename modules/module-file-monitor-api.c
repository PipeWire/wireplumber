/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include <wp/wp.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("m-file-monitor-api")

struct _WpFileMonitorApi
{
  WpPlugin parent;

  GHashTable *monitors;
};

enum {
  ACTION_ADD_WATCH,
  ACTION_REMOVE_WATCH,
  SIGNAL_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

G_DECLARE_FINAL_TYPE (WpFileMonitorApi, wp_file_monitor_api, WP,
    FILE_MONITOR_API, WpPlugin)
G_DEFINE_TYPE (WpFileMonitorApi, wp_file_monitor_api, WP_TYPE_PLUGIN)

static void
wp_file_monitor_api_init (WpFileMonitorApi * self)
{
  self->monitors = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      g_object_unref);
}

static void
wp_file_monitor_api_finalize (GObject * object)
{
  WpFileMonitorApi * self = WP_FILE_MONITOR_API (object);

  g_clear_pointer (&self->monitors, g_hash_table_unref);

  G_OBJECT_CLASS (wp_file_monitor_api_parent_class)->finalize (object);
}

static void
on_file_monitor_changed (GFileMonitor *monitor, GFile *file, GFile *other,
    GFileMonitorEvent evtype, gpointer data)
{
  WpFileMonitorApi * self = WP_FILE_MONITOR_API (data);

  g_autofree char *fpath = g_file_get_path (file);
  g_autofree char *opath = NULL;
  const gchar *evtype_str = NULL;

  if (other)
      opath = g_file_get_path (other);

  switch(evtype) {
    case G_FILE_MONITOR_EVENT_CHANGED:
      evtype_str = "changed";
      break;
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
      evtype_str = "changes-done-hint";
      break;
    case G_FILE_MONITOR_EVENT_DELETED:
      evtype_str = "deleted";
      break;
    case G_FILE_MONITOR_EVENT_CREATED:
      evtype_str = "created";
      break;
    case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
      evtype_str = "attribute-changed";
      break;
    case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
      evtype_str = "pre-unmount";
      break;
    case G_FILE_MONITOR_EVENT_UNMOUNTED:
      evtype_str = "unmounted";
      break;
    case G_FILE_MONITOR_EVENT_MOVED:
      evtype_str = "moved";
      break;
    case G_FILE_MONITOR_EVENT_RENAMED:
      evtype_str = "renamed";
      break;
    case G_FILE_MONITOR_EVENT_MOVED_IN:
      evtype_str = "moved-in";
      break;
    case G_FILE_MONITOR_EVENT_MOVED_OUT:
      evtype_str = "moved-out";
      break;
    default:
      wp_warning_object (self, "Unknown event type %d", evtype);
      break;
  }

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0, fpath, opath, evtype_str);
}

static gboolean
wp_file_monitor_api_add_watch (WpFileMonitorApi * self, const gchar *path,
    const gchar *flags_str)
{
  g_autoptr (GError) e = NULL;
  g_autoptr (GFileMonitor) fm = NULL;
  g_autoptr (GFile) f = NULL;
  GFileMonitorFlags flags = G_FILE_MONITOR_NONE;

  /* don't do anything if the path is already being watched */
  if (g_hash_table_contains (self->monitors, path))
    return TRUE;

  /* get path */
  f = g_file_new_for_path (path);
  if (!f) {
    wp_warning_object (self, "Invalid path '%s'", path);
    return FALSE;
  }

  /* parse flags */
  for (guint i = 0; flags_str && i < strlen (flags_str); i++) {
    switch (flags_str[i]) {
      case 'o': flags |= G_FILE_MONITOR_WATCH_MOUNTS; break;
      case 's': flags |= G_FILE_MONITOR_SEND_MOVED; break;
      case 'h': flags |= G_FILE_MONITOR_WATCH_HARD_LINKS; break;
      case 'm': flags |= G_FILE_MONITOR_WATCH_MOVES; break;
      default:
        break;
    }
  }

  /* create the file monitor for that path */
  fm = g_file_monitor (f, flags, NULL, &e);
  if (e) {
    wp_warning_object (self, "Failed to add watch for path '%s': %s", path,
        e->message);
    return FALSE;
  }

  /* handle changed signal and add it to monitors table */
  g_signal_connect (fm, "changed", G_CALLBACK (on_file_monitor_changed), self);
  g_hash_table_insert (self->monitors, g_strdup (path), g_steal_pointer (&fm));
  return TRUE;
}

static void
wp_file_monitor_api_remove_watch (WpFileMonitorApi * self, const gchar *path)
{
  g_hash_table_remove (self->monitors, path);
}

static void
wp_file_monitor_api_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpFileMonitorApi * self = WP_FILE_MONITOR_API (plugin);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_file_monitor_api_disable (WpPlugin * plugin)
{
  WpFileMonitorApi * self = WP_FILE_MONITOR_API (plugin);

  g_hash_table_remove_all (self->monitors);
}

static void
wp_file_monitor_api_class_init (WpFileMonitorApiClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_file_monitor_api_finalize;

  plugin_class->enable = wp_file_monitor_api_enable;
  plugin_class->disable = wp_file_monitor_api_disable;

  signals[ACTION_ADD_WATCH] = g_signal_new_class_handler (
      "add-watch", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_file_monitor_api_add_watch,
      NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 2, G_TYPE_STRING, G_TYPE_STRING);

  signals[ACTION_REMOVE_WATCH] = g_signal_new_class_handler (
      "remove-watch", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_file_monitor_api_remove_watch,
      NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_STRING);

  signals[SIGNAL_CHANGED] = g_signal_new (
      "changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
}

WP_PLUGIN_EXPORT GObject *
wireplumber__module_init (WpCore * core, WpSpaJson * args, GError ** error)
{
  return G_OBJECT (g_object_new (wp_file_monitor_api_get_type (),
      "name", "file-monitor-api",
      "core", core,
      NULL));
}
