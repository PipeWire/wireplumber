/* WirePlumber
 *
 * Copyright © 2023 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "internal-comp-loader.h"
#include "wp.h"
#include "registry.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-internal-comp-loader")

/*** ComponentData ***/

enum
{
  NO_FAIL = 0x1,
  IF_EXISTS = 0x2
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

/*** components parser ***/

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
json_to_components_list (GList **list, WpSpaJson *json)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

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

/*** WpComponentArrayLoadTask ***/

struct _WpComponentArrayLoadTask
{
  WpTransition parent;
  WpSpaJson *json;
  GList *components;
  GList *components_iter;
  ComponentData *curr_component;
};

enum {
  STEP_PARSE = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_LOAD_NEXT_1,
  STEP_LOAD_NEXT_2,
  STEP_CLEANUP,
};

G_DECLARE_FINAL_TYPE (WpComponentArrayLoadTask, wp_component_array_load_task,
                      WP, COMPONENT_ARRAY_LOAD_TASK, WpTransition)
G_DEFINE_TYPE (WpComponentArrayLoadTask, wp_component_array_load_task,
               WP_TYPE_TRANSITION)

static void
wp_component_array_load_task_init (WpComponentArrayLoadTask * self)
{
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

static guint
wp_component_array_load_task_get_next_step (WpTransition * transition, guint step)
{
  WpComponentArrayLoadTask *self = WP_COMPONENT_ARRAY_LOAD_TASK (transition);

  switch (step) {
  case WP_TRANSITION_STEP_NONE:     return STEP_PARSE;
  case STEP_PARSE:                  return STEP_LOAD_NEXT_1;
  case STEP_LOAD_NEXT_1:
    return (self->components_iter) ? STEP_LOAD_NEXT_2 : STEP_CLEANUP;
  case STEP_LOAD_NEXT_2:
    return (self->components_iter) ? STEP_LOAD_NEXT_1 : STEP_CLEANUP;
  case STEP_CLEANUP:                return WP_TRANSITION_STEP_NONE;
  default:
    g_return_val_if_reached (WP_TRANSITION_STEP_ERROR);
  }
}

static void
on_component_loaded (WpCore *core, GAsyncResult *res, gpointer data)
{
  WpComponentArrayLoadTask *self = WP_COMPONENT_ARRAY_LOAD_TASK (data);
  g_autoptr (GError) error = NULL;

  g_return_if_fail (self->curr_component);

  if (!wp_core_load_component_finish (core, res, &error)) {
    if (self->curr_component->flags & IF_EXISTS &&
        error->domain == G_IO_ERROR &&
        error->code == G_IO_ERROR_NOT_FOUND) {
      wp_info_object (self, "skipping component '%s' with 'ifexists' flag "
          "because the file does not exist", self->curr_component->name);
      goto next;
    } else if (self->curr_component->flags & NO_FAIL) {
      wp_info_object (self, "skipping component '%s' with 'nofail' flag "
          "due to error: %s", self->curr_component->name, error->message);
      goto next;
    }

    wp_transition_return_error (WP_TRANSITION (self), g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "failed to activate component '%s': %s", self->curr_component->name,
        error->message));
    return;
  }

next:
  wp_transition_advance (WP_TRANSITION (self));
}

static void
wp_component_array_load_task_execute_step (WpTransition * transition, guint step)
{
  WpComponentArrayLoadTask *self = WP_COMPONENT_ARRAY_LOAD_TASK (transition);
  WpCore *core = wp_transition_get_source_object (transition);

  switch (step) {
  case STEP_PARSE:
    if (!wp_spa_json_is_array (self->json)) {
      wp_transition_return_error (transition, g_error_new (
          WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "components section is not a JSON array"));
      return;
    }

    json_to_components_list (&self->components, self->json);
    self->components_iter = g_list_first (self->components);
    wp_transition_advance (transition);
    break;

  case STEP_LOAD_NEXT_1:
  case STEP_LOAD_NEXT_2:
    self->curr_component = (ComponentData *) self->components_iter->data;

    /* Advance iterator */
    self->components_iter = g_list_next (self->components_iter);

    /* Skip component if its dependencies are not met */
    if (!component_meets_dependencies (core, self->curr_component)) {
      wp_info_object (self, "... skipping component '%s' as its dependencies "
          "are not met", self->curr_component->name);
      wp_transition_advance (transition);
      return;
    }

    /* Load the component */
    wp_debug_object (self,
        "... loading component '%s' ('%s') with priority '%d' and flags '%x'",
        self->curr_component->name, self->curr_component->type,
        self->curr_component->priority, self->curr_component->flags);
    wp_core_load_component (core, self->curr_component->name,
        self->curr_component->type, NULL, NULL, NULL,
        (GAsyncReadyCallback) on_component_loaded, self);
    break;

  case STEP_CLEANUP:
  case WP_TRANSITION_STEP_ERROR:
    g_list_free_full (g_steal_pointer (&self->components),
        (GDestroyNotify) component_data_free);
    g_clear_pointer (&self->json, wp_spa_json_unref);
    break;

  default:
    g_assert_not_reached ();
  }
}

static void
wp_component_array_load_task_class_init (WpComponentArrayLoadTaskClass * klass)
{
  WpTransitionClass * transition_class = (WpTransitionClass *) klass;
  transition_class->get_next_step = wp_component_array_load_task_get_next_step;
  transition_class->execute_step = wp_component_array_load_task_execute_step;
}

static WpTransition *
wp_component_array_load_task_new (WpSpaJson *json,
    gpointer source_object, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer callback_data)
{
  WpTransition *t = wp_transition_new (wp_component_array_load_task_get_type (),
      source_object, cancellable, callback, callback_data);
  WpComponentArrayLoadTask *task = WP_COMPONENT_ARRAY_LOAD_TASK (t);
  task->json = wp_spa_json_ref (json);
  return t;
}

/*** WpInternalCompLoader ***/

struct _WpInternalCompLoader
{
  GObject parent;
};
static void wp_internal_comp_loader_iface_init (WpComponentLoaderInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpInternalCompLoader, wp_internal_comp_loader,
                         G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (
                            WP_TYPE_COMPONENT_LOADER,
                            wp_internal_comp_loader_iface_init))

#define WP_MODULE_INIT_SYMBOL "wireplumber__module_init"
typedef GObject * (*WpModuleInitFunc) (WpCore *, WpSpaJson *, GError **);

static void
wp_internal_comp_loader_init (WpInternalCompLoader * self)
{
}

static void
wp_internal_comp_loader_class_init (WpInternalCompLoaderClass * klass)
{
}

static GObject *
load_module (WpCore * core, const gchar * module_name, WpSpaJson * args,
    GError ** error)
{
  g_autofree gchar *module_path = NULL;
  GModule *gmodule;
  gpointer module_init;

  if (!g_file_test (module_name, G_FILE_TEST_EXISTS))
    module_path = g_module_build_path (wp_get_module_dir (), module_name);
  else
    module_path = g_strdup (module_name);

  wp_trace_object (core, "loading %s from %s", module_name, module_path);

  gmodule = g_module_open (module_path, G_MODULE_BIND_LOCAL);
  if (!gmodule) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "Failed to open %s: %s", module_path, g_module_error ());
    return NULL;
  }

  if (!g_module_symbol (gmodule, WP_MODULE_INIT_SYMBOL, &module_init)) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "Failed to locate symbol " WP_MODULE_INIT_SYMBOL " in %s",
        module_path);
    g_module_close (gmodule);
    return NULL;
  }

  return ((WpModuleInitFunc) module_init) (core, args, error);
}

