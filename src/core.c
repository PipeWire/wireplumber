/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "core.h"
#include "loop-source.h"
#include "module-loader.h"
#include "plugin-registry-impl.h"
#include "proxy-registry-impl.h"
#include "utils.h"

#include <pipewire/pipewire.h>
#include <glib-unix.h>
#include <gio/gio.h>

#define WIREPLUMBER_DEFAULT_CONFIG_FILE "wireplumber.conf"

struct _WpCore
{
  GObject parent;

  GMainLoop *loop;
  GSource *source;

  struct pw_core *core;
  struct pw_remote *remote;
  struct spa_hook remote_listener;

  WpModuleLoader *module_loader;

  GError *exit_error;
};

G_DEFINE_TYPE (WpCore, wp_core, WP_TYPE_OBJECT);

static gboolean
signal_handler (gpointer data)
{
  WpCore *self = WP_CORE (data);
  wp_core_exit (self, WP_DOMAIN_CORE, WP_CODE_INTERRUPTED,
      "interrupted by signal");
  return G_SOURCE_CONTINUE;
}

static void
remote_state_changed (void * data, enum pw_remote_state old_state,
    enum pw_remote_state new_state, const char * error)
{
  WpCore *self = WP_CORE (data);

  g_debug ("remote state changed, old:%s new:%s",
      pw_remote_state_as_string (old_state),
      pw_remote_state_as_string (new_state));

  switch (new_state) {
  case PW_REMOTE_STATE_UNCONNECTED:
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
  .state_changed = remote_state_changed,
};

static gboolean
wp_core_parse_commands_file (WpCore * self, GInputStream * stream,
    GError ** error)
{
  g_autoptr (WpPluginRegistry) plugin_registry = NULL;
  gchar buffer[4096];
  gssize bytes_read;
  gchar *cur, *linestart, *saveptr;
  gchar *cmd, *abi, *module;
  gint lineno = 1;
  gboolean eof = FALSE;

  plugin_registry = wp_object_get_interface (WP_OBJECT (self),
      WP_TYPE_PLUGIN_REGISTRY);

  linestart = cur = buffer;

  do {
    bytes_read = g_input_stream_read (stream, cur, sizeof (buffer), NULL, error);
    if (bytes_read < 0)
      return FALSE;
    else if (bytes_read == 0) {
      eof = TRUE;
      /* terminate the remaining data, so that we consume it all */
      if (cur != linestart) {
        *cur = '\n';
      }
    }

    bytes_read += (cur - linestart);

    while (cur - buffer < bytes_read) {
      while (cur - buffer < bytes_read && *cur != '\n')
        cur++;

      if (*cur == '\n') {
        /* found the end of a line */
        *cur = '\0';

        /* tokenize and execute */
        cmd = strtok_r (linestart, " ", &saveptr);

        if (!g_strcmp0 (cmd, "load-module")) {
          abi = strtok_r (NULL, " ", &saveptr);
          module = strtok_r (NULL, " ", &saveptr);

          if (!abi || !module) {
            g_set_error (error, WP_DOMAIN_CORE, WP_CODE_INVALID_ARGUMENT,
                "expected ABI and MODULE at line %i", lineno);
            return FALSE;
          } else if (!wp_module_loader_load (self->module_loader,
                          plugin_registry, abi, module, error)) {
            return FALSE;
          }
        } else {
          g_set_error (error, WP_DOMAIN_CORE, WP_CODE_INVALID_ARGUMENT,
              "unknown command '%s' at line %i", cmd, lineno);
          return FALSE;
        }

        /* continue with the next line */
        linestart = ++cur;
        lineno++;
      }
    }

    /* reached the end of the data that was read */

    if (cur - linestart >= sizeof (buffer)) {
      g_set_error (error, WP_DOMAIN_CORE, WP_CODE_OPERATION_FAILED,
          "line %i exceeds the maximum allowed line size (%d bytes)",
          lineno, (gint) sizeof (buffer));
      return FALSE;
    } else if (cur - linestart > 0) {
      /* we have unparsed data, move it to the
       * beginning of the buffer and continue */
      strncpy (buffer, linestart, cur - linestart);
      linestart = buffer;
      cur = buffer + (cur - linestart);
    }
  } while (!eof);

  return TRUE;
}

static gboolean
wp_core_load_commands_file (WpCore * self)
{
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GFileInputStream) istream = NULL;
  const gchar *filename;

