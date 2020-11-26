/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_SERIALIZABLE_H__
#define __WIREPLUMBER_SERIALIZABLE_H__

#include <glib-object.h>
#include "defs.h"

G_BEGIN_DECLS

/**
 * WP_TYPE_SERIALIZABLE:
 *
 * The #WpSerializable #GType
 */
#define WP_TYPE_SERIALIZABLE (wp_serializable_get_type ())
WP_API
G_DECLARE_INTERFACE (WpSerializable, wp_serializable, WP, SERIALIZABLE, GObject)

struct _WpSerializableInterface
{
  GTypeInterface interface;

  void (*to_asv) (WpSerializable * self, GVariantBuilder * b);
};

WP_API
GVariant * wp_serializable_to_asv (WpSerializable * self);

G_END_DECLS

#endif
