/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "factory.h"

G_DEFINE_TYPE (WpFactory, wp_factory, G_TYPE_OBJECT)

static void
wp_factory_init (WpFactory * self)
{
}

static void
wp_factory_finalize (GObject * obj)
{
  WpFactory * self = WP_FACTORY (obj);

  g_free (self->name);

  G_OBJECT_CLASS (wp_factory_parent_class)->finalize (obj);
}

static void
wp_factory_class_init (WpFactoryClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  object_class->finalize = wp_factory_finalize;
}

WpFactory *
wp_factory_new (const gchar * name, WpFactoryFunc func)
{
  WpFactory *f;

  g_return_val_if_fail (name != NULL && *name != '\0', NULL);
  g_return_val_if_fail (func != NULL, NULL);

  f = g_object_new (WP_TYPE_FACTORY, NULL);
  f->name = g_strdup (name);
  f->name_quark = g_quark_from_string (f->name);
  f->create_object = func;
  return f;
}
