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

typedef enum {
  FEATURE_STATE_DISABLED,
  FEATURE_STATE_OPTIONAL,
  FEATURE_STATE_REQUIRED
} FeatureState;

typedef struct _ComponentData ComponentData;
struct _ComponentData
{
  grefcount ref;
  /* an identifier for this component that is understandable by the end user */
  gchar *printable_id;
  /* the provided feature name (points to same storage as the id) or NULL */
  gchar *provides;
  /* the original state of the feature (required / optional / disabled) */
  FeatureState state;

  /* other fields extracted as-is from the json description */
  gchar *name;
  gchar *type;
  WpSpaJson *arguments;
  GPtrArray *requires;  /* value-type: string (owned) */
  GPtrArray *wants;     /* value-type: string (owned) */

  /* TRUE when the component is in the final sorted list */
  gboolean visited;
  /* one of the components that requires this one with a strong
    dependency chain (i.e. there is a required component that requires
    this one, directly or indirectly) */
  ComponentData *required_by;
};

static void component_data_free (ComponentData * self);

static ComponentData *
component_data_ref (ComponentData *self)
{
  g_ref_count_inc (&self->ref);
  return self;
}

static void
component_data_unref (ComponentData *self)
{
  if (self && g_ref_count_dec (&self->ref))
    component_data_free (self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ComponentData, component_data_unref)

static FeatureState
get_feature_state (WpProperties * dict, const gchar * feature)
{
  const gchar *value = wp_properties_get (dict, feature);

  if (!value || g_str_equal (value, "optional"))
    return FEATURE_STATE_OPTIONAL;
  else if (g_str_equal (value, "required"))
    return FEATURE_STATE_REQUIRED;
  else if (g_str_equal (value, "disabled"))
    return FEATURE_STATE_DISABLED;
  else {
    wp_warning ("invalid feature state '%s' specified in configuration for '%s'",
        value, feature);
    wp_warning ("considering '%s' to be optional", feature);
    return FEATURE_STATE_OPTIONAL;
  }
}

static ComponentData *
component_data_new_from_json (WpSpaJson * json, WpProperties * features,
    GError ** error)
{
  g_autoptr (ComponentData) comp = NULL;
  g_autoptr (WpSpaJson) deps = NULL;

  if (!wp_spa_json_is_object (json)) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "expected JSON object instead of: %.*s", (int) wp_spa_json_get_size (json),
        wp_spa_json_get_data (json));
    return NULL;
  }

  comp = g_new0 (ComponentData, 1);
  g_ref_count_init (&comp->ref);
  comp->requires = g_ptr_array_new_with_free_func (g_free);
  comp->wants = g_ptr_array_new_with_free_func (g_free);

  if (!wp_spa_json_object_get (json, "type", "s", &comp->type, NULL)) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "component 'type' is required at: %.*s", (int) wp_spa_json_get_size (json),
        wp_spa_json_get_data (json));
    return NULL;
  }

  wp_spa_json_object_get (json, "name", "s", &comp->name, NULL);
  wp_spa_json_object_get (json, "arguments", "J", &comp->arguments, NULL);

  if (wp_spa_json_object_get (json, "provides", "s", &comp->provides, NULL)) {
    comp->state = get_feature_state (features, comp->provides);
    if (comp->name) {
      comp->printable_id =
          g_strdup_printf ("%s [%s: %s]", comp->provides, comp->type, comp->name);
    } else {
      comp->printable_id = g_strdup_printf ("%s [%s]", comp->provides, comp->type);
    }
  } else {
    comp->provides = NULL;
    comp->state = FEATURE_STATE_REQUIRED;
    comp->printable_id = g_strdup_printf ("[%s: %s]", comp->type, comp->name);
  }

  if (wp_spa_json_object_get (json, "requires", "J", &deps, NULL)) {
    g_autoptr (WpIterator) it = wp_spa_json_new_iterator (deps);
    g_auto (GValue) item = G_VALUE_INIT;

    for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
      WpSpaJson *dep = g_value_get_boxed (&item);
      g_ptr_array_add (comp->requires, wp_spa_json_to_string (dep));
    }
  }

  if (wp_spa_json_object_get (json, "wants", "J", &deps, NULL)) {
    g_autoptr (WpIterator) it = wp_spa_json_new_iterator (deps);
    g_auto (GValue) item = G_VALUE_INIT;

    for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
      WpSpaJson *dep = g_value_get_boxed (&item);
      g_ptr_array_add (comp->wants, wp_spa_json_to_string (dep));
    }
  }

  return g_steal_pointer (&comp);
}

