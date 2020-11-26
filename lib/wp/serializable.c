/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: serializable
 * @title: Serializable Interface
 */

#define G_LOG_DOMAIN "wp-object-ifaces"

#include "serializable.h"

G_DEFINE_INTERFACE (WpSerializable, wp_serializable, G_TYPE_OBJECT)

static void
wp_serializable_default_init (WpSerializableInterface * iface)
{
}

GVariant *
wp_serializable_to_asv (WpSerializable * self)
{
  GVariantBuilder b;

  g_return_val_if_fail (WP_IS_SERIALIZABLE (self), NULL);
  g_return_val_if_fail (WP_SERIALIZABLE_GET_IFACE (self)->to_asv, NULL);

  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  WP_SERIALIZABLE_GET_IFACE (self)->to_asv (self, &b);
  return g_variant_builder_end (&b);
}
