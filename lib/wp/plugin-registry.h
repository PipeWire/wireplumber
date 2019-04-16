/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WP_PLUGIN_REGISTRY_H__
#define __WP_PLUGIN_REGISTRY_H__

#include <glib-object.h>

G_BEGIN_DECLS

/* declared in plugin.h */
typedef struct _WpPluginMetadata WpPluginMetadata;

G_DECLARE_FINAL_TYPE (WpPluginRegistry, wp_plugin_registry, WP, PLUGIN_REGISTRY, GObject)

WpPluginRegistry * wp_plugin_registry_new (void);

void wp_plugin_registry_register_with_metadata (WpPluginRegistry * self,
    GType plugin_type,
    const WpPluginMetadata * metadata,
    gsize metadata_size);

void wp_plugin_registry_register (WpPluginRegistry * self,
    GType plugin_type,
    guint16 rank,
    const gchar *name,
    const gchar *description,
    const gchar *author,
    const gchar *license,
    const gchar *version,
    const gchar *origin);


typedef gboolean (*WpPluginFunc) (gpointer plugin, gpointer data);
gboolean wp_plugin_registry_invoke_internal (WpPluginRegistry * self,
    WpPluginFunc func, gpointer data);

#define wp_plugin_registry_invoke(r, func, data) \
  G_STMT_START \
    if (!(0 ? func ((WpPlugin *) NULL, data) : \
            wp_plugin_registry_invoke_internal (r, (WpPluginFunc) func, \
                (gpointer) data))) { \
      g_warning ("No plugin handled invocation to " ##func); \
    } \
  G_STMT_END

G_END_DECLS

#endif
