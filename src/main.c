/* WirePlumber
 *
 * Copyright © 2019-2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <glib-unix.h>
#include <pipewire/pipewire.h>
#include <locale.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wireplumber")

enum WpExitCode
{
  /* based on sysexits.h */
  WP_EXIT_OK = 0,
  WP_EXIT_USAGE = 64,       /* command line usage error */
  WP_EXIT_UNAVAILABLE = 69, /* service unavailable */
  WP_EXIT_SOFTWARE = 70,    /* internal software error */
  WP_EXIT_CONFIG = 78,      /* configuration error */
};

static gboolean show_version = FALSE;
static gchar * config_file = NULL;
static gchar * profile = NULL;

static GOptionEntry entries[] =
{
  { "version", 'v', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &show_version,
    "Show version", NULL },
  { "config-file", 'c', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &config_file,
    "The context configuration file", NULL },
  { "profile", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &profile,
    "The profile to load", NULL },
  { NULL }
};

/*** WpDaemon ***/

typedef struct
{
  WpCore *core;
  GMainLoop *loop;
  gint exit_code;
} WpDaemon;

static void
daemon_clear (WpDaemon * self)
{
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_clear_object (&self->core);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (WpDaemon, daemon_clear)

static void
daemon_exit (WpDaemon * d, gint code)
{
  /* replace OK with an error, but do not replace error with OK */
  if (d->exit_code == WP_EXIT_OK)
    d->exit_code = code;
  g_main_loop_quit (d->loop);
}

static void
on_disconnected (WpCore *core, WpDaemon * d)
{
  wp_notice ("disconnected from pipewire");
  daemon_exit (d, WP_EXIT_OK);
}

static gboolean
signal_handler (int signal, gpointer data)
{
  WpDaemon *d = data;
  wp_notice ("stopped by signal: %s", strsignal (signal));
  daemon_exit (d, WP_EXIT_OK);
  return G_SOURCE_CONTINUE;
}

static gboolean
signal_handler_int (gpointer data)
{
  return signal_handler (SIGINT, data);
}

static gboolean
signal_handler_hup (gpointer data)
{
  return signal_handler (SIGHUP, data);
}

static gboolean
signal_handler_term (gpointer data)
{
  return signal_handler (SIGTERM, data);
}

static void
on_core_activated (WpObject * core, GAsyncResult * res, WpDaemon * d)
{
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (core, res, &error)) {
    fprintf (stderr, "%s\n", error->message);

    switch (error->code) {
      case WP_LIBRARY_ERROR_SERVICE_UNAVAILABLE:
        daemon_exit (d, WP_EXIT_UNAVAILABLE);
        break;

      case WP_LIBRARY_ERROR_INVALID_ARGUMENT:
        daemon_exit (d, WP_EXIT_CONFIG);
        break;

      default:
        daemon_exit (d, WP_EXIT_SOFTWARE);
    }
  }
}

gint
main (gint argc, gchar **argv)
{
  g_auto (WpDaemon) d = {0};
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpProperties) properties = NULL;
  const gchar *conf_env;

  setlocale (LC_ALL, "");
  setlocale (LC_NUMERIC, "C");
  wp_init (WP_INIT_ALL);

  context = g_option_context_new ("- PipeWire Session/Policy Manager");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    fprintf (stderr, "%s\n", error->message);
    return WP_EXIT_USAGE;
  }

  if (show_version) {
    g_print ("%s\n"
        "Compiled with libwireplumber %s\n"
        "Linked with libwireplumber %s\n",
        argv[0],
        WIREPLUMBER_VERSION,
        wp_get_library_version());
    return WP_EXIT_OK;
  }

  if (!config_file)
    config_file = "wireplumber.conf";
  if (!profile)
    profile = "main";

  /* Forward WIREPLUMBER_CONFIG_DIR to PIPEWIRE_CONFIG_DIR */
  conf_env = g_getenv ("WIREPLUMBER_CONFIG_DIR");
  if (conf_env)
    g_setenv ("PIPEWIRE_CONFIG_DIR", conf_env, TRUE);

  properties = wp_properties_new (
      PW_KEY_CONFIG_NAME, config_file,
      PW_KEY_APP_NAME, "WirePlumber",
      "wireplumber.daemon", "true",
      "wireplumber.profile", profile,
      NULL);

  /* prefer manager socket */
  if (pw_check_library_version(0, 3, 84))
    wp_properties_set (properties, PW_KEY_REMOTE_NAME,
        ("[" PW_DEFAULT_REMOTE "-manager," PW_DEFAULT_REMOTE "]"));

  /* init wireplumber daemon */
  d.loop = g_main_loop_new (NULL, FALSE);
  d.core = wp_core_new (NULL, g_steal_pointer (&properties));
  g_signal_connect (d.core, "disconnected", G_CALLBACK (on_disconnected), &d);

  /* watch for exit signals */
  g_unix_signal_add (SIGINT, signal_handler_int, &d);
  g_unix_signal_add (SIGTERM, signal_handler_term, &d);
  g_unix_signal_add (SIGHUP, signal_handler_hup, &d);

  wp_object_activate (WP_OBJECT (d.core), WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_core_activated, &d);

  /* run */
  g_main_loop_run (d.loop);
  wp_core_disconnect (d.core);
  return d.exit_code;
}
