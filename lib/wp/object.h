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

#ifndef __WP_OBJECT_H__
#define __WP_OBJECT_H__

#include "interface-impl.h"

G_BEGIN_DECLS

G_DECLARE_DERIVABLE_TYPE (WpObject, wp_object, WP, OBJECT, GObject)

struct _WpObjectClass
{
  GObjectClass parent_class;
};

gboolean wp_object_implements_interface (WpObject * self, GType interface);
GObject * wp_object_get_interface (WpObject * self, GType interface);
GType * wp_object_list_interfaces (WpObject * self, guint * n_interfaces);

gboolean wp_object_attach_interface_impl (WpObject * self,
    WpInterfaceImpl * impl, GError ** error);

G_END_DECLS

#endif
