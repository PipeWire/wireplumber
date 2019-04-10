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

#ifndef __WP_ERROR_H__
#define __WP_ERROR_H__

#include <glib.h>

G_BEGIN_DECLS

GQuark wp_domain_library_quark (void);
#define WP_DOMAIN_LIBRARY (wp_domain_library_quark ())

typedef enum {
  WP_LIBRARY_ERROR_INVARIANT,
} WpLibraryErrorEnum;

G_END_DECLS

#endif
