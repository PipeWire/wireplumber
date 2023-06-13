/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "component-loader.h"
#include "log.h"
#include "error.h"
#include "private/registry.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-comp-loader")

/*! \defgroup wpcomponentloader WpComponentLoader */
/*!
 * \struct WpComponentLoader
 *
 * An interface that provides the ability to load components.
 *
 * Components can be:
 *  - WirePlumber modules (libraries that provide WpPlugin and WpSiFactory objects)
 *  - Scripts (ex. lua scripts)
 *
 * The WirePlumber library provides built-in support for loading WirePlumber
 * modules, without a component loader. For other kinds of components,
 * a component loader is meant to be provided in by some WirePlumber module.
 * For Lua scripts specifically, a component loader is provided by the lua
 * scripting module.
 */

G_DEFINE_INTERFACE (WpComponentLoader, wp_component_loader, G_TYPE_OBJECT)

static void
wp_component_loader_default_init (WpComponentLoaderInterface * iface)
{
}

static gboolean
find_component_loader_func (gpointer cl, gpointer type)
{
  if (WP_IS_COMPONENT_LOADER (cl) &&
      (WP_COMPONENT_LOADER_GET_IFACE (cl)->supports_type (
            WP_COMPONENT_LOADER (cl), (const gchar *) type)))
    return TRUE;

  return FALSE;
}

static WpComponentLoader *
wp_component_loader_find (WpCore * core, const gchar * type)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);
  GObject *c = wp_core_find_object (core,
      (GEqualFunc) find_component_loader_func, type);
  return c ? WP_COMPONENT_LOADER (c) : NULL;
}

static void
wp_component_loader_load (WpComponentLoader * self, WpCore * core,
    const gchar * component, const gchar * type, WpSpaJson * args,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WP_COMPONENT_LOADER_GET_IFACE (self)->load (self, core, component, type,
      args, cancellable, callback, data);
}

static GObject *
wp_component_loader_load_finish (WpComponentLoader * self, GAsyncResult * res,
    GError ** error)
{
  return WP_COMPONENT_LOADER_GET_IFACE (self)->load_finish (self, res, error);
}

static void
on_object_activated (WpObject * object, GAsyncResult * res, gpointer data)
{
  g_autoptr (GTask) task = G_TASK (data);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (object, res, &error)) {
    g_task_return_error (task, g_steal_pointer (&error));
    return;
  }

  g_task_return_boolean (task, TRUE);
}

static void
on_component_loader_load_done (WpComponentLoader * cl, GAsyncResult * res,
    gpointer data)
{
  g_autoptr (GTask) task = G_TASK (data);
  g_autoptr (GError) error = NULL;
  g_autoptr (GObject) o = NULL;
  WpCore *core = g_task_get_source_object (task);
  WpRegistry *reg = wp_core_get_registry (core);
  gchar *provides = g_task_get_task_data (task);

  o = wp_component_loader_load_finish (cl, res, &error);
  if (error) {
    g_task_return_error (task, g_steal_pointer (&error));
    return;
  }

  if (provides)
    wp_registry_mark_feature_provided (reg, provides);

  if (o) {
    wp_trace_object (cl, "loaded object " WP_OBJECT_FORMAT, WP_OBJECT_ARGS (o));

    /* store object in the registry */
    wp_core_register_object (core, g_object_ref (o));

    if (WP_IS_OBJECT (o)) {
      /* WpObject needs to be activated */
      wp_object_activate (WP_OBJECT (o), WP_OBJECT_FEATURES_ALL, NULL,
          (GAsyncReadyCallback) on_object_activated, g_steal_pointer (&task));
      return;
    }
  }

  g_task_return_boolean (task, TRUE);
}

/*!
 * \brief Loads the specified \a component on \a self
 *
 * The \a type will determine which component loader to use. The following types
 * are built-in and will always work without a component loader:
 *  - "module" - Loads a WirePlumber module
 *  - "array" - Loads multiple components interpreting the \a args as a JSON
 *    array with component definitions, as they would appear in the
 *    configuration file. When this type is used, \a component is ignored and
 *    can be NULL
 *
 * \ingroup wpcomponentloader
 * \param self the core
 * \param component (nullable): the module name or file name
 * \param type the type of the component
 * \param args (transfer none)(nullable): additional arguments for the component,
 *   expected to be a JSON object
 * \param provides (nullable): the name of the feature that this component will
 *   provide if it loads successfully; this can be queried later with
 *   wp_core_test_feature()
 * \param cancellable (nullable): optional GCancellable
 * \param callback (scope async): the callback to call when the operation is done
 * \param data (closure): data to pass to \a callback
 */
void
wp_core_load_component (WpCore * self, const gchar * component,
    const gchar * type, WpSpaJson * args, const gchar * provides,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer data)
{
  g_autoptr (GTask) task = NULL;
  g_autoptr (WpComponentLoader) cl = NULL;

  task = g_task_new (self, cancellable, callback, data);
  g_task_set_source_tag (task, wp_core_load_component);

  if (provides)
    g_task_set_task_data (task, g_strdup (provides), g_free);

  /* find a component loader for that type and load the component */
  cl = wp_component_loader_find (self, type);
  if (!cl) {
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "No component loader was found for components of type '%s'", type);
    return;
  }

  wp_debug_object (self, "load '%s', type '%s', loader " WP_OBJECT_FORMAT,
      component, type, WP_OBJECT_ARGS (cl));

  wp_component_loader_load (cl, self, component, type, args, cancellable,
      (GAsyncReadyCallback) on_component_loader_load_done, g_object_ref (task));
}

/*!
 * \brief Finishes the operation started by wp_core_load_component().
 * This is meant to be called in the callback that was passed to that method.
 *
 * \ingroup wpcomponentloader
 * \param self the component loader object
 * \param res the async result
 * \param error (out) (optional): the operation's error, if it occurred
 * \returns TRUE if the requested component was loaded, FALSE otherwise
 */
gboolean
wp_core_load_component_finish (WpCore * self, GAsyncResult * res,
    GError ** error)
{
  g_return_val_if_fail (
    g_async_result_is_tagged (res, wp_core_load_component), FALSE);

  return g_task_propagate_boolean (G_TASK (res), error);
}