  filename = g_getenv ("WIREPLUMBER_CONFIG_FILE");
  if (!filename)
    filename = WIREPLUMBER_DEFAULT_CONFIG_FILE;

  file = g_file_new_for_path (filename);
  istream = g_file_read (file, NULL, &error);
  if (!istream) {
    g_propagate_error (&self->exit_error, error);
    error = NULL;
    g_main_loop_quit (self->loop);
    return FALSE;
  }

  if (!wp_core_parse_commands_file (self, G_INPUT_STREAM (istream), &error)) {
    g_propagate_prefixed_error (&self->exit_error, error, "Failed to read %s: ",
        filename);
    error = NULL;
    g_main_loop_quit (self->loop);
    return FALSE;
  }

  return TRUE;
}

static void
wp_core_init (WpCore * self)
{
  WpPluginRegistryImpl *plugin_registry;
  WpProxyRegistryImpl *proxy_registry;

  self->loop = g_main_loop_new (NULL, FALSE);
  self->source = wp_loop_source_new ();
  g_source_attach (self->source, NULL);

  self->core = pw_core_new (wp_loop_source_get_loop (self->source), NULL, 0);
  self->remote = pw_remote_new (self->core, NULL, 0);

  pw_remote_add_listener (self->remote, &self->remote_listener, &remote_events,
      self);

  self->module_loader = wp_module_loader_new ();

  proxy_registry = wp_proxy_registry_impl_new (self->remote);
  wp_object_attach_interface_impl (WP_OBJECT (self), proxy_registry, NULL);

  plugin_registry = wp_plugin_registry_impl_new ();
  wp_object_attach_interface_impl (WP_OBJECT (self), plugin_registry, NULL);
}

static void
wp_core_dispose (GObject * obj)
{
  WpCore *self = WP_CORE (obj);
  g_autoptr (WpPluginRegistry) plugin_registry = NULL;
  g_autoptr (WpProxyRegistry) proxy_registry = NULL;

  /* ensure all proxies and plugins are unrefed,
   * so that the registries can be disposed */

  plugin_registry = wp_object_get_interface (WP_OBJECT (self),
      WP_TYPE_PLUGIN_REGISTRY);
  wp_plugin_registry_impl_unload (WP_PLUGIN_REGISTRY_IMPL (plugin_registry));

  proxy_registry = wp_object_get_interface (WP_OBJECT (self),
      WP_TYPE_PROXY_REGISTRY);
  wp_proxy_registry_impl_unload (WP_PROXY_REGISTRY_IMPL (proxy_registry));
}

static void
wp_core_finalize (GObject * obj)
{
  WpCore *self = WP_CORE (obj);

  g_clear_object (&self->module_loader);

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

  object_class->dispose = wp_core_dispose;
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
wp_core_run_in_idle (WpCore * self)
{
  if (!wp_core_load_commands_file (self)) goto out;
  if (pw_remote_connect (self->remote) < 0) goto out;

out:
  return G_SOURCE_REMOVE;
}

void
wp_core_run (WpCore * self, GError ** error)
{
  g_unix_signal_add (SIGINT, signal_handler, self);
  g_unix_signal_add (SIGTERM, signal_handler, self);
  g_unix_signal_add (SIGHUP, signal_handler, self);

  g_idle_add ((GSourceFunc) wp_core_run_in_idle, self);

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