static void
component_data_free (ComponentData * self)
{
  g_clear_pointer (&self->provides, g_free);
  g_clear_pointer (&self->printable_id, g_free);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->type, g_free);
  g_clear_pointer (&self->arguments, wp_spa_json_unref);
  g_clear_pointer (&self->requires, g_ptr_array_unref);
  g_clear_pointer (&self->wants, g_ptr_array_unref);
  g_free (self);
}

/*** WpComponentArrayLoadTask ***/

struct _WpComponentArrayLoadTask
{
  WpTransition parent;
  /* the input json object */
  WpSpaJson *json;
  /* all components that provide a feature; key: comp->provides, value: comp */
  GHashTable *feat_components;
  /* the final sorted list of components to load */
  GPtrArray *components;
  /* iterator in the components array above */
  ComponentData **components_iter;
  /* the current component being loaded */
  ComponentData *curr_component;
};

enum {
  STEP_PARSE = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_GET_NEXT,
  STEP_LOAD_NEXT,
};

G_DECLARE_FINAL_TYPE (WpComponentArrayLoadTask, wp_component_array_load_task,
                      WP, COMPONENT_ARRAY_LOAD_TASK, WpTransition)
G_DEFINE_TYPE (WpComponentArrayLoadTask, wp_component_array_load_task,
               WP_TYPE_TRANSITION)

static void
wp_component_array_load_task_init (WpComponentArrayLoadTask * self)
{
}

static guint
wp_component_array_load_task_get_next_step (WpTransition * transition, guint step)
{
  WpComponentArrayLoadTask *self = WP_COMPONENT_ARRAY_LOAD_TASK (transition);

  switch (step) {
  case WP_TRANSITION_STEP_NONE:     return STEP_PARSE;
  case STEP_PARSE:                  return STEP_GET_NEXT;
  case STEP_GET_NEXT:
    return (self->curr_component) ? STEP_LOAD_NEXT : WP_TRANSITION_STEP_NONE;
  case STEP_LOAD_NEXT:              return STEP_GET_NEXT;
  default:
    g_return_val_if_reached (WP_TRANSITION_STEP_ERROR);
  }
}

static gchar *
print_dep_chain (ComponentData *comp)
{
  GString *str = g_string_new (NULL);

  while (comp->required_by) {
    comp = comp->required_by;
    g_string_prepend (str, comp->printable_id);
    if (comp->required_by)
      g_string_prepend (str, " -> ");
  }
  return g_string_free (str, FALSE);
}

static gboolean
add_component (ComponentData * comp, gboolean strongly_required,
    WpComponentArrayLoadTask * self, GError ** error)
{
  if (comp->visited || comp->state == FEATURE_STATE_DISABLED)
    return TRUE;

  comp->visited = TRUE;

  /* recursively visit all the required features */
  for (guint i = 0; i < comp->requires->len; i++) {
    const gchar *dependency = g_ptr_array_index (comp->requires, i);
    ComponentData *req_comp =
        g_hash_table_lookup (self->feat_components, dependency);
    if (!req_comp) {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "no component provides '%s', required by '%s'", dependency,
          comp->printable_id);
      return FALSE;
    }

    /* make a note if there is a strong dependency chain */
    if (strongly_required && !req_comp->required_by) {
      if (req_comp->state == FEATURE_STATE_OPTIONAL) {
        req_comp->required_by = comp;
      }
      else if (req_comp->state == FEATURE_STATE_DISABLED) {
        g_autofree gchar *dep_chain = print_dep_chain (comp);
        g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
            "component '%s' is disabled, required by %s",
            req_comp->printable_id, dep_chain);
        return FALSE;
      }
    }

    if (!add_component (req_comp, strongly_required, self, error))
      return FALSE;
  }

  /* recursively visit all the optionally wanted features */
  for (guint i = 0; i < comp->wants->len; i++) {
    const gchar *dependency = g_ptr_array_index (comp->wants, i);
    ComponentData *wanted_comp =
        g_hash_table_lookup (self->feat_components, dependency);
    if (!wanted_comp) {
      /* in theory we could ignore this, but it's most likely a typo,
         so let's be strict about it and let the user correct it */
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "no component provides '%s', wanted by '%s'", dependency,
          comp->printable_id);
      return FALSE;
    }
    if (!add_component (wanted_comp, FALSE, self, error))
      return FALSE;
  }

  /* append component to the sorted list after all its dependencies */
  g_ptr_array_add (self->components, component_data_ref (comp));
  return TRUE;
}

