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
#include "iterator.h"
#include "object-interest.h"

G_BEGIN_DECLS

/**
 * WP_TYPE_OBJECT_MANAGER:
 *
 * The #WpObjectManager #GType
 */
#define WP_TYPE_OBJECT_MANAGER (wp_object_manager_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpObjectManager, wp_object_manager, WP, OBJECT_MANAGER, GObject)

WP_API
WpObjectManager * wp_object_manager_new (void);

/* installation */

WP_API
gboolean wp_object_manager_is_installed (WpObjectManager * self);

/* interest */

WP_API
void wp_object_manager_add_interest (WpObjectManager * self,
    GType gtype, ...) G_GNUC_NULL_TERMINATED;

WP_API
void wp_object_manager_add_interest_full (WpObjectManager * self,
    WpObjectInterest * interest);

/* proxy features */

WP_API
void wp_object_manager_request_proxy_features (WpObjectManager *self,
    GType proxy_type, WpProxyFeatures wanted_features);

/* object inspection */

WP_API
guint wp_object_manager_get_n_objects (WpObjectManager * self);

WP_API
WpIterator * wp_object_manager_iterate (WpObjectManager * self);

WP_API
WpIterator * wp_object_manager_iterate_filtered (WpObjectManager * self,
    GType gtype, ...);

WP_API
WpIterator * wp_object_manager_iterate_filtered_full (WpObjectManager * self,
    WpObjectInterest * interest);

WP_API
gpointer wp_object_manager_lookup (WpObjectManager * self,
    GType gtype, ...) G_GNUC_NULL_TERMINATED;

WP_API
gpointer wp_object_manager_lookup_full (WpObjectManager * self,
    WpObjectInterest * interest);

G_END_DECLS

#endif
