/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CONFIG_STATIC_OBJECTS_CONTEXT_H__
#define __WIREPLUMBER_CONFIG_STATIC_OBJECTS_CONTEXT_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_TYPE_CONFIG_STATIC_OBJECTS_CONTEXT (wp_config_static_objects_context_get_type ())
G_DECLARE_FINAL_TYPE (WpConfigStaticObjectsContext, wp_config_static_objects_context,
    WP, CONFIG_STATIC_OBJECTS_CONTEXT, WpPlugin);

WpConfigStaticObjectsContext * wp_config_static_objects_context_new (WpModule * module);

G_END_DECLS

#endif
