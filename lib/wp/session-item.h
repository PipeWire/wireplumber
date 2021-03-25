/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_SESSION_ITEM_H__
#define __WIREPLUMBER_SESSION_ITEM_H__

#include "object.h"
#include "proxy.h"

G_BEGIN_DECLS

/**
 * WpSessionItemFeatures:
 *
 * Flags to be used as #WpObjectFeatures for #WpSessionItem subclasses.
 */
typedef enum { /*< flags >*/
  /* main features */
  WP_SESSION_ITEM_FEATURE_ACTIVE       = (1 << 0),
  WP_SESSION_ITEM_FEATURE_EXPORTED     = (1 << 1),

  WP_SESSION_ITEM_FEATURE_CUSTOM_START = (1 << 16), /*< skip >*/
} WpSessionItemFeatures;

/**
 * WP_TYPE_SESSION_ITEM:
 *
 * The #WpSessionItem #GType
 */
#define WP_TYPE_SESSION_ITEM (wp_session_item_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpSessionItem, wp_session_item,
                          WP, SESSION_ITEM, WpObject)

/**
 * WpSessionItemClass:
 * @reset: See wp_session_item_reset()
 * @configure: See wp_session_item_configure()
 * @get_associated_proxy: See wp_session_item_get_associated_proxy()
 * @disable_active: disables the active feature of the session item
 * @disable_exported: disables the exported feature of the session item
 * @enable_active: enables the active feature of the session item
 * @enable_exported: enables the exported feature of the session item
 */
struct _WpSessionItemClass
{
  WpObjectClass parent_class;

  void (*reset) (WpSessionItem * self);
  gboolean (*configure) (WpSessionItem * self, WpProperties * props);
  gpointer (*get_associated_proxy) (WpSessionItem * self, GType proxy_type);

  void (*disable_active) (WpSessionItem * self);
  void (*disable_exported) (WpSessionItem * self);
  void (*enable_active) (WpSessionItem * self, WpTransition * transition);
  void (*enable_exported) (WpSessionItem * self, WpTransition * transition);
};

/* parent */

WP_API
WpSessionItem * wp_session_item_get_parent (WpSessionItem * self);

WP_PRIVATE_API
void wp_session_item_set_parent (WpSessionItem *self, WpSessionItem *parent);

/* Id */

WP_API
guint wp_session_item_get_id (WpSessionItem * self);

/* configuration */

WP_API
void wp_session_item_reset (WpSessionItem * self);

WP_API
gboolean wp_session_item_configure (WpSessionItem * self, WpProperties * props);

WP_API
gboolean wp_session_item_is_configured (WpSessionItem * self);

/* associated proxies */

WP_API
gpointer wp_session_item_get_associated_proxy (WpSessionItem * self,
    GType proxy_type);

WP_API
guint32 wp_session_item_get_associated_proxy_id (WpSessionItem * self,
    GType proxy_type);

/* registry */

WP_API
void wp_session_item_register (WpSessionItem * self);

WP_API
void wp_session_item_remove (WpSessionItem * self);

/* properties */

WP_API
WpProperties * wp_session_item_get_properties (WpSessionItem * self);

/* for subclasses only */

WP_API
void wp_session_item_set_properties (WpSessionItem * self, WpProperties *props);

WP_API
void wp_session_item_handle_proxy_destroyed (WpProxy * proxy,
    WpSessionItem * item);

G_END_DECLS

#endif
