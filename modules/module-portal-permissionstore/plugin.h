/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PORTAL_PERMISSIONSTORE_PLUGIN_H__
#define __WIREPLUMBER_PORTAL_PERMISSIONSTORE_PLUGIN_H__

#include <wp/wp.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE (WpPortalPermissionStorePlugin,
    wp_portal_permissionstore_plugin, WP, PORTAL_PERMISSIONSTORE_PLUGIN,
    WpPlugin)

struct _WpPortalPermissionStorePlugin
{
  WpPlugin parent;

  WpDbus *dbus;

  guint signal_id;
};

G_END_DECLS

#endif
