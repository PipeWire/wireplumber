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
  guint pending_plugins;
};

enum {
  STEP_CONNECT = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_ACTIVATE_SETTINGS,
  STEP_LOAD_COMPONENTS,
  STEP_CHECK_MEDIA_SESSION,
  STEP_ACTIVATE_PLUGINS,
  STEP_ACTIVATE_SCRIPTS,
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
  case WP_TRANSITION_STEP_NONE: return STEP_CONNECT;
  case STEP_CONNECT:            return STEP_ACTIVATE_SETTINGS;
  case STEP_ACTIVATE_SETTINGS:  return STEP_LOAD_COMPONENTS;
  case STEP_LOAD_COMPONENTS:    return STEP_CHECK_MEDIA_SESSION;
  case STEP_CHECK_MEDIA_SESSION:return STEP_ACTIVATE_PLUGINS;
  case STEP_CLEANUP:            return WP_TRANSITION_STEP_NONE;

  case STEP_ACTIVATE_PLUGINS: {
    WpInitTransition *self = WP_INIT_TRANSITION (transition);
    if (self->pending_plugins == 0)
      return STEP_ACTIVATE_SCRIPTS;
    else
      return STEP_ACTIVATE_PLUGINS;
  }

  case STEP_ACTIVATE_SCRIPTS: {
    WpInitTransition *self = WP_INIT_TRANSITION (transition);
    if (self->pending_plugins == 0)
      return STEP_CLEANUP;
    else
      return STEP_ACTIVATE_SCRIPTS;
  }

  default:
    g_return_val_if_reached (WP_TRANSITION_STEP_ERROR);
  }
}

static void
on_plugin_activated (WpObject * p, GAsyncResult * res, WpInitTransition *self)
{
  GError *error = NULL;

  if (!wp_object_activate_finish (p, res, &error)) {
    wp_transition_return_error (WP_TRANSITION (self), error);
    return;
  }

  --self->pending_plugins;
  wp_transition_advance (WP_TRANSITION (self));
}

static void
on_plugin_added (WpObjectManager * om, WpObject * p, WpInitTransition *self)
{
  self->pending_plugins++;
  wp_object_activate (p, WP_PLUGIN_FEATURE_ENABLED, NULL,
      (GAsyncReadyCallback) on_plugin_activated, self);
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

struct data {
  WpTransition *transition;
  int count;
};

static int
do_load_components(void *data, const char *location, const char *section,
		const char *str, size_t len)
{
  struct data *d = data;
  WpTransition *transition = d->transition;
  WpCore *core = wp_transition_get_source_object (transition);
  g_autoptr (WpSpaJson) json = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  g_autoptr (WpSettings) settings = wp_settings_get_instance (core, NULL);
  GError *error = NULL;

  json = wp_spa_json_new_from_stringn (str, len);

  if (!wp_spa_json_is_array (json)) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_DAEMON, WP_EXIT_CONFIG,
        "wireplumber.components is not a JSON array"));
    return -EINVAL;
  }

  it = wp_spa_json_new_iterator (json);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *o = g_value_get_boxed (&item);
    g_autofree gchar *name = NULL;
    g_autofree gchar *type = NULL;
    g_autofree gchar *deps = NULL;
    WpSpaJson *flags = NULL;
    gboolean if_exists = FALSE;
    gboolean no_fail = FALSE;

    if (!wp_spa_json_is_object (o) ||
        !wp_spa_json_object_get (o,
            "name", "s", &name,
            "type", "s", &type,
            NULL)) {
      wp_transition_return_error (transition, g_error_new (
          WP_DOMAIN_DAEMON, WP_EXIT_CONFIG,
          "component must have both a 'name' and a 'type'"));
      return -EINVAL;
    }

    if (wp_spa_json_object_get (o, "deps", "s", &deps, NULL) && deps) {
      gboolean value = 0;
      g_autoptr (WpSpaJson) j = wp_settings_get (settings, deps);
      if (!j || !wp_spa_json_parse_boolean (j, &value)) {
        wp_info ("deps(%s) not met for component(%s), skip loading it",
            deps, name);
        continue;
      }
    }

    if (wp_spa_json_object_get (o, "flags", "J", &flags, NULL) && flags) {
      g_autofree gchar *s1 = NULL;
      g_autofree gchar *s2 = NULL;

      wp_spa_json_parse_array (flags, "s", &s1, "s", &s2, NULL);

      if (!g_strcmp0 (s1, "ifexists") || !g_strcmp0 (s2, "ifexists"))
        if_exists = TRUE;
      if (!g_strcmp0 (s1, "nofail") || !g_strcmp0 (s2, "nofail"))
        no_fail = TRUE;
    }

    wp_debug ("load component(%s) type(%s) deps(%s) ifexists(%d) nofail(%d)",
        name, type, deps, if_exists, no_fail);

    if (!wp_core_load_component (core, name, type, NULL, &error)) {
      wp_info ("%s", error->message);
      if ((error->code == G_FILE_ERROR_NOENT) ||
          (error->code == G_FILE_ERROR_ACCES)) {

        if (if_exists) {
          wp_info ("\"ifexists\" flag set, ignore the failure");
          g_clear_error (&error);
          continue;
        }

      } else if (no_fail) {
        wp_info ("\"nofail\" flag set, ignore the failure");
        g_clear_error (&error);
        continue;
      }
      wp_transition_return_error (transition, error);
      return -EINVAL;
    }
    d->count++;
  }
  return 0;
}

