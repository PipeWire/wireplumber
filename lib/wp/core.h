/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WIREPLUMBER_CORE_H__
#define __WIREPLUMBER_CORE_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define WP_TYPE_CORE (wp_core_get_type ())
G_DECLARE_FINAL_TYPE (WpCore, wp_core, WP, CORE, GObject)

WpCore * wp_core_new (void);

gpointer wp_core_get_global (WpCore * self, GQuark key);

gboolean wp_core_register_global (WpCore * self, GQuark key, gpointer obj,
    GDestroyNotify destroy_obj);
void wp_core_remove_global (WpCore * self, GQuark key);

/**
 * WP_GLOBAL_PW_CORE:
 * The key to access the pw_core global object
 */
#define WP_GLOBAL_PW_CORE (wp_global_pw_core_quark ())
GQuark wp_global_pw_core_quark (void);

/**
 * WP_GLOBAL_PW_REMOTE:
 * The key to access the pw_remote global object
 */
#define WP_GLOBAL_PW_REMOTE (wp_global_pw_remote_quark ())
GQuark wp_global_pw_remote_quark (void);

G_END_DECLS

#endif