static WpProperties *
conf_get_features_section (WpComponentArrayLoadTask * self)
{
  WpProperties *props = wp_properties_new_empty ();
  WpCore *core = wp_transition_get_data (WP_TRANSITION (self));
  g_autoptr (WpConf) conf = wp_conf_get_instance (core);
  g_autoptr (WpSpaJson) json =
      wp_conf_get_section (conf, "wireplumber.features", NULL);
  if (json)
    wp_properties_update_from_json (props, json);
  return props;
}

static gboolean
parse_components (WpComponentArrayLoadTask * self, GError ** error)
{
  /* all the parsed components that are explicitly required */
  g_autoptr (GPtrArray) required_components = NULL;
  g_autoptr (WpProperties) conf = conf_get_features_section (self);
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  if (!wp_spa_json_is_array (self->json)) {
    g_set_error (error,
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "components section is not a JSON array");
    return FALSE;
  }

  self->feat_components = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) component_data_unref);
  self->components = g_ptr_array_new_with_free_func (
      (GDestroyNotify) component_data_unref);
  required_components = g_ptr_array_new_with_free_func (
      (GDestroyNotify) component_data_unref);

  /* first parse each component from its json description */
  it = wp_spa_json_new_iterator (self->json);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *cjson = g_value_get_boxed (&item);
    GError *e = NULL;
    g_autoptr (ComponentData) comp = NULL;

    if (!(comp = component_data_new_from_json (cjson, conf, &e))) {
      g_propagate_error (error, e);
      return FALSE;
    }

    if (comp->state == FEATURE_STATE_REQUIRED)
      g_ptr_array_add (required_components, component_data_ref (comp));

    if (comp->provides)
      g_hash_table_insert (self->feat_components, comp->provides,
          component_data_ref (comp));
  }

  /* topological sorting based on depth-first search */
  for (guint i = 0; i < required_components->len; i++) {
    ComponentData *comp = g_ptr_array_index (required_components, i);
    GError *e = NULL;
    if (!add_component (comp, TRUE, self, &e)) {
      g_propagate_error (error, e);
      return FALSE;
    }
  }

  /* terminate the array with NULL */
  g_ptr_array_add (self->components, NULL);

  /* clear feat_components, they are no longer needed */
  g_clear_pointer (&self->feat_components, g_hash_table_unref);
  return TRUE;
}

static void
on_component_loaded (WpCore *core, GAsyncResult *res, gpointer data)
{
  WpComponentArrayLoadTask *self = WP_COMPONENT_ARRAY_LOAD_TASK (data);
  g_autoptr (GError) error = NULL;

  g_return_if_fail (self->curr_component);

  if (!wp_core_load_component_finish (core, res, &error)) {
    // if it was required, fail
    if (self->curr_component->state == FEATURE_STATE_REQUIRED) {
      wp_transition_return_error (WP_TRANSITION (self), g_error_new (
          WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
          "failed to load required component '%s': %s",
          self->curr_component->printable_id, error->message));
      return;
    }
    // if it was optional, check if strongly_required
    else if (self->curr_component->state == FEATURE_STATE_OPTIONAL &&
             self->curr_component->required_by) {
      g_autofree gchar *dep_chain = print_dep_chain (self->curr_component);
      wp_transition_return_error (WP_TRANSITION (self), g_error_new (
          WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
          "failed to load component '%s' (required by %s): %s",
          self->curr_component->printable_id, dep_chain, error->message));
      return;
    }
    else {
      wp_notice_object (core, "optional component '%s' failed to load: %s",
          self->curr_component->printable_id, error->message);
    }
  }

  wp_transition_advance (WP_TRANSITION (self));
}

