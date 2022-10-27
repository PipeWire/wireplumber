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
  GList *components;
};

enum {
  STEP_CONNECT = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_PARSE_COMPONENTS,
  STEP_LOAD_ENABLE_COMPONENTS,
  STEP_CHECK_MEDIA_SESSION,
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
  case STEP_CONNECT:                return STEP_PARSE_COMPONENTS;
  case STEP_PARSE_COMPONENTS:       return STEP_LOAD_ENABLE_COMPONENTS;
  case STEP_LOAD_ENABLE_COMPONENTS: return STEP_CHECK_MEDIA_SESSION;
  case STEP_CHECK_MEDIA_SESSION:    return STEP_CLEANUP;
  case STEP_CLEANUP:                return WP_TRANSITION_STEP_NONE;

  default:
    g_return_val_if_reached (WP_TRANSITION_STEP_ERROR);
  }
}

typedef struct _component_data component_data;

struct _component_data
{
  gchar *name;
  gchar *type;
  gint priority;
  gint flags;
  WpSpaJson *deps;
};

static gint
component_cmp_func (const component_data *a, const component_data *b)
{
  return b->priority - a->priority;
}

static void
component_unref (component_data *self)
{
  g_free (self->name);
  g_free (self->type);
  g_clear_object (&self->deps);
  g_slice_free (component_data, self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (component_data, component_unref)

static gint
is_component_present (const component_data *listed_cmpnt,
    const gchar *new_cmpnt_name)
{
  return !g_str_equal (listed_cmpnt->name, new_cmpnt_name);
}

enum
{
  NO_FAIL = 0x1,
  IF_EXISTS = 0x2
};

static gchar *
extract_base_name (const gchar *filepath)
{
  gchar *basename = g_path_get_basename (filepath);

  if (!basename)
    return NULL;

  if (g_str_has_prefix (basename, "libwireplumber-module-")) {
    /* strip the file extension for modules */
    basename [strlen (basename) - strlen (".so")] = '\0';
    return basename;
  } else if (g_str_has_suffix (basename, ".lua"))
    return basename;
  else
    return NULL;
}

static gchar *
extract_plugin_name (gchar *name)
{
  if (g_file_test (name, G_FILE_TEST_EXISTS)) {
    /* dangling components */
    name = extract_base_name (name);
  }
  if (g_str_has_prefix (name, "libwireplumber-module-"))
    return g_strdup (name + strlen ("libwireplumber-module-"));
  else
    return g_strdup_printf ("script:%s", name);
}

static void
on_plugin_activated (WpObject *p, GAsyncResult *res, WpInitTransition *self);

static int
load_enable_component (WpInitTransition *self, GError **error)
{
  WpCore *core = wp_transition_get_source_object (WP_TRANSITION (self));
  GList *comps = self->components;
  GList *lcomp = g_list_first (comps);

  while (lcomp) {
    component_data *comp = (component_data *) lcomp->data;
    g_autofree gchar *plugin_name = NULL;
    g_autoptr (WpPlugin) plugin = NULL;

    if (comp->deps) {
      g_autoptr (WpSettings) settings = wp_settings_get_instance (core, NULL);
      g_autoptr (WpIterator) it = wp_spa_json_new_iterator (comp->deps);
      g_auto (GValue) item = G_VALUE_INIT;
      gboolean deps_met = TRUE;

      /* Note that we consider the dependency valid by default if it is not
       * found in the settings */
      for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
        WpSpaJson *dep = g_value_get_boxed (&item);
        g_autofree gchar *setting = wp_spa_json_parse_string (dep);
        gboolean value = wp_settings_parse_boolean_safe (settings, setting,
            TRUE);
        if (!value) {
          deps_met = FALSE;
          wp_info (".. deps(%s) not met for component(%s), skip loading it",
              setting, comp->name);
          break;
        }
      }
      if (!deps_met) {
          comps = g_list_delete_link (comps, g_steal_pointer (&lcomp));
          self->components = comps;
          lcomp = g_list_first (comps);
          continue;
        }
      }

    wp_debug (".. loading component(%s) type(%s) priority(%d) flags(%x)",
        comp->name, comp->type, comp->priority, comp->flags);

    g_autoptr (GError) load_error = NULL;
    if (!wp_core_load_component (core, comp->name, comp->type, NULL,
            &load_error)) {
      wp_warning (".. error in loading component (%s)", load_error->message);
      if ((load_error->code == G_FILE_ERROR_NOENT) ||
        (load_error->code == G_FILE_ERROR_ACCES)) {

        if (comp->flags & IF_EXISTS) {
          wp_warning (".. \"ifexists\" flag set, ignore the failure");
          comps = g_list_delete_link (comps, g_steal_pointer (&lcomp));
          lcomp = g_list_first (comps);
          self->components = comps;
          continue;
        } else if (comp->flags & NO_FAIL) {
          wp_warning (".. \"nofail\" flag set, ignore the failure");
          comps = g_list_delete_link (comps, g_steal_pointer (&lcomp));
          lcomp = g_list_first (comps);
          self->components = comps;
          continue;
        }
      }
      g_propagate_error (error, g_steal_pointer (&load_error));

      return -EINVAL;
    }
    /* get handle to corresponding plugin & activate it */
    plugin_name = extract_plugin_name (comp->name);
    plugin = wp_plugin_find (core, plugin_name);

    if (!plugin) {
      g_autoptr (WpSiFactory) si = wp_si_factory_find (core, plugin_name);
      if (si) {
        /* si factory modules register factories they need not be activated */
        comps = g_list_delete_link (comps, g_steal_pointer (&lcomp));
        lcomp = g_list_first (comps);
        self->components = comps;
        wp_debug (".. enabled si module(%s)", comp->name);
        continue;
      } else {
        wp_warning (".. unable to find (%s) plugin", plugin_name);
        g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
            "unable to find %s plugin", plugin_name);
        return -EINVAL;
      }
    }
    wp_debug (".. enabling component(%s) plugin name(%s)", comp->name,
        plugin_name);

    comps = g_list_delete_link (comps, g_steal_pointer (&lcomp));
    self->components = comps;
    wp_object_activate_closure (WP_OBJECT (plugin), WP_OBJECT_FEATURES_ALL,
        NULL, g_cclosure_new_object (G_CALLBACK (on_plugin_activated),
        G_OBJECT (self)));
    return 1;
  }
  return 0;
}

