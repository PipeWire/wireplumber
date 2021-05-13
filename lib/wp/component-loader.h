/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_COMPONENT_LOADER_H__
#define __WIREPLUMBER_COMPONENT_LOADER_H__

#include "plugin.h"

G_BEGIN_DECLS

/*!
 * @memberof WpComponentLoader
 *
 * @brief The [WpComponentLoader](@ref component_loader_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_COMPONENT_LOADER (wp_component_loader_get_type ())
 * @endcode
 */
#define WP_TYPE_COMPONENT_LOADER (wp_component_loader_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpComponentLoader, wp_component_loader,
                          WP, COMPONENT_LOADER, WpPlugin)

/*!
 * @brief
 * @em parent_class
 */
struct _WpComponentLoaderClass
{
  WpPluginClass parent_class;

  gboolean (*supports_type) (WpComponentLoader * self, const gchar * type);

  gboolean (*load) (WpComponentLoader * self, const gchar * component,
      const gchar * type, GVariant * args, GError ** error);
};

G_END_DECLS

#endif
