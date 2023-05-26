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

  wp_debug_object (core, "loading %s from %s", module_name, module_path);

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

static void
on_object_activated (WpObject *object, GAsyncResult *res, gpointer data)
{
  g_autoptr (GTask) task = G_TASK (data);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (object, res, &error)) {
    g_task_return_error (task, g_steal_pointer (&error));
    return;
  }

  g_task_return_pointer (task, g_object_ref (object), g_object_unref);
}

static gboolean
wp_internal_comp_loader_supports_type (WpComponentLoader * cl,
    const gchar * type)
{
  return g_str_equal (type, "module");
}

static void
wp_internal_comp_loader_load (WpComponentLoader * self, WpCore * core,
    const gchar * component, const gchar * type, WpSpaJson * args,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer data)
{
  g_autoptr (GTask) task = g_task_new (self, cancellable, callback, data);
  g_autoptr (GError) error = NULL;
  g_autoptr (GObject) o = NULL;

  g_task_set_source_tag (task, wp_internal_comp_loader_load);

  /* load Module */
  o = load_module (core, component, args, &error);
  if (!o) {
    g_task_return_error (task, g_steal_pointer (&error));
    return;
  }

  /* store object in the registry */
  wp_registry_register_object (wp_core_get_registry (core), g_object_ref (o));

  if (WP_IS_OBJECT (o)) {
    /* WpObject needs to be activated */
    wp_object_activate (WP_OBJECT (o), WP_OBJECT_FEATURES_ALL, NULL,
        (GAsyncReadyCallback) on_object_activated, g_object_ref (task));
  }
}

static GObject *
wp_internal_comp_loader_load_finish (WpComponentLoader * self,
    GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (
    g_async_result_is_tagged (res, wp_internal_comp_loader_load), NULL);

  return g_task_propagate_pointer (G_TASK (res), error);
}

static void
wp_internal_comp_loader_iface_init (WpComponentLoaderInterface * iface)
{
  iface->supports_type = wp_internal_comp_loader_supports_type;
  iface->load = wp_internal_comp_loader_load;
  iface->load_finish = wp_internal_comp_loader_load_finish;
}
