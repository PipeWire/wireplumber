/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_FACTORY_H__
#define __WIREPLUMBER_FACTORY_H__

#include <gio/gio.h>

#include "core.h"

G_BEGIN_DECLS

/**
 * WP_TYPE_FACTORY:
 *
 * The #WpFactory #GType
 */
#define WP_TYPE_FACTORY (wp_factory_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpFactory, wp_factory, WP, FACTORY, GObject)

/**
 * WpFactoryFunc:
 * @self: the factory
 * @type: the object type to construct
 * @properties: a dictionary ("a{sv}") variant with additional properties
 * @ready: (scope async): a callback to call when the object is constructed
 *    and ready
 * @user_data: (closure): data to pass to @ready
 *
 * A function that constructs an object. Object creation is meant to be
 * asynchronous and notified through the @ready callback.
 */
typedef void (*WpFactoryFunc) (WpFactory * self, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data);

WP_API
WpFactory * wp_factory_new (WpCore * core, const gchar * name,
    WpFactoryFunc func);

WP_API
const gchar * wp_factory_get_name (WpFactory * self);

WP_API
WpCore * wp_factory_get_core (WpFactory * self);

WP_API
void wp_factory_create_object (WpFactory * self, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data);

WP_API
WpFactory * wp_factory_find (WpCore * core, const gchar * name);

WP_API
void wp_factory_make (WpCore * core, const gchar * name, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data);

G_END_DECLS

#endif
