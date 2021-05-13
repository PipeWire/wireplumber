/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PORT_H__
#define __WIREPLUMBER_PORT_H__

#include "global-proxy.h"

G_BEGIN_DECLS

/*!
 * @memberof @WpPort
 *
 * @brief
 * @em WP_DIRECTION_INPUT: a sink, consuming input
 * @em WP_DIRECTION_OUTPUT: a source, producing output
 *
 * The different directions the endpoint can have
 */
typedef enum {
  WP_DIRECTION_INPUT,
  WP_DIRECTION_OUTPUT,
} WpDirection;

/*!
 * @memberof @WpPort
 *
 * @brief The [WpPort](@ref port_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_PORT (wp_port_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_PORT (wp_port_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpPort, wp_port, WP, PORT, WpGlobalProxy)

WP_API
WpDirection wp_port_get_direction (WpPort * self);

G_END_DECLS

#endif
