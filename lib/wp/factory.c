/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "factory.h"

struct _WpFactory
{
  GObject parent;

  GWeakRef core;
  gchar *name;
  GQuark name_quark;
  WpFactoryFunc create_object;
};

G_DEFINE_TYPE (WpFactory, wp_factory, G_TYPE_OBJECT)

static void
wp_factory_init (WpFactory * self)
{
}

static void
wp_factory_finalize (GObject * obj)
{
  WpFactory * self = WP_FACTORY (obj);

  g_debug ("WpFactory:%p destroying factory: %s", self, self->name);

  g_weak_ref_clear (&self->core);
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
wp_factory_new (WpCore * core, const gchar * name, WpFactoryFunc func)
{
  WpFactory *f = NULL;

  g_return_val_if_fail (name != NULL && *name != '\0', NULL);
  g_return_val_if_fail (func != NULL, NULL);

  f = g_object_new (WP_TYPE_FACTORY, NULL);
  g_weak_ref_init (&f->core, core);
  f->name = g_strdup (name);
  f->name_quark = g_quark_from_string (f->name);
  f->create_object = func;

  g_info ("WpFactory:%p new factory: %s", f, name);

  wp_core_register_global (core, WP_GLOBAL_FACTORY, f, g_object_unref);

  return f;
}

const gchar *
wp_factory_get_name (WpFactory * self)
{
  return self->name;
}

/**
 * wp_factory_get_core:
 * @self: the factory
 *
 * Returns: (transfer full): the core on which this factory is registered
 */
WpCore *
wp_factory_get_core (WpFactory * self)
{
  return g_weak_ref_get (&self->core);
}

gpointer
wp_factory_create_object (WpFactory * self, GType type, GVariant * properties)
{
  g_debug ("WpFactory:%p (%s) create object of type %s", self, self->name,
      g_type_name (type));
  return self->create_object (self, type, properties);
}

struct find_factory_data
{
  GQuark name_quark;
  WpFactory *ret;
};

static gboolean
find_factory_func (GQuark key, gpointer global, gpointer user_data)
{
  struct find_factory_data *d = user_data;

  if (key != WP_GLOBAL_FACTORY ||
      WP_FACTORY (global)->name_quark != d->name_quark)
    return WP_CORE_FOREACH_GLOBAL_CONTINUE;

  d->ret = WP_FACTORY (global);
  return WP_CORE_FOREACH_GLOBAL_DONE;
}

WpFactory *
wp_factory_find (WpCore * core, const gchar * name)
{
  struct find_factory_data d = { g_quark_from_string (name), NULL };
  wp_core_foreach_global (core, find_factory_func, &d);
  return d.ret;
}

gpointer
wp_factory_make (WpCore * core, const gchar * name, GType type,
    GVariant * properties)
{
  WpFactory *f = wp_factory_find (core, name);
  if (!f) return NULL;
  return wp_factory_create_object (f, type, properties);
}