static void
on_settings_ready (WpSettings *s, GAsyncResult *res, gpointer data)
{
  WpInitTransition *self = WP_INIT_TRANSITION (data);
  g_autoptr (GError) error = NULL;

  wp_info_object(self, "wpsettings object ready");

  if (!wp_object_activate_finish (WP_OBJECT (s), res, &error)) {
    wp_debug_object (self, "wpsettings activation failed: %s", error->message);
    return;
  }

  wp_transition_advance (WP_TRANSITION (self));
}

static void
on_settings_plugin_ready (WpPlugin *s, GAsyncResult *res, gpointer data)
{
  WpInitTransition *self = WP_INIT_TRANSITION (data);
  WpTransition *transition = WP_TRANSITION (data);
  WpCore *core = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;
  g_autoptr (WpSettings) settings = wp_settings_get_instance (core, NULL);

  wp_info_object (self, "wpsettingsplugin object ready");

  if (!wp_object_activate_finish (WP_OBJECT (s), res, &error)) {
    wp_debug_object (self, "wpSettingsPlugin activation failed: %s",
      error->message);
    return;
  }

  wp_object_activate (WP_OBJECT (settings), WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_settings_ready, self);

}

static void
wp_init_transition_execute_step (WpTransition * transition, guint step)
{
  WpInitTransition *self = WP_INIT_TRANSITION (transition);
  WpCore *core = wp_transition_get_source_object (transition);
  struct pw_context *pw_ctx = wp_core_get_pw_context (core);
  const struct pw_properties *props = pw_context_get_properties (pw_ctx);
  GError *error = NULL;

  switch (step) {

  case STEP_CONNECT: {
    wp_info_object (self, "Core connect...");

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

  case STEP_ACTIVATE_SETTINGS: {

    wp_info_object (self, "Activating settings...");

    /* load settings module */
    if (!wp_core_load_component (core, "libwireplumber-module-settings",
        "module", NULL, &error)) {
      wp_transition_return_error (transition, error);
      return;
    }

    /* get handle to module to settings module/plugin & activate it */
    WpPlugin *p = wp_plugin_find (core, "settings");
    if (!p) {
      wp_transition_return_error (transition, g_error_new (
          WP_DOMAIN_DAEMON, WP_EXIT_CONFIG,
          "unable to find settings plugin"));
      return;
    }

    wp_object_activate (WP_OBJECT (p), WP_OBJECT_FEATURES_ALL, NULL,
        (GAsyncReadyCallback) on_settings_plugin_ready, self);

    break;
  }

  case STEP_LOAD_COMPONENTS: {
    struct data data = { .transition = transition };

    wp_info_object (self, "Load Wireplumber Components...");

    if (pw_context_conf_section_for_each(pw_ctx, "wireplumber.components",
		    do_load_components, &data) < 0)
	    return;
    if (data.count == 0) {
      wp_transition_return_error (transition, g_error_new (
          WP_DOMAIN_DAEMON, WP_EXIT_CONFIG,
          "No components configured in the context conf file; nothing to do"));
      return;
    }
    wp_transition_advance (transition);
    break;
  }

  case STEP_CHECK_MEDIA_SESSION: {
    wp_info_object (self, "Checking for session manager conflicts...");

    self->om = wp_object_manager_new ();
    wp_object_manager_add_interest (self->om, WP_TYPE_CLIENT,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
        "application.name", "=s", "pipewire-media-session", NULL);
    g_signal_connect_object (self->om, "installed",
        G_CALLBACK (check_media_session), self, 0);
    wp_core_install_object_manager (core, self->om);
    break;
  }

  case STEP_ACTIVATE_PLUGINS: {
    const char *engine = pw_properties_get (props, "wireplumber.script-engine");

    g_clear_object (&self->om);
    wp_info_object (self, "Activating plugins...");

    self->om = wp_object_manager_new ();
    if (engine) {
      wp_object_manager_add_interest (self->om, WP_TYPE_PLUGIN,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "name", "!s", engine,
          NULL);
    } else {
      wp_object_manager_add_interest (self->om, WP_TYPE_PLUGIN, NULL);
    }
    g_signal_connect_object (self->om, "object-added",
        G_CALLBACK (on_plugin_added), self, 0);
    g_signal_connect_object (self->om, "installed",
        G_CALLBACK (wp_transition_advance), transition, G_CONNECT_SWAPPED);
    wp_core_install_object_manager (core, self->om);
    break;
  }

  case STEP_ACTIVATE_SCRIPTS: {
    const char *engine = pw_properties_get (props, "wireplumber.script-engine");

    g_clear_object (&self->om);

    if (engine) {
      wp_info_object (self, "Executing scripts...");

      g_autoptr (WpPlugin) plugin = wp_plugin_find (core, engine);
      if (!plugin) {
        wp_transition_return_error (transition, g_error_new (
            WP_DOMAIN_DAEMON, WP_EXIT_CONFIG,
            "script engine '%s' is not loaded", engine));
        return;
      }

      self->pending_plugins = 1;

      self->om = wp_object_manager_new ();
      wp_object_manager_add_interest (self->om, WP_TYPE_PLUGIN,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "name", "#s", "script:*",
          NULL);
      g_signal_connect_object (self->om, "object-added",
          G_CALLBACK (on_plugin_added), self, 0);
      wp_core_install_object_manager (core, self->om);

      wp_object_activate (WP_OBJECT (plugin), WP_PLUGIN_FEATURE_ENABLED, NULL,
          (GAsyncReadyCallback) on_plugin_activated, self);
    } else {
      wp_transition_advance (transition);
    }
    break;
  }

  case STEP_CLEANUP:
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
  wp_message ("disconnected from pipewire");
  daemon_exit (d, WP_EXIT_OK);
}

static gboolean
signal_handler (int signal, gpointer data)
{
  WpDaemon *d = data;
  wp_message ("stopped by signal: %s", strsignal (signal));
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
  g_autofree gchar *config_file_path = NULL;

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

  config_file_path = wp_find_file (
      WP_LOOKUP_DIR_ENV_CONFIG |
      WP_LOOKUP_DIR_XDG_CONFIG_HOME |
      WP_LOOKUP_DIR_ETC |
      WP_LOOKUP_DIR_PREFIX_SHARE,
      config_file, NULL);
  if (config_file_path == NULL) {
    fprintf (stderr, "Unable to find the required configuration file %s\n",
             config_file);
    return WP_EXIT_CONFIG;
  }

  properties = wp_properties_new (
      PW_KEY_CONFIG_NAME, config_file_path,
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
