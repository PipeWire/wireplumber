/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#define G_LOG_DOMAIN "wireplumber-core"

#include "core.h"
#include "loop-source.h"
#include "utils.h"

#include <pipewire/pipewire.h>
#include <glib-unix.h>

struct _WpCore
{
  GObject parent;

  GMainLoop *loop;
  GSource *source;

  struct pw_core *core;
  struct pw_remote *remote;
  struct spa_hook remote_listener;

  struct pw_core_proxy *core_proxy;
  struct spa_hook core_proxy_listener;

  struct pw_registry_proxy *registry_proxy;
  struct spa_hook registry_proxy_listener;

  GError *exit_error;
};

G_DEFINE_TYPE (WpCore, wp_core, G_TYPE_OBJECT);

static const struct pw_registry_proxy_events registry_events = {
  PW_VERSION_REGISTRY_PROXY_EVENTS,
  //.global = registry_global,
  //.global_remove = registry_global_remove,
};

static const struct pw_core_proxy_events core_events = {
  PW_VERSION_CORE_EVENTS,
  //.done = core_done
};

static void on_state_changed (void * data,
    enum pw_remote_state old_state,
    enum pw_remote_state new_state,
    const char * error)
{
  WpCore *self = WP_CORE (data);

  g_debug ("remote state changed, old:%s new:%s",
      pw_remote_state_as_string (old_state),
      pw_remote_state_as_string (new_state));

  switch (new_state) {
  case PW_REMOTE_STATE_CONNECTED:
    self->core_proxy = pw_remote_get_core_proxy (self->remote);
    pw_core_proxy_add_listener (self->core_proxy, &self->core_proxy_listener,
        &core_events, self);

    self->registry_proxy = pw_core_proxy_get_registry (self->core_proxy,
        PW_TYPE_INTERFACE_Registry, PW_VERSION_REGISTRY, 0);
    pw_registry_proxy_add_listener (self->registry_proxy,
        &self->registry_proxy_listener, &registry_events, self);
    break;

  case PW_REMOTE_STATE_UNCONNECTED:
    self->core_proxy = NULL;
    self->registry_proxy = NULL;
    wp_core_exit (self, WP_DOMAIN_CORE, WP_CODE_DISCONNECTED, "disconnected");
    break;

  case PW_REMOTE_STATE_ERROR:
    wp_core_exit (self, WP_DOMAIN_CORE, WP_CODE_REMOTE_ERROR,
        "pipewire remote error: %s", error);
    break;

  default:
    break;
  }
}

static const struct pw_remote_events remote_events = {
  PW_VERSION_REMOTE_EVENTS,
  .state_changed = on_state_changed,
};

static void
wp_core_init (WpCore * self)
{
  self->loop = g_main_loop_new (NULL, FALSE);
  self->source = wp_loop_source_new ();
  g_source_attach (self->source, NULL);

  self->core = pw_core_new (wp_loop_source_get_loop (self->source), NULL, 0);
  self->remote = pw_remote_new (self->core, NULL, 0);

  pw_remote_add_listener (self->remote, &self->remote_listener, &remote_events,
      self);
}

static void
wp_core_finalize (GObject * obj)
{
  WpCore *self = WP_CORE (obj);

  spa_hook_remove (&self->remote_listener);

  pw_remote_destroy (self->remote);
  pw_core_destroy (self->core);

  g_source_destroy (self->source);
  g_source_unref (self->source);
  g_main_loop_unref (self->loop);

  g_warn_if_fail (self->exit_error == NULL);
  g_clear_error (&self->exit_error);
}

static void
wp_core_class_init (WpCoreClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;

  object_class->finalize = wp_core_finalize;
}

WpCore *
wp_core_get_instance (void)
{
  static WpCore *instance = NULL;
  if (G_UNLIKELY (!instance)) {
    instance = g_object_new (wp_core_get_type (), NULL);
  }
  return instance;
}

static gboolean
signal_handler (gpointer data)
{
  WpCore *self = WP_CORE (data);
  wp_core_exit (self, WP_DOMAIN_CORE, WP_CODE_INTERRUPTED,
      "interrupted by signal");
  return G_SOURCE_CONTINUE;
}

void
wp_core_run (WpCore * self, GError ** error)
{
  g_unix_signal_add (SIGINT, signal_handler, self);
  g_unix_signal_add (SIGTERM, signal_handler, self);
  g_unix_signal_add (SIGHUP, signal_handler, self);

  g_idle_add ((GSourceFunc) pw_remote_connect, self->remote);

  g_main_loop_run (self->loop);

  if (self->exit_error) {
    g_propagate_error (error, self->exit_error);
    self->exit_error = NULL;
  }
}

void
wp_core_exit (WpCore * self, GQuark domain, gint code,
    const gchar *format, ...)
{
  va_list args;
  va_start (args, format);
  self->exit_error = g_error_new_valist (domain, code, format, args);
  va_end (args);
  g_main_loop_quit (self->loop);
}
