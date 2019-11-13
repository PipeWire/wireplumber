/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "module.h"
#include "error.h"
#include "private.h"
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

  g_debug ("WpModule:%p unloading module", self);

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
 * @args: the args passed to the module
 * @error: return location for errors, or NULL to ignore
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

  g_info ("WpModule:%p loading module %s (ABI: %s)", module, module_name, abi);

  if (!g_strcmp0 (abi, "C")) {
    if (!wp_module_load_c (module, core, module_name, args, error))
      return NULL;
  } else {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "unknown module ABI %s", abi);
    return NULL;
  }

  wp_core_register_object (core, g_object_ref (module));

  return module;
}

GVariant *
wp_module_get_properties (WpModule * self)
{
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
  return g_weak_ref_get (&self->core);
}

void
wp_module_set_destroy_callback (WpModule * self, GDestroyNotify callback,
    gpointer data)
{
  g_return_if_fail (self->destroy == NULL);
  self->destroy = callback;
  self->destroy_data = data;
}
