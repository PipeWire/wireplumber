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

#define WP_DOMAIN_DAEMON (wp_domain_daemon_quark ())
static G_DEFINE_QUARK (wireplumber-daemon, wp_domain_daemon);

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

static GOptionEntry entries[] =
{
  { "version", 'v', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &show_version,
    "Show version", NULL },
  { "config-file", 'c', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &config_file,
    "The context configuration file", NULL },
  { NULL }
};

/*** WpInitTransition ***/

struct _WpInitTransition
{
  WpTransition parent;
  WpObjectManager *om;
};

enum {
  STEP_CONNECT = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_CHECK_MEDIA_SESSION,
  STEP_LOAD_COMPONENTS,
  STEP_CLEANUP,
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
  case WP_TRANSITION_STEP_NONE:     return STEP_CONNECT;
  case STEP_CONNECT: {
    WpCore *core = wp_transition_get_source_object (transition);
    WpCore *export_core =
        g_object_get_data (G_OBJECT (core), "wireplumber.export-core");

    if (wp_core_is_connected (core) &&
        (!export_core || wp_core_is_connected (export_core))) {
      return STEP_CHECK_MEDIA_SESSION;
    } else {
      return STEP_CONNECT;
    }
  }
  case STEP_CHECK_MEDIA_SESSION:    return STEP_LOAD_COMPONENTS;
  case STEP_LOAD_COMPONENTS:        return STEP_CLEANUP;
  case STEP_CLEANUP:                return WP_TRANSITION_STEP_NONE;

  default:
    g_return_val_if_reached (WP_TRANSITION_STEP_ERROR);
  }
}

static void
check_media_session (WpObjectManager * om, WpInitTransition *self)
{
  if (wp_object_manager_get_n_objects (om) > 0) {
    wp_transition_return_error (WP_TRANSITION (self), g_error_new (
        WP_DOMAIN_DAEMON, WP_EXIT_SOFTWARE,
        "pipewire-media-session appears to be running; "
        "please stop it before starting wireplumber"));
    return;
  }
  wp_transition_advance (WP_TRANSITION (self));
}

static void
on_components_loaded (WpCore *core, GAsyncResult *res, gpointer data)
{
  WpTransition *self = data;
  g_autoptr (GError) error = NULL;

  if (!wp_core_load_component_finish (core, res, &error)) {
    wp_transition_return_error (self, g_error_new (
        WP_DOMAIN_DAEMON, WP_EXIT_CONFIG,
        "failed to load components: %s", error->message));
    return;
  }

  wp_transition_advance (self);
}

