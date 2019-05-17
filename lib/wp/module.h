/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WIREPLUMBER_MODULE_H__
#define __WIREPLUMBER_MODULE_H__

#include "core.h"

G_BEGIN_DECLS

#define WP_TYPE_MODULE (wp_module_get_type ())
G_DECLARE_FINAL_TYPE (WpModule, wp_module, WP, MODULE, GObject)

WpModule * wp_module_load (WpCore * core, const gchar * abi,
    const gchar * module_name, GVariant * args, GError ** error);

GVariant * wp_module_get_properties (WpModule * module);

void wp_module_set_destroy_callback (WpModule * module, GDestroyNotify callback,
    gpointer data);

G_END_DECLS

#endif
