/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "module-loader.h"
#include "utils.h"

#include <wp/plugin.h>

struct _WpModuleLoader
{
  GObject parent;
  const gchar *module_dir;
};

G_DEFINE_TYPE (WpModuleLoader, wp_module_loader, G_TYPE_OBJECT);

static void
wp_module_loader_init (WpModuleLoader * self)
{
  self->module_dir = g_getenv ("WIREPLUMBER_MODULE_DIR");
}

static void
wp_module_loader_class_init (WpModuleLoaderClass * klass)
{
}

WpModuleLoader *
wp_module_loader_new (void)
{
  return g_object_new (wp_module_loader_get_type (), NULL);
}

static gboolean
wp_module_loader_load_c (WpModuleLoader * self, WpPluginRegistry * registry,
    const gchar * module_name, GError ** error)
{
  g_autofree gchar *module_path = NULL;
  GModule *module;
  gpointer module_init;
  typedef void (*WpModuleInitFunc)(WpPluginRegistry *);

  module_path = g_module_build_path (self->module_dir, module_name);
  module = g_module_open (module_path, G_MODULE_BIND_LOCAL);
  if (!module) {
    g_set_error (error, WP_DOMAIN_CORE, WP_CODE_OPERATION_FAILED,
        "Failed to open module %s: %s", module_path, g_module_error ());
    return FALSE;
  }

  if (!g_module_symbol (module, G_STRINGIFY (WP_MODULE_INIT_SYMBOL),
          &module_init)) {
    g_set_error (error, WP_DOMAIN_CORE, WP_CODE_OPERATION_FAILED,
        "Failed to locate symbol " G_STRINGIFY (WP_MODULE_INIT_SYMBOL) " in %s",
        module_path);
    g_module_close (module);
    return FALSE;
  }

  ((WpModuleInitFunc) module_init) (registry);
  return TRUE;
}

gboolean
wp_module_loader_load (WpModuleLoader * self, WpPluginRegistry * registry,
    const gchar * abi, const gchar * module_name, GError ** error)
{
  if (!g_strcmp0 (abi, "C")) {
    return wp_module_loader_load_c (self, registry, module_name, error);
  } else {
    g_set_error (error, WP_DOMAIN_CORE, WP_CODE_INVALID_ARGUMENT,
        "unknown module ABI %s", abi);
    return FALSE;
  }
}
