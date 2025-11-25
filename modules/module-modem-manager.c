/* WirePlumber
 *
 * Copyright (C) 2025 Richard Acayan and contributors
 *    @author: Richard Acayan
 *
 * SPDX-License-Identifier: MIT
 */

#include <gio/gio.h>
#include <wp/wp.h>

#include "dbus-connection-state.h"

#define WP_TYPE_MODEM_MANAGER wp_modem_manager_get_type ()

WP_DEFINE_LOCAL_LOG_TOPIC ("m-modem-manager")

struct _WpModemManager {
  WpPlugin parent;
  WpPlugin *dbus;
  GDBusObjectManager *manager;
  GList *voice;
  GList *calls;
  guint n_calls;
};

enum {
  SIGNAL_CALL_START,
  SIGNAL_CALL_STOP,
  N_SIGNALS,
};

/* see ModemManager-enums.h */
enum MMCallState {
  MM_CALL_STATE_DIALING     = 1,
  MM_CALL_STATE_RINGING_OUT = 2,
  MM_CALL_STATE_ACTIVE      = 4,
};

static guint signals[N_SIGNALS];

G_DECLARE_FINAL_TYPE (WpModemManager, wp_modem_manager,
                      WP, MODEM_MANAGER, WpPlugin)

G_DEFINE_TYPE (WpModemManager, wp_modem_manager, WP_TYPE_PLUGIN)

static void
wp_modem_manager_init (WpModemManager * self)
{
}

static gboolean
is_active_state (gint32 state)
{
  return state == MM_CALL_STATE_DIALING
      || state == MM_CALL_STATE_RINGING_OUT
      || state == MM_CALL_STATE_ACTIVE;
}

static void
active_calls_inc (WpModemManager * wpmm)
{
  wpmm->n_calls++;
  if (wpmm->n_calls == 1) {
    wp_info_object (wpmm, "modem call started");
    g_signal_emit (wpmm, signals[SIGNAL_CALL_START], 0);
  }
}
static void
active_calls_dec (WpModemManager * wpmm)
{
  wpmm->n_calls--;
  if (wpmm->n_calls == 0) {
    wp_info_object (wpmm, "modem call stopped");
    g_signal_emit (wpmm, signals[SIGNAL_CALL_STOP], 0);
  }
}

static void
on_call_state_change (GDBusProxy * iface,
                      gchar * sender,
                      gchar * signal,
                      GVariant * params,
                      gpointer data)
{
  WpModemManager *wpmm = WP_MODEM_MANAGER (data);
  gint32 old, new;
  guint32 reason;

  if (g_strcmp0 (signal, "StateChanged"))
    return;

  g_variant_get (params, "(iiu)", &old, &new, &reason);

  if (!is_active_state (old) && is_active_state (new)) {
    active_calls_inc (wpmm);
  } else if (is_active_state (old) && !is_active_state (new)) {
    active_calls_dec (wpmm);
  }
}

static void
bind_call (GObject * obj, GAsyncResult * res, gpointer data)
{
  WpModemManager *wpmm = WP_MODEM_MANAGER (data);
  g_autoptr (GError) err = NULL;
  GDBusProxy *call;
  g_autoptr (GVariant) prop = NULL;
  gint init_state;

  call = g_dbus_proxy_new_finish (res, &err);
  if (call == NULL) {
    g_prefix_error (&err, "Failed to get call: ");
    wp_warning_object (wpmm, "%s", err->message);
    return;
  }

  prop = g_dbus_proxy_get_cached_property (call, "State");
  if (prop == NULL) {
    wp_warning_object (wpmm, "Failed to get initial call state");
  } else {
    g_variant_get (prop, "i", &init_state);

    if (is_active_state (init_state))
      active_calls_inc (wpmm);
  }

  wpmm->calls = g_list_prepend (wpmm->calls, call);
  g_signal_connect (call, "g-signal", G_CALLBACK (on_call_state_change), wpmm);
}

static gint
match_call_path (gconstpointer a, gconstpointer b)
{
  GDBusProxy *call;
  const gchar *path, *call_path;

  if (G_IS_DBUS_PROXY (a)) {
    call = G_DBUS_PROXY (a);
    path = b;
  } else if (G_IS_DBUS_PROXY (b)) {
    call = G_DBUS_PROXY (b);
    path = a;
  } else {
    return 1;
  }

  call_path = g_dbus_proxy_get_object_path (call);
  return g_strcmp0 (path, call_path);
}

