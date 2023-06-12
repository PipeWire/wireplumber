/* WirePlumber
 *
 * Copyright © 2019-2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <glib-unix.h>
#include <pipewire/keys.h>
#include <stdio.h>
#include <locale.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wpexec")

#define WP_DOMAIN_DAEMON (wp_domain_daemon_quark ())
static G_DEFINE_QUARK (wireplumber-daemon, wp_domain_daemon);

enum WpExitCode
{
  /* based on sysexits.h */
  WP_EXIT_OK = 0,
  WP_EXIT_USAGE = 64,       /* command line usage error */
  WP_EXIT_UNAVAILABLE = 69, /* service unavailable */
  WP_EXIT_SOFTWARE = 70,    /* internal software error */
};

static gchar * exec_script = NULL;
static gchar *exec_args_s = NULL;
static WpSpaJson *exec_args = NULL;

static gboolean
parse_exec_script_arg (const gchar *option_name, const gchar *value,
    gpointer data, GError **error)
{
  /* the first argument is the script */
  if (!exec_script) {
    exec_script = g_strdup (value);
    return TRUE;
  }

  /* the second argument is a json object with script arguments */
  if (!exec_args) {
    exec_args_s = g_strdup (value);
    exec_args = wp_spa_json_new_from_string (exec_args_s);
    if (!exec_args || !wp_spa_json_is_object (exec_args)) {
      g_set_error (error, WP_DOMAIN_DAEMON, WP_EXIT_USAGE,
          "script argument must be a JSON object");
      return FALSE;
    }
  }

  return TRUE;
}

static GOptionEntry entries[] =
{
  { G_OPTION_REMAINING, 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK,
    parse_exec_script_arg, NULL, NULL },
  { NULL }
};

/*** WpInitTransition ***/

struct _WpInitTransition
{
  WpTransition parent;
  guint plugins_loaded;
};

enum {
  STEP_CONNECT = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_ACTIVATE_PLUGINS,
  STEP_ACTIVATE_SCRIPT,
};

G_DECLARE_FINAL_TYPE (WpInitTransition, wp_init_transition,
                      WP, INIT_TRANSITION, WpTransition)
G_DEFINE_TYPE (WpInitTransition, wp_init_transition, WP_TYPE_TRANSITION)

static void
wp_init_transition_init (WpInitTransition * self)
{
}

static guint
wp_init_transition_get_next_step (WpTransition * transition, guint step)
{
  switch (step) {
  case WP_TRANSITION_STEP_NONE: return STEP_CONNECT;
  case STEP_CONNECT:            return STEP_ACTIVATE_PLUGINS;
  case STEP_ACTIVATE_SCRIPT:    return WP_TRANSITION_STEP_NONE;

  case STEP_ACTIVATE_PLUGINS: {
    WpInitTransition *self = WP_INIT_TRANSITION (transition);
    if (self->plugins_loaded == 2)
      return STEP_ACTIVATE_SCRIPT;
    else
      return STEP_ACTIVATE_PLUGINS;
  }

  default:
    g_return_val_if_reached (WP_TRANSITION_STEP_ERROR);
  }
}

static void
on_plugin_loaded (WpCore * core, GAsyncResult * res, WpInitTransition *self)
{
  GError *error = NULL;

  if (!wp_core_load_component_finish (core, res, &error)) {
    wp_transition_return_error (WP_TRANSITION (self), error);
    return;
  }
  ++self->plugins_loaded;
  wp_transition_advance (WP_TRANSITION (self));
}

static void
wp_init_transition_execute_step (WpTransition * transition, guint step)
{
  WpInitTransition *self = WP_INIT_TRANSITION (transition);
  WpCore *core = wp_transition_get_source_object (transition);

  switch (step) {
  case STEP_CONNECT:
    g_signal_connect_object (core, "connected",
        G_CALLBACK (wp_transition_advance), transition, G_CONNECT_SWAPPED);

    if (!wp_core_connect (core)) {
      wp_transition_return_error (transition, g_error_new (WP_DOMAIN_DAEMON,
          WP_EXIT_UNAVAILABLE, "Failed to connect to PipeWire"));
      return;
    }
    break;

  case STEP_ACTIVATE_PLUGINS: {
    wp_core_load_component (core, "libwireplumber-module-lua-scripting",
        "module", NULL, NULL, NULL, (GAsyncReadyCallback) on_plugin_loaded, self);
    wp_core_load_component (core, "libwireplumber-module-standard-event-source",
        "module", NULL, NULL, NULL, (GAsyncReadyCallback) on_plugin_loaded, self);
    break;
  }

  case STEP_ACTIVATE_SCRIPT: {
    wp_core_load_component (core, exec_script, "script/lua", exec_args, NULL, NULL,
          (GAsyncReadyCallback) on_plugin_loaded, self);
    break;
  }

  case WP_TRANSITION_STEP_ERROR:
    break;

  default:
    g_assert_not_reached ();
  }
}

static void
wp_init_transition_class_init (WpInitTransitionClass * klass)
{
  WpTransitionClass * transition_class = (WpTransitionClass *) klass;

  transition_class->get_next_step = wp_init_transition_get_next_step;
  transition_class->execute_step = wp_init_transition_execute_step;
}

/*** WpExec ***/

typedef struct
{
  WpCore *core;
  GMainLoop *loop;
  gint exit_code;
} WpExec;

static void
wpexec_clear (WpExec * self)
{
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_clear_object (&self->core);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (WpExec, wpexec_clear)

static gboolean
signal_handler (gpointer data)
{
  WpExec *d = data;
  g_main_loop_quit (d->loop);
  return G_SOURCE_REMOVE;
}

static gboolean
init_start (WpTransition * transition)
{
  wp_transition_advance (transition);
  return G_SOURCE_REMOVE;
}

static void
init_done (WpCore * core, GAsyncResult * res, WpExec * d)
{
  g_autoptr (GError) error = NULL;
  if (!wp_transition_finish (res, &error)) {
    fprintf (stderr, "%s\n", error->message);
    d->exit_code = (error->domain == WP_DOMAIN_DAEMON) ?
        error->code : WP_EXIT_SOFTWARE;
    g_main_loop_quit (d->loop);
  }
}

gint
main (gint argc, gchar **argv)
{
  g_auto (WpExec) d = {0};
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;

  setlocale (LC_ALL, "");
  setlocale (LC_NUMERIC, "C");
  wp_init (WP_INIT_ALL);

  context = g_option_context_new ("- WirePlumber script interpreter");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    fprintf (stderr, "%s\n", error->message);
    return WP_EXIT_USAGE;
  }

  /* init wireplumber core */
  d.loop = g_main_loop_new (NULL, FALSE);
  d.core = wp_core_new (NULL, wp_properties_new (
          PW_KEY_APP_NAME, "wpexec",
          NULL));
  g_signal_connect_swapped (d.core, "disconnected",
      G_CALLBACK (g_main_loop_quit), d.loop);

  /* watch for exit signals */
  g_unix_signal_add (SIGINT, signal_handler, &d);
  g_unix_signal_add (SIGTERM, signal_handler, &d);
  g_unix_signal_add (SIGHUP, signal_handler, &d);

  /* initialization transition */
  g_idle_add ((GSourceFunc) init_start,
      wp_transition_new (wp_init_transition_get_type (), d.core,
          NULL, (GAsyncReadyCallback) init_done, &d));

  /* run */
  g_main_loop_run (d.loop);
  wp_core_disconnect (d.core);
  return d.exit_code;
}
