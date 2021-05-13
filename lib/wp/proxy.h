/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROXY_H__
#define __WIREPLUMBER_PROXY_H__

#include "object.h"

G_BEGIN_DECLS

struct pw_proxy;

/*!
 * @memberof WpProxy
 *
 * @section proxy_features_section WpProxyFeatures
 *
 * @brief Flags to be used as [WpObjectFeatures](@ref object_features_section) for
 * [WpProxy](@ref proxy_section) subclasses.
 */
typedef enum { /*< flags >*/
  /* standard features */
  WP_PROXY_FEATURE_BOUND                       = (1 << 0),

  /* WpPipewireObjectInterface */
  WP_PIPEWIRE_OBJECT_FEATURE_INFO              = (1 << 4),
  WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS       = (1 << 5),
  WP_PIPEWIRE_OBJECT_FEATURE_PARAM_FORMAT      = (1 << 6),
  WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROFILE     = (1 << 7),
  WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PORT_CONFIG = (1 << 8),
  WP_PIPEWIRE_OBJECT_FEATURE_PARAM_ROUTE       = (1 << 9),

  WP_PROXY_FEATURE_CUSTOM_START                = (1 << 16), /*< skip >*/
} WpProxyFeatures;

/*!
 * @memberof WpProxy
 * WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL:
 *
 * @brief The minimal feature set for proxies implementing [WpPipewireObject](@ref pipewire_object_section).
 * This is a subset of @em WP_PIPEWIRE_OBJECT_FEATURES_ALL
 */
static const WpObjectFeatures WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL =
    (WP_PROXY_FEATURE_BOUND | WP_PIPEWIRE_OBJECT_FEATURE_INFO);

/*!
 * @memberof WpProxy
 * WP_PIPEWIRE_OBJECT_FEATURES_ALL:
 *
 * @brief The complete common feature set for proxies implementing [WpPipewireObject](@ref pipewire_object_section).
 * This is a subset of @em WP_OBJECT_FEATURES_ALL
 */
static const WpObjectFeatures WP_PIPEWIRE_OBJECT_FEATURES_ALL =
    (WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL |
     WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS |
     WP_PIPEWIRE_OBJECT_FEATURE_PARAM_FORMAT |
     WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROFILE |
     WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PORT_CONFIG |
     WP_PIPEWIRE_OBJECT_FEATURE_PARAM_ROUTE);

/*!
 * @memberof WpProxy
 *
 * @brief The [WpProxy](@ref proxy_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_PROXY (wp_proxy_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_PROXY (wp_proxy_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpProxy, wp_proxy, WP, PROXY, WpObject)

/*!
 * @brief
 * @em pw_iface_type: the PipeWire type of the interface that is being proxied by
 *    this class (ex. `PW_TYPE_INTERFACE_Node` for [WpNode](@ref node_section))
 * @em pw_iface_version: the PipeWire version of the interface that is being
 *    proxied by this class
 */
struct _WpProxyClass
{
  WpObjectClass parent_class;

  const gchar * pw_iface_type;
  guint32 pw_iface_version;

  /* signals */

  void (*pw_proxy_created) (WpProxy * self, struct pw_proxy * proxy);
  void (*pw_proxy_destroyed) (WpProxy * self);
  void (*bound) (WpProxy * self, guint32 id);
  void (*error) (WpProxy * self, int seq, int res, const char *message);
};

WP_API
guint32 wp_proxy_get_bound_id (WpProxy * self);

WP_API
const gchar * wp_proxy_get_interface_type (WpProxy * self, guint32 * version);

WP_API
struct pw_proxy * wp_proxy_get_pw_proxy (WpProxy * self);

/* for subclasses only */

WP_API
void wp_proxy_set_pw_proxy (WpProxy * self, struct pw_proxy * proxy);

WP_API
void wp_proxy_watch_bind_error (WpProxy * proxy, WpTransition * transition);

G_END_DECLS

#endif
