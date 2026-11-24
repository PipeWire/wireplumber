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
#include "private/component-loader.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-comp-loader")

/*! \defgroup wpcomponentloader WpComponentLoader */
/*!
 * \struct WpComponentLoader
 *
 * An interface that provides the ability to load components.
 *
 * Components can be:
 *  - WirePlumber modules (libraries that provide WpPlugin objects)
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

WpComponentLoader *
wp_component_loader_find (WpCore * core, const gchar * type)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);
  GObject *c = wp_registry_find_object (wp_core_get_registry (core),
      (GEqualFunc) find_component_loader_func, type);
  return c ? WP_COMPONENT_LOADER (c) : NULL;
}

void
wp_component_loader_load (WpComponentLoader * self, WpCore * core,
    const gchar * component, const gchar * type, WpSpaJson * args,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WP_COMPONENT_LOADER_GET_IFACE (self)->load (self, core, component, type,
      args, cancellable, callback, data);
}

GObject *
wp_component_loader_load_finish (WpComponentLoader * self, GAsyncResult * res,
    GError ** error)
{
  return WP_COMPONENT_LOADER_GET_IFACE (self)->load_finish (self, res, error);
}
