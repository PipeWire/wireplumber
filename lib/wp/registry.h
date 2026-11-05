/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_REGISTRY_H__
#define __WIREPLUMBER_REGISTRY_H__

#include <glib-object.h>
#include "defs.h"

G_BEGIN_DECLS

typedef struct _WpCore WpCore;
typedef struct _WpObjectManager WpObjectManager;

/*!
 * \brief The WpRegistry GType
 * \ingroup wpregistry
 */
#define WP_TYPE_REGISTRY (wp_registry_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpRegistry, wp_registry, WP, REGISTRY, GObject)

WP_API
WpCore * wp_registry_get_core (WpRegistry * self);

WP_API
gpointer wp_registry_find_object (WpRegistry * self, GEqualFunc func,
    gconstpointer data);

WP_API
void wp_registry_register_object (WpRegistry * self, gpointer obj);

WP_API
void wp_registry_remove_object (WpRegistry * self, gpointer obj);

WP_API
void wp_registry_install_object_manager (WpRegistry * self,
    WpObjectManager * om);

WP_API
gboolean wp_registry_test_feature (WpRegistry * self, const gchar * feature);

G_END_DECLS

#endif
