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

#include "interface-impl.h"
#include "object.h"

typedef struct {
  GObject *object;
} WpInterfaceImplPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpInterfaceImpl, wp_interface_impl, G_TYPE_OBJECT);

static void
wp_interface_impl_init (WpInterfaceImpl * self)
{
}

static void
wp_interface_impl_finalize (GObject * obj)
{
  WpInterfaceImpl *self = WP_INTERFACE_IMPL (obj);
  WpInterfaceImplPrivate *priv = wp_interface_impl_get_instance_private (self);

  g_clear_weak_pointer (&priv->object);

  G_OBJECT_CLASS (wp_interface_impl_parent_class)->finalize (obj);
}

static void
wp_interface_impl_class_init (WpInterfaceImplClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  object_class->finalize = wp_interface_impl_finalize;
}

/**
 * wp_interface_impl_set_object: (skip)
 * @self: the interface implementation instance
 * @object: (nullable) (transfer none): the implementor
 */
void
wp_interface_impl_set_object (WpInterfaceImpl * self, GObject * object)
{
  WpInterfaceImplPrivate *priv = wp_interface_impl_get_instance_private (self);

  g_return_if_fail (WP_IS_INTERFACE_IMPL (self));
  g_return_if_fail (WP_IS_OBJECT (object));

  g_set_weak_pointer (&priv->object, object);
}

/**
 * wp_interface_impl_get_object: (method)
 * @self: the interface implementation instance
 *
 * Returns: (nullable) (transfer none): the object implementing this interface
 */
GObject *
wp_interface_impl_get_object (WpInterfaceImpl * self)
{
  WpInterfaceImplPrivate *priv = wp_interface_impl_get_instance_private (self);

  g_return_val_if_fail (WP_IS_INTERFACE_IMPL (self), NULL);

  return priv->object;
}

/**
 * wp_interface_impl_get_sibling: (method)
 * @self: the interface implementation instance
 * @interface: an interface type
 *
 * Returns: (nullable) (transfer full): the object implementing @interface
 */
GObject *
wp_interface_impl_get_sibling (WpInterfaceImpl * self, GType interface)
{
  WpInterfaceImplPrivate *priv = wp_interface_impl_get_instance_private (self);
  GObject *iface = NULL;

  g_return_val_if_fail (WP_IS_INTERFACE_IMPL (self), NULL);
  g_return_val_if_fail (G_TYPE_IS_INTERFACE (interface), NULL);

  if (g_type_is_a (G_TYPE_FROM_INSTANCE (self), interface)) {
    iface = G_OBJECT (g_object_ref (self));
  } else if (priv->object) {
    iface = wp_object_get_interface (WP_OBJECT (priv->object), interface);
  }

  return iface;
}

/**
 * wp_interface_impl_get_prerequisites: (virtual get_prerequisites)
 * @self: the interface implementation instance
 * @n_prerequisites: (out): the number of elements in the returned array
 *
 * Returns: (array length=n_prerequisites) (transfer none): the types that are
 *   required by this interface implementation
 */
GType *
wp_interface_impl_get_prerequisites (WpInterfaceImpl * self,
    guint * n_prerequisites)
{
  WpInterfaceImplClass * klass = WP_INTERFACE_IMPL_GET_CLASS (self);

  g_return_val_if_fail (WP_IS_INTERFACE_IMPL (self), NULL);
  g_return_val_if_fail (n_prerequisites != NULL, NULL);

  if (klass->get_prerequisites)
    return klass->get_prerequisites (self, n_prerequisites);

  *n_prerequisites = 0;
  return NULL;
}
