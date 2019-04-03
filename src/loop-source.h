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

#ifndef __WIREPLUMBER_LOOP_SOURCE_H__
#define __WIREPLUMBER_LOOP_SOURCE_H__

#include <glib.h>
#include <pipewire/pipewire.h>

G_BEGIN_DECLS

/*
 * A GSource that integrates a pw_loop with GMainLoop.
 * Use g_source_ref/unref to manage lifetime.
 * The pw_loop is owned by the GSource.
 */

GSource * wp_loop_source_new (void);
struct pw_loop * wp_loop_source_get_loop (GSource * s);

G_END_DECLS

#endif
