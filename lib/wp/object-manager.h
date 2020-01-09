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
G_DECLARE_FINAL_TYPE (WpObjectManager, wp_object_manager, WP, OBJECT_MANAGER, GObject)

WpObjectManager * wp_object_manager_new (void);

void wp_object_manager_add_proxy_interest (WpObjectManager *self,
    const gchar * iface_type, GVariant * constraints,
    WpProxyFeatures wanted_features);
void wp_object_manager_add_object_interest (WpObjectManager *self,
    GType gtype, GVariant * constraints);

GPtrArray * wp_object_manager_get_objects (WpObjectManager *self,
    GType type_filter);

G_END_DECLS

#endif