static void
wp_component_array_load_task_execute_step (WpTransition * transition, guint step)
{
  WpComponentArrayLoadTask *self = WP_COMPONENT_ARRAY_LOAD_TASK (transition);
  WpCore *core = wp_transition_get_data(transition);

  switch (step) {
  case STEP_PARSE: {
    g_autoptr (GError) error = NULL;
    if (parse_components (self, &error)) {
      self->components_iter =
          (ComponentData **) &g_ptr_array_index (self->components, 0);
      wp_transition_advance (transition);
    } else {
      wp_transition_return_error (transition, error);
    }
    break;
  }
  case STEP_GET_NEXT:
    /* get the next enabled component */
    do {
      self->curr_component = (ComponentData *) *self->components_iter;
      self->components_iter++;
    } while (self->curr_component &&
             self->curr_component->state == FEATURE_STATE_DISABLED);
    wp_transition_advance (transition);
    break;

  case STEP_LOAD_NEXT: {
    /* verify that dependencies have been loaded */
    gboolean dependencies_ok = TRUE;
    for (guint i = 0; i < self->curr_component->requires->len; i++) {
      const gchar *dependency =
          g_ptr_array_index (self->curr_component->requires, i);
      if (!wp_core_test_feature (core, dependency)) {
        dependencies_ok = FALSE;
        break;
      }
    }

    if (!dependencies_ok) {
      /* this component must be optional, because if it wasn't, the dependency
         failing to load would have caused an error earlier */
      g_assert (self->curr_component->state == FEATURE_STATE_OPTIONAL);
      wp_notice_object (core, "skipping component '%s' because some of its "
          "dependencies were not loaded", self->curr_component->printable_id);
      wp_transition_advance (transition);
      return;
    }

    /* Load the component */
    wp_debug_object (self, "loading component '%s'",
        self->curr_component->printable_id);
    wp_core_load_component (core, self->curr_component->name,
        self->curr_component->type, self->curr_component->arguments,
        self->curr_component->provides, NULL,
        (GAsyncReadyCallback) on_component_loaded, self);
    break;
  }
  case WP_TRANSITION_STEP_ERROR:
    break;

  default:
    g_assert_not_reached ();
  }
}

static void
wp_component_array_load_task_finalize (GObject * object)
{
  WpComponentArrayLoadTask *self = WP_COMPONENT_ARRAY_LOAD_TASK (object);

  g_clear_pointer (&self->feat_components, g_hash_table_unref);
  g_clear_pointer (&self->components, g_ptr_array_unref);
  g_clear_pointer (&self->json, wp_spa_json_unref);

  G_OBJECT_CLASS (wp_component_array_load_task_parent_class)->finalize (object);
}

static void
wp_component_array_load_task_class_init (WpComponentArrayLoadTaskClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;
  WpTransitionClass * transition_class = (WpTransitionClass *) klass;

  object_class->finalize = wp_component_array_load_task_finalize;

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
  return g_str_equal (type, "module") ||
         g_str_equal (type, "array") ||
         g_str_equal (type, "virtual");
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
    WpTransition *task = wp_component_array_load_task_new (args, self,
        cancellable, callback, data);
    wp_transition_set_data (task, g_object_ref (core), g_object_unref);
    wp_transition_set_source_tag (task, wp_internal_comp_loader_load);
    wp_transition_advance (task);
  }
  else if (g_str_equal (type, "virtual")) {
    /* dummy task, return immediately */
    g_autoptr (GTask) task = g_task_new (self, cancellable, callback, data);
    g_task_set_source_tag (task, wp_internal_comp_loader_load);
    g_task_return_pointer (task, NULL, NULL);
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
