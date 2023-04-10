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

struct _ComponentData
{
  gchar *name;
  gchar *type;
  gint priority;
  gint flags;
  WpSpaJson *deps;
};
typedef struct _ComponentData ComponentData;

static gint
component_cmp_func (const ComponentData *a, const ComponentData *b)
{
  return b->priority - a->priority;
}

static gint
component_equal_func (const ComponentData *a, ComponentData * b)
{
  return
      g_str_equal (a->name, b->name) && g_str_equal (a->type, b->type) ? 0 : 1;
}

static void
component_data_free (ComponentData *self)
{
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->type, g_free);
  g_clear_pointer (&self->deps, wp_spa_json_unref);
  g_slice_free (ComponentData, self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ComponentData, component_data_free)

enum
{
  NO_FAIL = 0x1,
  IF_EXISTS = 0x2
};

static void
on_plugin_loaded (WpCore *core, GAsyncResult *res, gpointer user_data);

/*** WpInitTransition ***/

struct _WpInitTransition
{
  WpTransition parent;
  WpObjectManager *om;
  GList *components;
  ComponentData *curr_component;
};

enum {
  STEP_CONNECT = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_CHECK_MEDIA_SESSION,
  STEP_PARSE_COMPONENTS,
  STEP_LOAD_ENABLE_COMPONENTS,
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
  case STEP_CONNECT:                return STEP_CHECK_MEDIA_SESSION;
  case STEP_CHECK_MEDIA_SESSION:    return STEP_PARSE_COMPONENTS;
  case STEP_PARSE_COMPONENTS:       return STEP_LOAD_ENABLE_COMPONENTS;
  case STEP_LOAD_ENABLE_COMPONENTS: return STEP_CLEANUP;
  case STEP_CLEANUP:                return WP_TRANSITION_STEP_NONE;

  default:
    g_return_val_if_reached (WP_TRANSITION_STEP_ERROR);
  }
}

static gboolean
component_meets_dependencies (WpCore *core, ComponentData *comp)
{
  g_autoptr (WpConf) conf = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  if (!comp->deps)
    return TRUE;

  /* Note that we consider the dependency valid by default if it is not
   * found in the settings configuration section */
  conf = wp_conf_get_instance (core);
  it = wp_spa_json_new_iterator (comp->deps);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *dep = g_value_get_boxed (&item);
    g_autofree gchar *dep_str = wp_spa_json_parse_string (dep);
    gboolean value = wp_conf_get_value_boolean (conf,
        "wireplumber.settings", dep_str, TRUE);
    if (!value)
      return FALSE;
  }

  return TRUE;
}

static gboolean
load_enable_components (WpInitTransition *self)
{
  WpCore *core = wp_transition_get_source_object (WP_TRANSITION (self));

  while (self->components) {
    self->curr_component = (ComponentData *) self->components->data;

    /* Advance */
    self->components = g_list_next (self->components);

    /* Skip component if its dependencies are not met */
    if (!component_meets_dependencies (core, self->curr_component)) {
      wp_info ("... skipping comp '%s' as its dependencies are not met",
          self->curr_component->name);
      continue;
    }

    /* Load the component */
    wp_debug ("... loading comp '%s' ('%s') with priority '%d' and flags '%x'",
        self->curr_component->name, self->curr_component->type,
        self->curr_component->priority, self->curr_component->flags);
    wp_core_load_component (core, self->curr_component->name,
        self->curr_component->type, NULL,
        (GAsyncReadyCallback) on_plugin_loaded, self);
    return FALSE;
  }

  self->curr_component = NULL;
  return TRUE;
}

