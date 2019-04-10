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

#ifndef __WP_INTERFACE_IMPL_H__
#define __WP_INTERFACE_IMPL_H__

#include <glib-object.h>

G_BEGIN_DECLS

G_DECLARE_DERIVABLE_TYPE (WpInterfaceImpl, wp_interface_impl, WP, INTERFACE_IMPL, GObject)

struct _WpInterfaceImplClass
{
  GObjectClass parent_class;

  /**
   * get_prerequisites:
   * @self: the interface implementation instance
   * @n_prerequisites: (out): the number of elements in the returned array
   *
   * Returns: (array length=n_prerequisites) (transfer none): the types that are
   *   required by this interface implementation
   */
  GType *(*get_prerequisites) (WpInterfaceImpl * self, guint * n_prerequisites);
};

void wp_interface_impl_set_object (WpInterfaceImpl * self, GObject * object);
GObject * wp_interface_impl_get_object (WpInterfaceImpl * self);
GObject * wp_interface_impl_get_sibling (WpInterfaceImpl * self,
    GType interface);
GType * wp_interface_impl_get_prerequisites (WpInterfaceImpl * self,
    guint * n_prerequisites);

G_END_DECLS

#endif
