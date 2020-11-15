/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpModule
 *
 * A module is a shared library that can be loaded dynamically in the
 * WirePlumber daemon process, adding functionality to the daemon.
 *
 * For every module that is loaded, WirePlumber constructs a #WpModule object
 * that gets registered on the #WpCore and can be retrieved through the
 * #WpObjectManager API.
 *
 * Every module has to conform to a certain interface in order for WirePlumber
 * to know how to load it. This interface is called "ABI" in #WpModule.
 * Currently there is only one possible ABI, the "C" one.
 *
 * ### Writing modules in C
 *
 * In order to define a module in C, you need to implement a function in
 * your shared library that has this signature:
 *
 * |[
 * WP_PLUGIN_EXPORT void
 * wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
 * ]|
 *
 * This function will be called once at the time of loading the module. The
 * @args parameter is a dictionary ("a{sv}") #GVariant that contains arguments
 * for this module that were specified in WirePlumber's configuration file.
 * The @module parameter is useful for registering a destroy callback (using
 * wp_module_set_destroy_callback()), which will be called at the time the
 * module is destroyed (when WirePlumber quits) and allows you to free any
 * resources that the module has allocated.
 */

#define G_LOG_DOMAIN "wp-module"

#include "module.h"
#include "debug.h"
#include "error.h"
#include "private/registry.h"
#include <gmodule.h>

#define WP_MODULE_INIT_SYMBOL "wireplumber__module_init"

typedef void (*WpModuleInitFunc) (WpModule *, WpCore *, GVariant *);

struct _WpModule
{
  GObject parent;

  GWeakRef core;
  GVariant *properties;
  GDestroyNotify destroy;
  gpointer destroy_data;
};

G_DEFINE_TYPE (WpModule, wp_module, G_TYPE_OBJECT)

static void
wp_module_init (WpModule * self)
{
}

static void
wp_module_finalize (GObject * object)
{
  WpModule *self = WP_MODULE (object);

  wp_trace_object (self, "unloading module");

  if (self->destroy)
    self->destroy (self->destroy_data);
  g_clear_pointer (&self->properties, g_variant_unref);
  g_weak_ref_clear (&self->core);

  G_OBJECT_CLASS (wp_module_parent_class)->finalize (object);
}

static void
wp_module_class_init (WpModuleClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;
  object_class->finalize = wp_module_finalize;
}

static const gchar *
get_module_dir (void)
{
  static const gchar *module_dir = NULL;
  if (!module_dir) {
    module_dir = g_getenv ("WIREPLUMBER_MODULE_DIR");
    if (!module_dir)
      module_dir = WIREPLUMBER_DEFAULT_MODULE_DIR;
  }
  return module_dir;
}

static gboolean
wp_module_load_c (WpModule * self, WpCore * core,
    const gchar * module_name, GVariant * args, GError ** error)
{
  g_autofree gchar *module_path = NULL;
  GModule *gmodule;
  gpointer module_init;
  GVariantDict properties;

  module_path = g_module_build_path (get_module_dir (), module_name);
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

  g_variant_dict_init (&properties, NULL);
  g_variant_dict_insert (&properties, "module.name", "s", module_name);
  g_variant_dict_insert (&properties, "module.abi", "s", "C");
  g_variant_dict_insert (&properties, "module.path", "s", module_path);
  if (args) {
    g_variant_take_ref (args);
    g_variant_dict_insert_value (&properties, "module.args", args);
  }
  self->properties = g_variant_ref_sink (g_variant_dict_end (&properties));

  ((WpModuleInitFunc) module_init) (self, core, args);

  if (args)
    g_variant_unref (args);

  return TRUE;
}

/**
 * wp_module_load:
 * @core: the core
 * @abi: the abi name of the module
 * @module_name: the module name
 * @args: (transfer floating)(nullable): additional properties passed to the
 *     module ("a{sv}")
 * @error: (out) (optional): return location for errors, or NULL to ignore
 *
 * Returns: (transfer none): the loaded module
 */
WpModule *
wp_module_load (WpCore * core, const gchar * abi, const gchar * module_name,
    GVariant * args, GError ** error)
{
  g_autoptr (WpModule) module = NULL;

  module = g_object_new (WP_TYPE_MODULE, NULL);
  g_weak_ref_init (&module->core, core);

  wp_debug_object (module, "loading module %s (ABI: %s)", module_name, abi);

  if (!g_strcmp0 (abi, "C")) {
    if (!wp_module_load_c (module, core, module_name, args, error))
      return NULL;
  } else {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "unknown module ABI %s", abi);
    return NULL;
  }

  wp_registry_register_object (wp_core_get_registry (core),
      g_object_ref (module));

  return module;
}

/**
 * wp_module_get_properties:
 * @self: the module
 *
 * Returns: (transfer none): the properties of the module ("a{sv}")
 */
GVariant *
wp_module_get_properties (WpModule * self)
{
  g_return_val_if_fail (WP_IS_MODULE (self), NULL);
  return self->properties;
}

/**
 * wp_module_get_core:
 * @self: the module
 *
 * Returns: (transfer full): the core on which this module is registered
 */
WpCore *
wp_module_get_core (WpModule * self)
{
  g_return_val_if_fail (WP_IS_MODULE (self), NULL);
  return g_weak_ref_get (&self->core);
}

/**
 * wp_module_set_destroy_callback:
 * @self: the module
 * @callback: (scope async): a function to call when the module is destroyed
 * @data: (closure): data to pass to @callback
 *
 * Registers a @callback to call when the module object is destroyed
 */
void
wp_module_set_destroy_callback (WpModule * self, GDestroyNotify callback,
    gpointer data)
{
  g_return_if_fail (self->destroy == NULL);
  self->destroy = callback;
  self->destroy_data = data;
}
