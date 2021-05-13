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

/*!
 * @memberof WpSessionItem
 *
 * @brief Flags to be used as [WpObjectFeatures](@ref object_features_section) for
 * [WpSessionItem](@ref session_item_section) subclasses.
 */
typedef enum { /*< flags >*/
  /* main features */
  WP_SESSION_ITEM_FEATURE_ACTIVE       = (1 << 0),
  WP_SESSION_ITEM_FEATURE_EXPORTED     = (1 << 1),

  WP_SESSION_ITEM_FEATURE_CUSTOM_START = (1 << 16), /*< skip >*/
} WpSessionItemFeatures;

/*!
 * @memberof WpSessionItem
 *
 * @brief The [WpSessionItem](@ref session_item_section) 
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_SESSION_ITEM (wp_session_item_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_SESSION_ITEM (wp_session_item_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpSessionItem, wp_session_item,
                          WP, SESSION_ITEM, WpObject)

/*!
 * @brief
 * @em reset: See wp_session_item_reset()
 * @em configure: See wp_session_item_configure()
 * @em get_associated_proxy: See wp_session_item_get_associated_proxy()
 * @em disable_active: disables the active feature of the session item
 * @em disable_exported: disables the exported feature of the session item
 * @em enable_active: enables the active feature of the session item
 * @em enable_exported: enables the exported feature of the session item
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

WP_API
const gchar * wp_session_item_get_property (WpSessionItem * self,
    const gchar *key);

/* for subclasses only */

WP_API
void wp_session_item_set_properties (WpSessionItem * self, WpProperties *props);

WP_API
void wp_session_item_handle_proxy_destroyed (WpProxy * proxy,
    WpSessionItem * item);

G_END_DECLS

#endif
