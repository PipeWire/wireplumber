/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PORT_H__
#define __WIREPLUMBER_PORT_H__

#include "proxy.h"

G_BEGIN_DECLS

/**
 * WP_TYPE_PORT:
 *
 * The #WpPort #GType
 */
#define WP_TYPE_PORT (wp_port_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpPort, wp_port, WP, PORT, WpProxy)

G_END_DECLS

#endif