static void
on_voice_signal (GDBusProxy * iface,
                 gchar * sender,
                 gchar * signal,
                 GVariant * params,
                 gpointer data)
{
  WpModemManager *wpmm = WP_MODEM_MANAGER (data);
  GList *deleted;
  gchar *path;
  g_autoptr (GDBusConnection) conn = NULL;

  g_object_get (wpmm->dbus, "connection", &conn, NULL);

  if (!g_strcmp0 (signal, "CallAdded")) {
    g_variant_get (params, "(&o)", &path);
    g_dbus_proxy_new (conn,
                      G_DBUS_PROXY_FLAGS_NONE,
                      NULL,
                      "org.freedesktop.ModemManager1",
                      path,
                      "org.freedesktop.ModemManager1.Call",
                      NULL,
                      bind_call,
                      wpmm);
  } else if (!g_strcmp0 (signal, "CallDeleted")) {
    g_variant_get (params, "(&o)", &path);

    // The user shouldn't have hundreds of calls, so just linear search.
    deleted = g_list_find_custom (wpmm->calls, path, match_call_path);
    if (deleted) {
      g_object_unref (deleted->data);
      wpmm->calls = g_list_delete_link (wpmm->calls, deleted);
    }
  }
}

static void
list_calls_done (GObject * obj,
                 GAsyncResult *res,
                 gpointer data)
{
  WpModemManager *wpmm = WP_MODEM_MANAGER (data);
  g_autoptr (GVariant) params = NULL;
  g_autoptr (GVariantIter) calls = NULL;
  gchar *path;
  g_autoptr (GError) err = NULL;
  g_autoptr (GDBusConnection) conn = NULL;

  params = g_dbus_proxy_call_finish (G_DBUS_PROXY (obj), res, &err);
  if (params == NULL) {
    g_prefix_error (&err, "Failed to list active calls on startup: ");
    wp_warning_object (wpmm, "%s", err->message);
    return;
  }

  g_object_get (wpmm->dbus, "connection", &conn, NULL);

  g_variant_get (params, "(ao)", &calls);
  while (g_variant_iter_loop (calls, "&o", &path)) {
    g_dbus_proxy_new (conn,
                      G_DBUS_PROXY_FLAGS_NONE,
                      NULL,
                      "org.freedesktop.ModemManager1",
                      path,
                      "org.freedesktop.ModemManager1.Call",
                      NULL,
                      bind_call,
                      wpmm);
  }
}

static void
hotplug_modem (GDBusObjectManager * mm,
               GDBusObject * obj,
               gpointer data)
{
  WpModemManager *wpmm = WP_MODEM_MANAGER (data);
  GDBusInterface *iface;

  iface = g_dbus_object_get_interface (obj, "org.freedesktop.ModemManager1.Modem.Voice");
  if (iface == NULL)
    return;

  g_dbus_proxy_call (G_DBUS_PROXY (iface),
                     "ListCalls",
                     g_variant_new ("()"),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     list_calls_done,
                     wpmm);

  wpmm->voice = g_list_prepend (wpmm->voice, iface);
  g_signal_connect (iface, "g-signal", G_CALLBACK (on_voice_signal), wpmm);
}

static void
coldplug_modem (gpointer elt, gpointer data)
{
  GDBusObject *obj = elt;
  WpModemManager *wpmm = data;
  GDBusInterface *iface;

  iface = g_dbus_object_get_interface (obj, "org.freedesktop.ModemManager1.Modem.Voice");
  if (iface == NULL)
    return;

  g_dbus_proxy_call (G_DBUS_PROXY (iface),
                     "ListCalls",
                     g_variant_new ("()"),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     list_calls_done,
                     wpmm);

  wpmm->voice = g_list_prepend (wpmm->voice, iface);
  g_signal_connect (iface, "g-signal", G_CALLBACK (on_voice_signal), wpmm);
}

