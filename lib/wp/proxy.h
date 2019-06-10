/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROXY_H__
#define __WIREPLUMBER_PROXY_H__

#include <gio/gio.h>

#include "core.h"

G_BEGIN_DECLS

#define WP_TYPE_PROXY (wp_proxy_get_type ())
G_DECLARE_DERIVABLE_TYPE (WpProxy, wp_proxy, WP, PROXY, GObject)

/* The proxy base class */
struct _WpProxyClass
{
  GObjectClass parent_class;
};

void wp_proxy_register (WpProxy * self);
WpCore *wp_proxy_get_core (WpProxy * self);
gpointer wp_proxy_get_pw_proxy (WpProxy * self);

G_END_DECLS

#endif
