/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_OBJECT_MANAGER_H__
#define __WIREPLUMBER_OBJECT_MANAGER_H__

#include <glib-object.h>
#include "proxy.h"

G_BEGIN_DECLS

typedef enum {
  WP_OBJECT_MANAGER_CONSTRAINT_PW_GLOBAL_PROPERTY,
  WP_OBJECT_MANAGER_CONSTRAINT_PW_PROPERTY,
  WP_OBJECT_MANAGER_CONSTRAINT_G_PROPERTY,
} WpObjectManagerConstraintType;

#define WP_TYPE_OBJECT_MANAGER (wp_object_manager_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpObjectManager, wp_object_manager, WP, OBJECT_MANAGER, GObject)

WP_API
WpObjectManager * wp_object_manager_new (void);

WP_API
void wp_object_manager_add_proxy_interest (WpObjectManager *self,
    GType gtype, GVariant * constraints, WpProxyFeatures wanted_features);

WP_API
void wp_object_manager_add_object_interest (WpObjectManager *self,
    GType gtype, GVariant * constraints);

WP_API
GPtrArray * wp_object_manager_get_objects (WpObjectManager *self,
    GType type_filter);

G_END_DECLS

#endif
