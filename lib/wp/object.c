/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "object.h"
#include "interface-impl.h"
#include "error.h"

typedef struct {
  GArray *iface_objects;
  GArray *iface_types;
} WpObjectPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (WpObject, wp_object, G_TYPE_OBJECT);

static void
wp_object_init (WpObject * self)
{
  WpObjectPrivate *priv = wp_object_get_instance_private (self);

  priv->iface_objects = g_array_new (FALSE, FALSE, sizeof (gpointer));
  priv->iface_types = g_array_new (FALSE, FALSE, sizeof (GType));
}

static void
wp_object_finalize (GObject * obj)
{
  WpObject *self = WP_OBJECT (obj);
  WpObjectPrivate *priv = wp_object_get_instance_private (self);
  guint i;

  for (i = 0; i < priv->iface_objects->len; i++) {
    GObject *obj = g_array_index (priv->iface_objects, GObject*, i);
    wp_interface_impl_set_object (WP_INTERFACE_IMPL (obj), NULL);
    g_object_unref (obj);
  }

  g_array_free (priv->iface_objects, TRUE);
  g_array_free (priv->iface_types, TRUE);

  G_OBJECT_CLASS (wp_object_parent_class)->finalize (obj);
}

static void
wp_object_class_init (WpObjectClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  object_class->finalize = wp_object_finalize;
}

/**
 * wp_object_implements_interface: (method)
 * @self: the object
 * @interface: an interface type
 *
 * Returns: whether the interface is implemented in this object or not
 */
gboolean
wp_object_implements_interface (WpObject * self, GType interface)
{
  WpObjectPrivate *priv = wp_object_get_instance_private (self);
  guint i;

  g_return_val_if_fail (WP_IS_OBJECT (self), FALSE);

  for (i = 0; i < priv->iface_types->len; i++) {
    GType t = g_array_index (priv->iface_types, GType, i);
    if (t == interface)
      return TRUE;
  }

  return FALSE;
}

/**
 * wp_object_get_interface: (method)
 * @self: the object
 * @interface: an interface type
 *
 * Returns: (type GObject*) (nullable) (transfer none): the object
 *    implementing @interface
 */
gpointer
wp_object_get_interface (WpObject * self, GType interface)
{
  WpObjectPrivate *priv = wp_object_get_instance_private (self);
  guint i;

  g_return_val_if_fail (WP_IS_OBJECT (self), FALSE);

  for (i = 0; i < priv->iface_objects->len; i++) {
    GObject *obj = g_array_index (priv->iface_objects, GObject*, i);
    if (g_type_is_a (G_TYPE_FROM_INSTANCE (obj), interface))
      return obj;
  }

  return NULL;
}

/**
 * wp_object_list_interfaces: (method)
 * @self: the object
 * @n_interfaces: (out): the number of elements in the returned array
 *
 * Returns: (array length=n_interfaces) (transfer none): the interface types
 *   that are implemented in this object
 */
GType *
wp_object_list_interfaces (WpObject * self, guint * n_interfaces)
{
  WpObjectPrivate *priv = wp_object_get_instance_private (self);

  g_return_val_if_fail (WP_IS_OBJECT (self), NULL);

  *n_interfaces = priv->iface_types->len;
  return (GType *) priv->iface_types->data;
}

/**
 * wp_object_attach_interface_impl: (method)
 * @self: the object
 * @impl: (type WpInterfaceImpl*) (transfer none): the interface implementation
 * @error: (out): a GError to return on failure
 *
 * Returns: TRUE one success, FALSE on error
 */
gboolean
wp_object_attach_interface_impl (WpObject * self, gpointer impl,
    GError ** error)
{
  WpObjectPrivate *priv = wp_object_get_instance_private (self);
  GType *new_ifaces;
  GType *prerequisites;
  guint n_new_ifaces;
  guint n_prerequisites, n_satisfied = 0;
  guint i, j;

  g_return_val_if_fail (WP_IS_OBJECT (self), FALSE);
  g_return_val_if_fail (WP_IS_INTERFACE_IMPL (impl), FALSE);

  new_ifaces = g_type_interfaces (G_TYPE_FROM_INSTANCE (impl),
      &n_new_ifaces);
  prerequisites = wp_interface_impl_get_prerequisites (impl, &n_prerequisites);

  for (i = 0; i < priv->iface_types->len; i++) {
    GType t = g_array_index (priv->iface_types, GType, i);

    for (j = 0; j < n_prerequisites; j++) {
      if (prerequisites[j] == t)
        n_satisfied++;
    }

    for (j = 0; j < n_new_ifaces; j++) {
      if (new_ifaces[j] == t) {
        g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "Interface %s is already provided on object %p",
            g_type_name (t), self);
        return FALSE;
      }
    }
  }

  if (n_satisfied != n_prerequisites) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "Interface implementation %p has unsatisfied requirements", impl);
    return FALSE;
  }

  g_object_ref (impl);
  g_array_append_val (priv->iface_objects, impl);
  g_array_append_vals (priv->iface_types, new_ifaces, n_new_ifaces);
  wp_interface_impl_set_object (WP_INTERFACE_IMPL (impl), self);
  return TRUE;
}

/* WpPipewireProperties */

G_DEFINE_INTERFACE (WpPipewireProperties, wp_pipewire_properties, G_TYPE_OBJECT)

static void
wp_pipewire_properties_default_init (WpPipewirePropertiesInterface * iface)
{
}

/**
 * wp_pipewire_properties_get: (virtual get)
 * @self: the interface
 * @key: the name of the property to lookup
 *
 * Return: (transfer none): The value of the underlying PipeWire object's
 *    property with this @key, or %NULL.
 */
const gchar *
wp_pipewire_properties_get (WpPipewireProperties * self, const gchar * key)
{
  WpPipewirePropertiesInterface *iface = WP_PIPEWIRE_PROPERTIES_GET_IFACE (self);

  g_return_val_if_fail (WP_IS_PIPEWIRE_PROPERTIES (self), NULL);
  g_return_val_if_fail (key != NULL, NULL);
  g_return_val_if_fail (iface->get, NULL);

  return iface->get (self, key);
}

/**
 * wp_pipewire_properties_get_as_spa_dict: (virtual get_as_spa_dict)
 * @self: the interface
 *
 * Return: (transfer none): The underlying `struct spa_dict` that holds
 *    the properties
 */
const struct spa_dict *
wp_pipewire_properties_get_as_spa_dict (WpPipewireProperties * self)
{
  WpPipewirePropertiesInterface *iface = WP_PIPEWIRE_PROPERTIES_GET_IFACE (self);

  g_return_val_if_fail (WP_IS_PIPEWIRE_PROPERTIES (self), NULL);
  g_return_val_if_fail (iface->get_as_spa_dict, NULL);

  return iface->get_as_spa_dict (self);
}
