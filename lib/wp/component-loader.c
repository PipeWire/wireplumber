/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "component-loader.h"
#include "wp.h"
#include "private/registry.h"
#include <pipewire/impl.h>

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

#define WP_MODULE_INIT_SYMBOL "wireplumber__module_init"
typedef GObject *(*WpModuleInitFunc) (WpCore *, WpSpaJson *, GError **);

G_DEFINE_INTERFACE (WpComponentLoader, wp_component_loader, G_TYPE_OBJECT)

static void
wp_component_loader_default_init (WpComponentLoaderInterface * iface)
{
}

static GObject *
load_module (WpCore * core, const gchar * module_name,
    WpSpaJson * args, GError ** error)
{
  g_autofree gchar *module_path = NULL;
  GModule *gmodule;
  gpointer module_init;

  if (!g_file_test (module_name, G_FILE_TEST_EXISTS))
    module_path = g_module_build_path (wp_get_module_dir (), module_name);
  else
    module_path = g_strdup (module_name);

  wp_debug_object (core, "loading module(%s) at %s", module_name, module_path);
  gmodule = g_module_open (module_path, G_MODULE_BIND_LOCAL);
  if (!gmodule) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "Failed to open module %s: %s", module_path, g_module_error ());
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
wp_component_loader_load (WpComponentLoader * self, const gchar * component,
    const gchar * type, WpSpaJson * args, GAsyncReadyCallback callback,
    gpointer data)
{
  g_return_if_fail (WP_IS_COMPONENT_LOADER (self));
  WP_COMPONENT_LOADER_GET_IFACE (self)->load (self, component, type,
      args, callback, data);
}

static void
on_object_loaded (WpObject *object, GAsyncResult *res, gpointer data)
{
  g_autoptr (GTask) task = G_TASK (data);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (object, res, &error)) {
    g_task_return_error (task, g_steal_pointer (&error));
    return;
  }

  g_task_return_pointer (task, g_object_ref (object), g_object_unref);
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
 * \param callback (scope async): the callback to call when the operation is done
 * \param data (closure): data to pass to \a callback
 */
void
wp_core_load_component (WpCore * self, const gchar * component,
    const gchar * type, WpSpaJson * args, GAsyncReadyCallback callback,
    gpointer data)
{
  g_autoptr (GTask) task = NULL;
  g_autoptr (WpComponentLoader) c = NULL;

  /* Special case for "module" component type */
  if (g_str_equal (type, "module")) {
    task = g_task_new (self, NULL, callback, data);
    g_autoptr (GError) error = NULL;
    g_autoptr (GObject) o = NULL;

    /* load Module */
    o = load_module (self, component, args, &error);
    if (!o) {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

    if (WP_IS_OBJECT (o)) {
      /* WpObject needs to be activated */
      if (WP_IS_PLUGIN (o))
        wp_plugin_register (WP_PLUGIN (g_object_ref (o)));
      wp_object_activate (WP_OBJECT (o), WP_OBJECT_FEATURES_ALL, NULL,
          (GAsyncReadyCallback) on_object_loaded, g_object_ref (task));
      return;
    } else if (WP_IS_SI_FACTORY (o)) {
      /* WpSiFactory doesn't need to be activated */
      wp_si_factory_register (self, WP_SI_FACTORY (g_object_ref (o)));
      g_task_return_pointer (task, g_object_ref (o), g_object_unref);
      return;
    }

    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "Invalid module object for component %s", component);
    return;
  }

  /* Otherwise find a component loader for that type and load the component */
  c = wp_component_loader_find (self, type);
  if (!c) {
    task = g_task_new (self, NULL, callback, data);
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "No component loader was found for components of type '%s'", type);
    return;
  }

  wp_component_loader_load (c, component, type, args, callback, data);
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