static void
on_plugin_activated (WpObject *p, GAsyncResult *res, WpInitTransition *self)
{
  g_autoptr (GError) error = NULL;
  int ret = 0;

  if (!wp_object_activate_finish (p, res, &error)) {
    wp_transition_return_error (WP_TRANSITION (self), g_steal_pointer (&error));
    return;
  }

  wp_debug (".. enabled plugin %s", wp_plugin_get_name (WP_PLUGIN (p)));
  ret = load_enable_component (self, &error);
  if (ret < 0) {
    wp_transition_return_error (WP_TRANSITION (self), g_steal_pointer (&error));
  }
  else if (ret == 0)
  {
    wp_debug (".. loading components successful");
    wp_transition_advance (WP_TRANSITION (self));
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

struct data {
  WpTransition *transition;
  int count;
  GList *components;
};

static gint
pick_default_component_priority (const char *name)
{
  if (g_str_has_suffix (name, ".so"))
    /* regular module default priority */
    return 110;
  else if (g_str_has_suffix (name, ".lua"))
    /* Lua Script default priority */
    return 100;

  return 100;
}

static char *
pick_component_type (const char *name)
{
  if (g_str_has_suffix (name, ".so"))
    return g_strdup ("module");
  else if (g_str_has_suffix (name, ".lua"))
    return g_strdup ("script/lua");

  return NULL;
}

static int
do_parse_json_components (void *data, const char *location, const char *section,
    const char *str, size_t len)
{
  struct data *d = data;
  WpTransition *transition = d->transition;
  g_autoptr (WpSpaJson) json = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  json = wp_spa_json_new_from_stringn (str, len);

  if (!wp_spa_json_is_array (json)) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_DAEMON, WP_EXIT_CONFIG,
        "wireplumber.components is not a JSON array"));
    return -EINVAL;
  }

  it = wp_spa_json_new_iterator (json);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *cjson = g_value_get_boxed (&item);
    g_autoptr (component_data) component = g_slice_new0 (component_data);
    g_autoptr (WpSpaJson) deps = NULL;
    g_autoptr (WpSpaJson) flags = NULL;

    /* name and type are mandatory tags */
    if (!wp_spa_json_is_object (cjson) ||
        !wp_spa_json_object_get (cjson,
            "name", "s", &component->name,
            "type", "s", &component->type,
            NULL)) {
      wp_transition_return_error (transition, g_error_new (
          WP_DOMAIN_DAEMON, WP_EXIT_CONFIG,
          "component must have both a 'name' and a 'type'"));
      return -EINVAL;
    }

    if (!wp_spa_json_object_get (cjson, "priority", "i", &component->priority,
        NULL))
      component->priority = pick_default_component_priority (component->name);

    if (wp_spa_json_object_get (cjson, "deps", "J", &deps, NULL)) {
      if (deps && wp_spa_json_is_array (deps)) {
        component->deps = g_steal_pointer (&deps);
      } else {
        wp_warning ("deps must be an array for component(%s), skip loading it",
            component->name);
        continue;
      }
    }

    if (wp_spa_json_object_get (cjson, "flags", "J", &flags, NULL)) {
      if (flags && wp_spa_json_is_array (flags)) {
        g_autoptr (WpIterator) it = wp_spa_json_new_iterator (flags);
        g_auto (GValue) item = G_VALUE_INIT;

        for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
          WpSpaJson *flag = g_value_get_boxed (&item);
          g_autofree gchar *flag_str = wp_spa_json_parse_string (flag);

          if (g_str_equal (flag_str, "ifexists"))
            component->flags |= IF_EXISTS;
          else if (g_str_equal (flag_str, "nofail"))
            component->flags |= NO_FAIL;
          else
            wp_warning ("flag(%s) is not valid for component(%s)", flag_str,
                component->name);
        }
      } else {
        wp_warning ("flags must be an array for component(%s), skip loading it",
            component->name);
        continue;
      }
    }

    if (!g_list_find_custom (d->components, component->name,
            (GCompareFunc) is_component_present)) {
      wp_trace (".. parsed component(%s) type(%s) priority(%d) flags(%x) "
          "deps defined(%s)", component->name, component->type,
          component->priority, component->flags,
          (component->deps) ? "true" : "false");

      d->components = g_list_insert_sorted (d->components,
          g_steal_pointer (&component), (GCompareFunc) component_cmp_func);
    } else
      wp_info (".. component(%s) already present, ignore this entry",
          component->name);

    d->count++;
  }
  return 0;
}

