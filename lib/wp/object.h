/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WP_OBJECT_H__
#define __WP_OBJECT_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define WP_TYPE_OBJECT (wp_object_get_type ())
G_DECLARE_DERIVABLE_TYPE (WpObject, wp_object, WP, OBJECT, GObject)

struct _WpObjectClass
{
  GObjectClass parent_class;
};

gboolean wp_object_implements_interface (WpObject * self, GType interface);
gpointer wp_object_get_interface (WpObject * self, GType interface);
GType * wp_object_list_interfaces (WpObject * self, guint * n_interfaces);

gboolean wp_object_attach_interface_impl (WpObject * self, gpointer impl,
    GError ** error);

/* WpPipewireProperties */

#define WP_TYPE_PIPEWIRE_PROPERTIES (wp_pipewire_properties_get_type ())
G_DECLARE_INTERFACE (WpPipewireProperties, wp_pipewire_properties,
                     WP, PIPEWIRE_PROPERTIES, GObject)

struct _WpPipewirePropertiesInterface
{
  GTypeInterface parent;

  const gchar * (*get) (WpPipewireProperties * self, const gchar * key);
};

const gchar * wp_pipewire_properties_get (WpPipewireProperties * self,
    const gchar * key);

G_END_DECLS

#endif
