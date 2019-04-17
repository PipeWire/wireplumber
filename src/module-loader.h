/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WIREPLUMBER_MODULE_LOADER_H__
#define __WIREPLUMBER_MODULE_LOADER_H__

#include <glib-object.h>
#include <wp/core-interfaces.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE (WpModuleLoader, wp_module_loader, WP, MODULE_LOADER, GObject)

WpModuleLoader * wp_module_loader_new (void);

gboolean wp_module_loader_load (WpModuleLoader * self,
    WpPluginRegistry * registry, const gchar * abi, const gchar * module_name,
    GError ** error);

G_END_DECLS

#endif
