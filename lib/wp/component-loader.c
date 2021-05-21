/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-comp-loader"

#include "component-loader.h"
#include "wp.h"
#include "private/registry.h"
#include <pipewire/impl.h>

/*! \defgroup wpcomponentloader WpComponentLoader */
/*!
 * \struct WpComponentLoader
 *
 * The component loader is a plugin that provides the ability to load components.
 *
 * Components can be:
 *  - WirePlumber modules (libraries that provide WpPlugin and WpSiFactory objects)
 *  - PipeWire modules (libraries that extend libpipewire)
 *  - Scripts (ex. lua scripts)
 *  - Configuration files
 *
 * The WirePlumber library provides built-in support for loading WirePlumber
 * and PipeWire modules, without a component loader. For other kinds of
 * components, like the lua scripts and config files, a component loader is
 * provided in by external WirePlumber module.
 */

#define WP_MODULE_INIT_SYMBOL "wireplumber__module_init"
typedef gboolean (*WpModuleInitFunc) (WpCore *, GVariant *, GError **);

G_DEFINE_ABSTRACT_TYPE (WpComponentLoader, wp_component_loader, WP_TYPE_PLUGIN)

static void
wp_component_loader_init (WpComponentLoader * self)
{
}

static void
wp_component_loader_class_init (WpComponentLoaderClass * klass)
{
}

static gboolean
load_module (WpCore * core, const gchar * module_name,
    GVariant * args, GError ** error)
{
  g_autofree gchar *module_path = NULL;
  GModule *gmodule;
  gpointer module_init;

  module_path = g_module_build_path (wp_get_module_dir (), module_name);
  gmodule = g_module_open (module_path, G_MODULE_BIND_LOCAL);
  if (!gmodule) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "Failed to open module %s: %s", module_path, g_module_error ());
    return FALSE;
  }

  if (!g_module_symbol (gmodule, WP_MODULE_INIT_SYMBOL, &module_init)) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "Failed to locate symbol " WP_MODULE_INIT_SYMBOL " in %s",
        module_path);
    g_module_close (gmodule);
    return FALSE;
  }

  return ((WpModuleInitFunc) module_init) (core, args, error);
}

static gboolean
load_pw_module (WpCore * core, const gchar * module_name,
    GVariant * args, GError ** error)
{
  const gchar *args_str = NULL;
  if (args) {
    if (g_variant_is_of_type (args, G_VARIANT_TYPE_STRING))
      args_str = g_variant_get_string (args, NULL);

    //TODO if it proves to be useful
    //else if (g_variant_is_of_type (args, G_VARIANT_TYPE_DICTIONARY))
  }

  struct pw_impl_module *module = pw_context_load_module (
      wp_core_get_pw_context (core), module_name, args_str, NULL);
  if (!module) {
    int res = errno;
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "Failed to load pipewire module: %s", g_strerror (res));
    return FALSE;
  }
  return TRUE;
}

static gboolean
find_component_loader_func (gpointer cl, gpointer type)
{
  if (WP_IS_COMPONENT_LOADER (cl) &&
      (WP_COMPONENT_LOADER_GET_CLASS (cl)->supports_type (
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

static gboolean
wp_component_loader_load (WpComponentLoader * self, const gchar * component,
    const gchar * type, GVariant * args, GError ** error)
{
  g_return_val_if_fail (WP_IS_COMPONENT_LOADER (self), FALSE);
  return WP_COMPONENT_LOADER_GET_CLASS (self)->load (self, component, type,
      args, error);
}

/*!
 * \brief Loads the specified \a component on \a self
 *
 * The \a type will determine which component loader to use. The following types
 * are built-in and will always work without a component loader:
 *  - "module" - Loads a WirePlumber module
 *  - "pw_module" - Loads a PipeWire module
 *
 * \ingroup wpcomponentloader
 * \param self the core
 * \param component the module name or file name
 * \param type the type of the component
 * \param args (transfer floating)(nullable): additional arguments for the component,
 *   usually a dict or a string
 * \param error (out) (optional): return location for errors, or NULL to ignore
 * \returns TRUE if loaded, FALSE if there was an error
 */
gboolean
wp_core_load_component (WpCore * self, const gchar * component,
    const gchar * type, GVariant * args, GError ** error)
{
  g_autoptr (GVariant) args_ref = args ? g_variant_ref_sink (args) : NULL;

  if (!g_strcmp0 (type, "module"))
    return load_module (self, component, args_ref, error);
  else if (!g_strcmp0 (type, "pw_module"))
    return load_pw_module (self, component, args_ref, error);
  else {
    g_autoptr (WpComponentLoader) c = wp_component_loader_find (self, type);
    if (c)
      return wp_component_loader_load (c, component, type, args, error);
    else {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "No component loader was found for components of type '%s'", type);
      return FALSE;
    }
  }
}
