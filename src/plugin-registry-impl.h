/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WP_PLUGIN_REGISTRY_IMPL_H__
#define __WP_PLUGIN_REGISTRY_IMPL_H__

#include <wp/core-interfaces.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE (WpPluginRegistryImpl, wp_plugin_registry_impl,
                      WP, PLUGIN_REGISTRY_IMPL, WpInterfaceImpl)

WpPluginRegistryImpl * wp_plugin_registry_impl_new (void);

void wp_plugin_registry_impl_unload (WpPluginRegistryImpl * self);

typedef gboolean (*WpPluginFunc) (gpointer plugin, gpointer data);
gboolean wp_plugin_registry_impl_invoke_internal (WpPluginRegistryImpl * self,
    WpPluginFunc func, gpointer data);

#define wp_plugin_registry_impl_invoke(r, func, data) \
  G_STMT_START { \
    (0 ? func ((WpPlugin *) NULL, data) : \
        wp_plugin_registry_impl_invoke_internal ( \
            WP_PLUGIN_REGISTRY_IMPL (r), (WpPluginFunc) func, \
            (gpointer) data)); \
  } G_STMT_END

G_END_DECLS

#endif