static void
on_modemmanager_get (GObject * obj, GAsyncResult * res, gpointer data)
{
  WpTransition *transition = WP_TRANSITION (data);
  GError *err = NULL;
  WpModemManager *wpmm;
  GList *modems;

  if (WP_IS_TRANSITION (data)) {
    // from wp_modem_manager_enable
    transition = WP_TRANSITION (data);
    wpmm = wp_transition_get_source_object (transition);
  } else {
    // from on_dbus_state_changed
    transition = NULL;
    wpmm = WP_MODEM_MANAGER (data);
  }

  wpmm->manager = g_dbus_object_manager_client_new_finish (res, &err);
  if (wpmm->manager == NULL) {
    g_prefix_error (&err, "Failed to connect to ModemManager: ");
    wp_warning_object (wpmm, "%s", err->message);

    if (transition)
      wp_transition_return_error (transition, g_steal_pointer (&err));

    g_clear_object (&wpmm->dbus);
    return;
  }

  modems = g_dbus_object_manager_get_objects (wpmm->manager);
  g_list_foreach (modems, coldplug_modem, wpmm);
  g_list_free_full (g_steal_pointer (&modems), g_object_unref);

  g_signal_connect (wpmm->manager, "object-added", G_CALLBACK (hotplug_modem), wpmm);

  if (WP_IS_TRANSITION (data))
    wp_object_update_features (WP_OBJECT (wpmm), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
on_dbus_state_changed (GObject * dbus, GParamSpec * spec, gpointer data)
{
  WpDBusConnectionState state;
  WpModemManager *wpmm = WP_MODEM_MANAGER (data);
  g_autoptr (GDBusConnection) conn = NULL;

  g_object_get (wpmm->dbus, "state", &state, NULL);

  switch (state) {
    case WP_DBUS_CONNECTION_STATE_CONNECTED:
      g_object_get (wpmm->dbus, "connection", &conn, NULL);
      g_dbus_object_manager_client_new (conn,
                                        G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
                                        "org.freedesktop.ModemManager1",
                                        "/org/freedesktop/ModemManager1",
                                        NULL, NULL, NULL, NULL,
                                        on_modemmanager_get, wpmm);
      break;

    case WP_DBUS_CONNECTION_STATE_CONNECTING:
    case WP_DBUS_CONNECTION_STATE_CLOSED:
      g_list_free_full (g_steal_pointer (&wpmm->calls), g_object_unref);
      g_list_free_full (g_steal_pointer (&wpmm->voice), g_object_unref);
      g_clear_object (&wpmm->manager);
      break;

    default:
      g_assert_not_reached ();
      break;
  }
}

static void
wp_modem_manager_enable (WpPlugin * self, WpTransition * transition)
{
  WpModemManager *wpmm = WP_MODEM_MANAGER (self);
  WpCore *core;
  GError *err;
  g_autoptr (GDBusConnection) conn = NULL;

  g_object_get (self, "core", &core, NULL);

  wpmm->dbus = wp_plugin_find (core, "system-dbus-connection");
  if (!wpmm->dbus) {
    err = g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
                       "dbus-connection module must be loaded before modem-manager");
    wp_transition_return_error (transition, err);
    return;
  }

  g_signal_connect_object (wpmm->dbus, "notify::state",
       G_CALLBACK (on_dbus_state_changed), wpmm, 0);

  g_object_get (wpmm->dbus, "connection", &conn, NULL);
  g_dbus_object_manager_client_new (conn,
                                    G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
                                    "org.freedesktop.ModemManager1",
                                    "/org/freedesktop/ModemManager1",
                                    NULL, NULL, NULL, NULL,
                                    on_modemmanager_get, transition);
}

static void
wp_modem_manager_disable (WpPlugin * self)
{
  WpModemManager *wpmm = WP_MODEM_MANAGER (self);

  g_list_free_full (g_steal_pointer (&wpmm->calls), g_object_unref);
  g_list_free_full (g_steal_pointer (&wpmm->voice), g_object_unref);
  g_clear_object (&wpmm->manager);
  g_clear_object (&wpmm->dbus);
}

static void
wp_modem_manager_class_init (WpModemManagerClass * klass)
{
  WpPluginClass *wpplugin_class = (WpPluginClass *) klass;

  wpplugin_class->enable = wp_modem_manager_enable;
  wpplugin_class->disable = wp_modem_manager_disable;

  signals[SIGNAL_CALL_START] = g_signal_new ("voice-call-start",
                                             WP_TYPE_MODEM_MANAGER,
                                             G_SIGNAL_RUN_LAST,
                                             0, NULL, NULL, NULL,
                                             G_TYPE_NONE,
                                             0);
  signals[SIGNAL_CALL_STOP] = g_signal_new ("voice-call-stop",
                                            WP_TYPE_MODEM_MANAGER,
                                            G_SIGNAL_RUN_LAST,
                                            0, NULL, NULL, NULL,
                                            G_TYPE_NONE,
                                            0);
}

WP_PLUGIN_EXPORT GObject *
wireplumber__module_init (WpCore * core, WpSpaJson * json, GError ** err)
{
  return g_object_new (wp_modem_manager_get_type (),
                       "name", "modem-manager",
                       "core", core,
                       NULL);
}