static gboolean
wp_internal_comp_loader_supports_type (WpComponentLoader * cl,
    const gchar * type)
{
  return g_str_equal (type, "module") || g_str_equal (type, "array");
}

static void
wp_internal_comp_loader_load (WpComponentLoader * self, WpCore * core,
    const gchar * component, const gchar * type, WpSpaJson * args,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer data)
{
  if (g_str_equal (type, "module")) {
    g_autoptr (GTask) task = g_task_new (self, cancellable, callback, data);
    g_autoptr (GError) error = NULL;
    g_autoptr (GObject) o = NULL;

    g_task_set_source_tag (task, wp_internal_comp_loader_load);

    /* load module */
    o = load_module (core, component, args, &error);
    if (o)
      g_task_return_pointer (task, g_steal_pointer (&o), g_object_unref);
    else
      g_task_return_error (task, g_steal_pointer (&error));
  }
  else if (g_str_equal (type, "array")) {
    WpTransition *task = wp_component_array_load_task_new (args, core,
        cancellable, callback, data);
    wp_transition_set_source_tag (task, wp_internal_comp_loader_load);
    wp_transition_advance (task);
  }
  else {
    g_assert_not_reached ();
  }
}

static GObject *
wp_internal_comp_loader_load_finish (WpComponentLoader * self,
    GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (
    g_async_result_is_tagged (res, wp_internal_comp_loader_load), NULL);

  if (G_IS_TASK (res))
    return g_task_propagate_pointer (G_TASK (res), error);
  else {
    wp_transition_finish (res, error);
    return NULL;
  }
}

static void
wp_internal_comp_loader_iface_init (WpComponentLoaderInterface * iface)
{
  iface->supports_type = wp_internal_comp_loader_supports_type;
  iface->load = wp_internal_comp_loader_load;
  iface->load_finish = wp_internal_comp_loader_load_finish;
}
