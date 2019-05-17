/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WIREPLUMBER_FACTORY_H__
#define __WIREPLUMBER_FACTORY_H__

#include "core.h"

G_BEGIN_DECLS

#define WP_TYPE_FACTORY (wp_factory_get_type ())
G_DECLARE_FINAL_TYPE (WpFactory, wp_factory, WP, FACTORY, GObject)

typedef gpointer (*WpFactoryFunc) (WpFactory * self, GType type,
    GVariant * properties);

struct _WpFactory
{
  GObject parent;

  gchar *name;
  GQuark name_quark;
  WpFactoryFunc create_object;
};

WpFactory * wp_factory_new (const gchar * name, WpFactoryFunc func);

static inline const gchar * wp_factory_get_name (WpFactory * factory)
{
  return factory->name;
}

static inline gpointer wp_factory_create_object (WpFactory * factory,
    GType type, GVariant * properties)
{
  return factory->create_object (factory, type, properties);
}


static inline gboolean
wp_core_register_factory (WpCore * core, WpFactory * factory)
{
  return wp_core_register_global (core, factory->name_quark, factory,
      g_object_unref);
}

static inline WpFactory *
wp_core_find_factory (WpCore * core, const gchar * name)
{
  return wp_core_get_global (core, g_quark_from_string (name));
}

static inline gpointer
wp_core_make_from_factory (WpCore * core, const gchar * name, GType type,
    GVariant * properties)
{
  WpFactory *f = wp_core_find_factory (core, name);
  if (!f) return NULL;
  return wp_factory_create_object (f, type, properties);
}

G_END_DECLS

#endif
