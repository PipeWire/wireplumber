/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WP_PROXY_REGISTRY_H__
#define __WP_PROXY_REGISTRY_H__

#include <glib-object.h>

G_BEGIN_DECLS

struct pw_remote;
struct pw_registry_proxy;
typedef struct _WpProxy WpProxy;

G_DECLARE_FINAL_TYPE (WpProxyRegistry, wp_proxy_registry, WP, PROXY_REGISTRY, GObject)

WpProxyRegistry * wp_proxy_registry_new (struct pw_remote * remote);

WpProxy * wp_proxy_registry_get_proxy (WpProxyRegistry * self,
    guint32 global_id);

struct pw_remote * wp_proxy_registry_get_pw_remote (WpProxyRegistry * self);
struct pw_registry_proxy * wp_proxy_registry_get_pw_registry_proxy (
    WpProxyRegistry * self);

G_END_DECLS

#endif
