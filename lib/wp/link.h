/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_LINK_H__
#define __WIREPLUMBER_LINK_H__

#include "proxy.h"

G_BEGIN_DECLS

#define WP_TYPE_LINK (wp_link_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpLink, wp_link, WP, LINK, WpProxy)

WP_API
WpLink * wp_link_new_from_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties);

G_END_DECLS

#endif
