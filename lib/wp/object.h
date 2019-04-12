/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
