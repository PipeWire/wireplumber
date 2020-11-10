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

/**
 * WpProxyFeatures:
 *
 * Flags to be used as #WpObjectFeatures for #WpProxy subclasses.
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

/**
 * WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL:
 *
 * The minimal feature set for proxies implementing #WpPipewireObject.
 * This is a subset of #WP_PIPEWIRE_OBJECT_FEATURES_ALL
 */
static const WpObjectFeatures WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL =
    (WP_PROXY_FEATURE_BOUND | WP_PIPEWIRE_OBJECT_FEATURE_INFO);

/**
 * WP_PIPEWIRE_OBJECT_FEATURES_ALL:
 *
 * The complete common feature set for proxies implementing #WpPipewireObject.
 * This is a subset of #WP_OBJECT_FEATURES_ALL
 */
static const WpObjectFeatures WP_PIPEWIRE_OBJECT_FEATURES_ALL =
    (WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL |
     WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS |
     WP_PIPEWIRE_OBJECT_FEATURE_PARAM_FORMAT |
     WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROFILE |
     WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PORT_CONFIG |
     WP_PIPEWIRE_OBJECT_FEATURE_PARAM_ROUTE);

/**
 * WP_TYPE_PROXY:
 *
 * The #WpProxy #GType
 */
#define WP_TYPE_PROXY (wp_proxy_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpProxy, wp_proxy, WP, PROXY, WpObject)

/**
 * WpProxyClass:
 * @pw_iface_type: the PipeWire type of the interface that is being proxied by
 *    this class (ex. `PW_TYPE_INTERFACE_Node` for #WpNode)
 * @pw_iface_version: the PipeWire version of the interface that is being
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
};

WP_API
guint32 wp_proxy_get_bound_id (WpProxy * self);

WP_API
struct pw_proxy * wp_proxy_get_pw_proxy (WpProxy * self);

/* for subclasses only */

WP_API
void wp_proxy_set_pw_proxy (WpProxy * self, struct pw_proxy * proxy);

G_END_DECLS

#endif
