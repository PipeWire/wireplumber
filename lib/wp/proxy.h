/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WP_PROXY_H__
#define __WP_PROXY_H__

#include <glib-object.h>

G_BEGIN_DECLS

struct pw_proxy;

G_DECLARE_FINAL_TYPE (WpProxy, wp_proxy, WP, PROXY, GObject)

guint32 wp_proxy_get_id (WpProxy * self);
guint32 wp_proxy_get_parent_id (WpProxy * self);
guint32 wp_proxy_get_spa_type (WpProxy * self);
const gchar * wp_proxy_get_spa_type_string (WpProxy * self);

G_END_DECLS

#endif