static void
on_plugin_loaded (WpCore *core, GAsyncResult *res, gpointer data)
{
  WpInitTransition *self = data;
  g_autoptr (GObject) o = NULL;
  g_autoptr (GError) error = NULL;

  g_return_if_fail (self->curr_component);

  o = wp_core_load_component_finish (core, res, &error);
  if (!o) {
    if (self->curr_component->flags & IF_EXISTS &&
        error->domain == G_IO_ERROR &&
        error->code == G_IO_ERROR_NOT_FOUND) {
      wp_info ("skipping component '%s' with 'ifexists' flag because its "
          "file does not exist", self->curr_component->name);
      goto next;
    } else if (self->curr_component->flags & NO_FAIL) {
      wp_info ("skipping component '%s' with 'nofail' flag because of "
          "loading error: %s", self->curr_component->name, error->message);
      goto next;
    }

    wp_transition_return_error (WP_TRANSITION (self), g_error_new (
        WP_DOMAIN_DAEMON, WP_EXIT_CONFIG,
        "failed to activate component '%s': %s", self->curr_component->name,
        error->message));
    return;
  }

  wp_debug ("successfully enabled plugin %s",
      wp_plugin_get_name (WP_PLUGIN (o)));

next:
  /* load and enable the rest of components */
  if (load_enable_components (self))
    wp_transition_advance (WP_TRANSITION (self));
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

static gint
pick_default_component_priority (const char *type)
{
  if (g_str_equal (type, "module"))
    /* regular module default priority */
    return 110;
  else if (g_str_equal (type, "script/lua"))
    /* Lua Script default priority */
    return 100;

  return 100;
}

static void
append_json_components (GList **list, WpSpaJson *json)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  if (!wp_spa_json_is_array (json)) {
    wp_warning ("components section is not a JSON array, skipping...");
    return;
  }

  it = wp_spa_json_new_iterator (json);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *cjson = g_value_get_boxed (&item);
    g_autoptr (ComponentData) comp = g_slice_new0 (ComponentData);
    g_autoptr (WpSpaJson) deps = NULL;
    g_autoptr (WpSpaJson) flags = NULL;

    /* Parse name and type (mandatory) */
    if (!wp_spa_json_is_object (cjson) ||
        !wp_spa_json_object_get (cjson,
            "name", "s", &comp->name,
            "type", "s", &comp->type,
            NULL)) {
      wp_warning ("component must have both a 'name' and a 'type'");
      continue;
    }

    /* Parse priority (optional) */
    if (!wp_spa_json_object_get (cjson, "priority", "i", &comp->priority,
        NULL))
      comp->priority = pick_default_component_priority (comp->type);

    /* Parse deps (optional) */
    if (wp_spa_json_object_get (cjson, "deps", "J", &deps, NULL)) {
      if (wp_spa_json_is_array (deps)) {
        comp->deps = g_steal_pointer (&deps);
      } else {
        wp_warning ("skipping component %s as its 'deps' is not a JSON array",
            comp->name);
        continue;
      }
    }

    /* Parse flags (optional) */
    if (wp_spa_json_object_get (cjson, "flags", "J", &flags, NULL)) {
      if (flags && wp_spa_json_is_array (flags)) {
        g_autoptr (WpIterator) it = wp_spa_json_new_iterator (flags);
        g_auto (GValue) item = G_VALUE_INIT;

        for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
          WpSpaJson *flag = g_value_get_boxed (&item);
          g_autofree gchar *flag_str = wp_spa_json_parse_string (flag);

          if (g_str_equal (flag_str, "ifexists"))
            comp->flags |= IF_EXISTS;
          else if (g_str_equal (flag_str, "nofail"))
            comp->flags |= NO_FAIL;
          else
            wp_warning ("flag '%s' is not valid for component '%s'", flag_str,
                comp->name);
        }
      } else {
        wp_warning ("skipping component %s as its 'flags' is not a JSON array",
            comp->name);
        continue;
      }
    }

    /* Insert component into the list if it does not exist */
    if (!g_list_find_custom (*list, comp,
        (GCompareFunc) component_equal_func)) {
      wp_trace ("appended component '%s' of type '%s' with priority '%d'",
          comp->name, comp->type, comp->priority);
      *list = g_list_insert_sorted (*list, g_steal_pointer (&comp),
          (GCompareFunc) component_cmp_func);
    } else {
      wp_debug ("ignoring component '%s' as it is already defined previously",
          comp->name);
    }
  }
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

  case STEP_PARSE_COMPONENTS: {
    g_autoptr (WpConf) conf = wp_conf_get_instance (core);
    g_autoptr (WpSpaJson) json_comps = NULL;

    wp_info_object (self, "parsing components...");

    /* Append components that are defined in the configuration section */
    json_comps = wp_conf_get_section (conf, "wireplumber.components", NULL);
    if (json_comps)
      append_json_components (&self->components, json_comps);

    wp_transition_advance (transition);
    break;
  }

  case STEP_LOAD_ENABLE_COMPONENTS:
    wp_info ("loading and enabling components...");
    if (load_enable_components (self))
      wp_transition_advance (WP_TRANSITION (self));
    break;

  case STEP_CLEANUP:
    wp_info ("wirePlumber initialized");
    G_GNUC_FALLTHROUGH;

  case WP_TRANSITION_STEP_ERROR:
    g_clear_object (&self->om);
    g_list_free_full (self->components, (GDestroyNotify) component_data_free);
    self->components = NULL;
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
