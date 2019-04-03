/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
