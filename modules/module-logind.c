/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <glib-unix.h>

#include <systemd/sd-login.h>

#include <spa/utils/result.h>
#include <spa/utils/string.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("m-logind")

#define NAME "logind"

struct _WpLogind
{
  WpPlugin parent;
  sd_login_monitor *monitor;
  GSource *source;
  char *state;
};

enum {
  ACTION_GET_STATE,
  SIGNAL_STATE_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

G_DECLARE_FINAL_TYPE (WpLogind, wp_logind, WP, LOGIND, WpPlugin)
G_DEFINE_TYPE (WpLogind, wp_logind, WP_TYPE_PLUGIN)

static void
wp_logind_init (WpLogind * self)
{
}

static gchar *
wp_logind_get_state (WpLogind *self)
{
  return g_strdup (self->state);
}

static gboolean
wp_logind_source_ready (gint fd, GIOCondition condition, gpointer user_data)
{
  WpLogind *self = WP_LOGIND (user_data);
  sd_login_monitor_flush (self->monitor);
  {
    char *state = NULL;
    sd_uid_get_state (getuid(), &state);
    if (g_strcmp0 (state, self->state) != 0) {
      char *tmp = state;
      state = self->state;
      self->state = tmp;
      g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, self->state);
    }
    free (state);
  }
  return G_SOURCE_CONTINUE;
}

static void
wp_logind_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpLogind *self = WP_LOGIND (plugin);
  int res = 0;

  if ((res = sd_login_monitor_new ("uid", &self->monitor)) < 0) {
    wp_transition_return_error (transition, g_error_new (G_IO_ERROR,
            g_io_error_from_errno (-res),
            "failed to start systemd logind monitor: %d (%s)",
            res, spa_strerror(res)));
    return;
  }

  if ((res = sd_uid_get_state (getuid(), &self->state)) < 0) {
    wp_transition_return_error (transition, g_error_new (G_IO_ERROR,
            g_io_error_from_errno (-res),
            "failed to get systemd login state: %d (%s)",
            res, spa_strerror(res)));
    g_clear_pointer (&self->monitor, sd_login_monitor_unref);
    return;
  }

  self->source = g_unix_fd_source_new (
      sd_login_monitor_get_fd (self->monitor),
      sd_login_monitor_get_events (self->monitor));
  g_source_set_callback (self->source, G_SOURCE_FUNC (wp_logind_source_ready),
      self, NULL);

  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  GMainContext *context = wp_core_get_g_main_context (core);
  g_source_attach (self->source, context);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_logind_disable (WpPlugin * plugin)
{
  WpLogind *self = WP_LOGIND (plugin);

  g_clear_pointer (&self->state, free);
  g_source_destroy (self->source);
  g_clear_pointer (&self->source, g_source_unref);
  g_clear_pointer (&self->monitor, sd_login_monitor_unref);
}

static void
wp_logind_class_init (WpLogindClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_logind_enable;
  plugin_class->disable = wp_logind_disable;

  signals[ACTION_GET_STATE] = g_signal_new_class_handler (
      "get-state", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_logind_get_state,
      NULL, NULL, NULL, G_TYPE_STRING, 0);

  signals[SIGNAL_STATE_CHANGED] = g_signal_new (
      "state-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

WP_PLUGIN_EXPORT GObject *
wireplumber__module_init (WpCore * core, WpSpaJson * args, GError ** error)
{
  return G_OBJECT (g_object_new (wp_logind_get_type (),
      "name", NAME,
      "core", core,
      NULL));
}
