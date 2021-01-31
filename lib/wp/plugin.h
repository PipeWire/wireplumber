/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PLUGIN_H__
#define __WIREPLUMBER_PLUGIN_H__

#include "object.h"

G_BEGIN_DECLS

/**
 * WpPluginFeatures:
 * @WP_PLUGIN_FEATURE_ENABLED: enables the plugin
 *
 * Flags to be used as #WpObjectFeatures for #WpPlugin subclasses.
 */
typedef enum { /*< flags >*/
  WP_PLUGIN_FEATURE_ENABLED = (1 << 0),
} WpPluginFeatures;

/**
 * WP_TYPE_PLUGIN:
 *
 * The #WpPlugin #GType
 */
#define WP_TYPE_PLUGIN (wp_plugin_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpPlugin, wp_plugin, WP, PLUGIN, WpObject)

/**
 * WpPluginClass:
 */
struct _WpPluginClass
{
  WpObjectClass parent_class;

  void (*enable) (WpPlugin * self, WpTransition * transition);
  void (*disable) (WpPlugin * self);
};

WP_API
void wp_plugin_register (WpPlugin * plugin);

WP_API
WpPlugin * wp_plugin_find (WpCore * core, const gchar * plugin_name);

WP_API
const gchar * wp_plugin_get_name (WpPlugin * self);

G_END_DECLS

#endif