static gboolean
do_parse_dangling_component (const GValue *item, GValue *ret, gpointer data)
{
  GList *comps = data;
  const gchar *path = g_value_dup_string (item);
  g_autofree gchar *basename = NULL;
  g_autoptr (component_data) comp = g_slice_new0 (component_data);

  comp->type = pick_component_type (path);
  comp->name = (gchar *) path;
  comp->priority = pick_default_component_priority (path);

  if (!(basename = extract_base_name (path))) {
    wp_warning (".. ignore dangling shared object(%s), it is not a wireplumber"
        " module", path);
    return TRUE;
  }

  if (!g_list_find_custom (comps, basename,
        (GCompareFunc) is_component_present)) {
    wp_debug (".. parsed dangling component(%s) type(%s)", comp->name,
        comp->type);
    comps = g_list_insert_sorted (comps, g_steal_pointer (&comp),
        (GCompareFunc) component_cmp_func);
  } else
    wp_warning (".. dangling component(%s) already present, ignore this one",
        comp->name);

  g_value_set_int (ret, g_value_get_int (ret) + 1);
  return TRUE;
}

#define CONFIG_DIRS_LOOKUP_SET \
    (WP_LOOKUP_DIR_ENV_CONFIG | \
     WP_LOOKUP_DIR_XDG_CONFIG_HOME | \
     WP_LOOKUP_DIR_ETC | \
     WP_LOOKUP_DIR_PREFIX_SHARE)

