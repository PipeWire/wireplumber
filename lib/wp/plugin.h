/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PLUGIN_H__
#define __WIREPLUMBER_PLUGIN_H__

#include "module.h"

G_BEGIN_DECLS

/**
 * WP_TYPE_PLUGIN:
 *
 * The #WpPlugin #GType
 */
#define WP_TYPE_PLUGIN (wp_plugin_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpPlugin, wp_plugin, WP, PLUGIN, GObject)

/**
 * WpPluginClass:
 * @activate: See wp_plugin_activate()
 * @deactivate: See wp_plugin_deactivate()
 */
struct _WpPluginClass
{
  GObjectClass parent_class;

  void (*activate) (WpPlugin * self);
  void (*deactivate) (WpPlugin * self);
};

WP_API
void wp_plugin_register (WpPlugin * plugin);

WP_API
WpModule * wp_plugin_get_module (WpPlugin * self);

WP_API
WpCore * wp_plugin_get_core (WpPlugin * self);

WP_API
void wp_plugin_activate (WpPlugin * self);

WP_API
void wp_plugin_deactivate (WpPlugin * self);

G_END_DECLS

#endif
