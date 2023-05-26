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
  GObject *c = wp_registry_find_object (wp_core_get_registry (core),
      (GEqualFunc) find_component_loader_func, type);
  return c ? WP_COMPONENT_LOADER (c) : NULL;
}

static void
wp_component_loader_load (WpComponentLoader * self, WpCore * core,
    const gchar * component, const gchar * type, WpSpaJson * args,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer data)
{
  g_return_if_fail (WP_IS_COMPONENT_LOADER (self));
  WP_COMPONENT_LOADER_GET_IFACE (self)->load (self, core, component, type,
      args, cancellable, callback, data);
}

/*!
 * \brief Loads the specified \a component on \a self
 *
 * The \a type will determine which component loader to use. The following types
 * are built-in and will always work without a component loader:
 *  - "module" - Loads a WirePlumber module
 *
 * \ingroup wpcomponentloader
 * \param self the core
 * \param component the module name or file name
 * \param type the type of the component
 * \param args (transfer none)(nullable): additional arguments for the component,
 *   expected to be a JSON object
 * \param cancellable (nullable): optional GCancellable
 * \param callback (scope async): the callback to call when the operation is done
 * \param data (closure): data to pass to \a callback
 */
void
wp_core_load_component (WpCore * self, const gchar * component,
    const gchar * type, WpSpaJson * args, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer data)
{
  g_autoptr (GTask) task = NULL;
  g_autoptr (WpComponentLoader) cl = NULL;

  /* find a component loader for that type and load the component */
  cl = wp_component_loader_find (self, type);
  if (!cl) {
    task = g_task_new (self, cancellable, callback, data);
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "No component loader was found for components of type '%s'", type);
    return;
  }

  wp_component_loader_load (cl, self, component, type, args, cancellable,
      callback, data);
}

/*!
 * \brief Finishes the operation started by wp_core_load_component().
 * This is meant to be called in the callback that was passed to that method.
 *
 * \ingroup wpcomponentloader
 * \param self the component loader object
 * \param res the async result
 * \param error (out) (optional): the operation's error, if it occurred
 * \returns (transfer full): The loaded component object, or NULL if an
 *    error happened.
 */
GObject *
wp_core_load_component_finish (WpCore * self, GAsyncResult * res,
    GError ** error)
{
  gpointer o = g_task_propagate_pointer (G_TASK (res), error);
  return o ? g_object_ref (G_OBJECT (o)) : NULL;
}
