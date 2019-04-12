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

G_DECLARE_FINAL_TYPE (WpCore, wp_core, WP, CORE, GObject);

WpCore * wp_core_get_instance (void);
void wp_core_run (WpCore * self, GError ** error);

void wp_core_exit (WpCore * self, GQuark domain, gint code,
    const gchar *format, ...);

G_END_DECLS

#endif
