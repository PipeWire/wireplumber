/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROPS_H__
#define __WIREPLUMBER_PROPS_H__

#include "iterator.h"
#include "spa-pod.h"
#include "proxy.h"

G_BEGIN_DECLS

/**
 * WpPropsMode:
 * @WP_PROPS_MODE_CACHE: props are stored on the proxy and cached here
 * @WP_PROPS_MODE_STORE: props are stored here directly
 */
typedef enum {
  WP_PROPS_MODE_CACHE,
  WP_PROPS_MODE_STORE,
} WpPropsMode;

/**
 * WP_TYPE_PROPS:
 *
 * The #WpProps #GType
 */
#define WP_TYPE_PROPS (wp_props_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpProps, wp_props, WP, PROPS, GObject)

WP_API
WpProps * wp_props_new (WpPropsMode mode, WpProxy * proxy);

WP_API
void wp_props_register (WpProps * self, const gchar * name,
    const gchar * description, WpSpaPod * pod);

WP_API
void wp_props_register_from_info (WpProps * self, WpSpaPod * pod);

WP_API
WpIterator * wp_props_iterate_prop_info (WpProps * self);

WP_API
WpSpaPod * wp_props_get_all (WpProps * self);

WP_API
WpSpaPod * wp_props_get (WpProps * self, const gchar * name);

WP_API
void wp_props_set (WpProps * self, const gchar * name, WpSpaPod * value);

WP_API
void wp_props_store (WpProps * self, const gchar * name, WpSpaPod * value);

G_END_DECLS

#endif