/*
 * dangling components are those not present in the json config files but
 * present in the wireplumber lookup folders.
 */
static gboolean
do_parse_dangling_components (GList *components, GError **error)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) fold_ret = G_VALUE_INIT;
  gint nfiles = 0;

  /* look for 'modules' folder in the look up folders*/
  it = wp_new_files_iterator (CONFIG_DIRS_LOOKUP_SET, "modules", ".so");

  g_value_init (&fold_ret, G_TYPE_INT);
  g_value_set_int (&fold_ret, nfiles);
  if (!wp_iterator_fold (it, do_parse_dangling_component, &fold_ret,
        components)) {
    if (error && G_VALUE_HOLDS (&fold_ret, G_TYPE_ERROR))
      *error = g_value_dup_boxed (&fold_ret);
    return FALSE;
  }
  nfiles = g_value_get_int (&fold_ret);
  if (nfiles > 0) {
    wp_info (".. parsed %d dangling modules", nfiles);
  }

  g_clear_pointer (&it, wp_iterator_unref);
  g_value_unset (&fold_ret);
  nfiles = 0;

  /* look for 'scripts' folder in the look up folders*/
  it = wp_new_files_iterator (CONFIG_DIRS_LOOKUP_SET, "scripts", ".lua");

  g_value_init (&fold_ret, G_TYPE_INT);
  g_value_set_int (&fold_ret, nfiles);
  if (!wp_iterator_fold (it, do_parse_dangling_component, &fold_ret,
        components)) {
    if (error && G_VALUE_HOLDS (&fold_ret, G_TYPE_ERROR))
      *error = g_value_dup_boxed (&fold_ret);
    return FALSE;
  }
  nfiles = g_value_get_int (&fold_ret);
  if (nfiles > 0) {
    wp_info (".. parsed %d dangling scripts", nfiles);
  }

  return TRUE;
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

  case STEP_PARSE_COMPONENTS: {
    struct data data = { .transition = transition, .components = NULL };
    GError *error = NULL;
    wp_info_object (self, "parse wireplumber components...");

    if (pw_context_conf_section_for_each (pw_ctx, "wireplumber.components",
        do_parse_json_components, &data) < 0)
      return;

    if (data.count == 0) {
      wp_transition_return_error (transition, g_error_new (
          WP_DOMAIN_DAEMON, WP_EXIT_CONFIG,
          "No components configured in the context conf file; nothing to do"));
      return;
    }

    if (!do_parse_dangling_components (data.components, &error)) {
      wp_warning ("..error in traversing dangling components (%s)",
          error->message);
      wp_transition_return_error (transition, error);
    }

    self->components = g_steal_pointer (&data.components);
    wp_transition_advance (transition);
    break;
  }

  case STEP_LOAD_ENABLE_COMPONENTS: {
    g_autoptr (GError) error = NULL;
    int ret = 0;
    wp_info ("load enable components..");

    ret = load_enable_component (self, &error);
    if (ret < 0) {
      wp_transition_return_error (transition, g_steal_pointer (&error));
    } else if (ret == 0) {
      g_set_error (&error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
          "list of components not available to load");
      wp_transition_return_error (transition, g_steal_pointer (&error));
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

  case STEP_CLEANUP:
    wp_info ("wirePlumber initialized");
    g_clear_object (&self->om);
    g_list_free_full (self->components, (GDestroyNotify) component_unref);
    break;

  case WP_TRANSITION_STEP_ERROR:
    g_clear_object (&self->om);
    g_list_free_full (self->components, (GDestroyNotify) component_unref);
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