static void
wp_init_transition_execute_step (WpTransition * transition, guint step)
{
  WpInitTransition *self = WP_INIT_TRANSITION (transition);
  WpCore *core = wp_transition_get_source_object (transition);
  struct pw_context *pw_ctx = wp_core_get_pw_context (core);
  const struct pw_properties *props = pw_context_get_properties (pw_ctx);

  switch (step) {

  case STEP_CONNECT: {
    wp_info_object (self, "core connect...");

    g_signal_connect_object (core, "connected",
        G_CALLBACK (wp_transition_advance), transition, G_CONNECT_SWAPPED);

    if (!wp_core_connect (core)) {
      wp_transition_return_error (transition, g_error_new (WP_DOMAIN_DAEMON,
          WP_EXIT_UNAVAILABLE, "Failed to connect to PipeWire"));
      return;
    }

    /* initialize secondary connection to pipewire */
    const char *str = pw_properties_get (props, "wireplumber.export-core");
    if (str && pw_properties_parse_bool (str)) {
      g_autofree gchar *export_core_name = NULL;
      g_autoptr (WpCore) export_core = NULL;

      str = pw_properties_get (props, PW_KEY_APP_NAME);
      export_core_name = g_strdup_printf ("%s [export]", str);

      export_core = wp_core_clone (core);
      wp_core_update_properties (export_core, wp_properties_new (
            PW_KEY_APP_NAME, export_core_name,
            NULL));

      g_signal_connect_object (export_core, "connected",
          G_CALLBACK (wp_transition_advance), transition, G_CONNECT_SWAPPED);

      if (!wp_core_connect (export_core)) {
        wp_transition_return_error (transition, g_error_new (
            WP_DOMAIN_DAEMON, WP_EXIT_UNAVAILABLE,
            "Failed to connect export core to PipeWire"));
        return;
      }

      g_object_set_data_full (G_OBJECT (core), "wireplumber.export-core",
          g_steal_pointer (&export_core), g_object_unref);
    }
    break;
  }

  case STEP_CHECK_MEDIA_SESSION: {
    wp_info_object (self, "checking for session manager conflicts...");

    self->om = wp_object_manager_new ();
    wp_object_manager_add_interest (self->om, WP_TYPE_CLIENT,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
        "application.name", "=s", "pipewire-media-session", NULL);
    g_signal_connect_object (self->om, "installed",
        G_CALLBACK (check_media_session), self, 0);
    wp_core_install_object_manager (core, self->om);
    break;
  }

  case STEP_LOAD_COMPONENTS: {
    g_autoptr (WpConf) conf = wp_conf_get_instance (core);
    g_autoptr (WpSpaJson) json_comps = NULL;

    wp_info_object (self, "parsing & loading components...");

    /* Load components that are defined in the configuration section */
    json_comps = wp_conf_get_section (conf, "wireplumber.components", NULL);
    wp_core_load_component (core, NULL, "array", json_comps, NULL,
        (GAsyncReadyCallback) on_components_loaded, self);
    break;
  }
  case STEP_CLEANUP:
    wp_info_object (self, "WirePlumber initialized");
    G_GNUC_FALLTHROUGH;

  case WP_TRANSITION_STEP_ERROR:
    g_clear_object (&self->om);
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


static gboolean
init_start (WpTransition * transition)
{
  wp_transition_advance (transition);
  return G_SOURCE_REMOVE;
}

static void
init_done (WpCore * core, GAsyncResult * res, WpDaemon * d)
{
  g_autoptr (GError) error = NULL;
  if (!wp_transition_finish (res, &error)) {
    fprintf (stderr, "%s\n", error->message);
    daemon_exit (d, (error->domain == WP_DOMAIN_DAEMON) ?
        error->code : WP_EXIT_SOFTWARE);
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

  /* Forward WIREPLUMBER_CONFIG_DIR to PIPEWIRE_CONFIG_DIR */
  conf_env = g_getenv ("WIREPLUMBER_CONFIG_DIR");
  if (conf_env)
    g_setenv ("PIPEWIRE_CONFIG_DIR", conf_env, TRUE);

  properties = wp_properties_new (
      PW_KEY_CONFIG_NAME, config_file,
      PW_KEY_APP_NAME, "WirePlumber",
      "wireplumber.daemon", "true",
      "wireplumber.export-core", "true",
      NULL);

  /* init wireplumber daemon */
  d.loop = g_main_loop_new (NULL, FALSE);
  d.core = wp_core_new (NULL, g_steal_pointer (&properties));
  g_signal_connect (d.core, "disconnected", G_CALLBACK (on_disconnected), &d);

  /* watch for exit signals */
  g_unix_signal_add (SIGINT, signal_handler_int, &d);
  g_unix_signal_add (SIGTERM, signal_handler_term, &d);
  g_unix_signal_add (SIGHUP, signal_handler_hup, &d);

  /* initialization transition */
  g_idle_add ((GSourceFunc) init_start,
      wp_transition_new (wp_init_transition_get_type (), d.core,
          NULL, (GAsyncReadyCallback) init_done, &d));

  /* run */
  g_main_loop_run (d.loop);
  wp_core_disconnect (d.core);
  return d.exit_code;
}
