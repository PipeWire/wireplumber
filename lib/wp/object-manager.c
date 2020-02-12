/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "object-manager.h"
#include "private.h"
#include <pipewire/array.h>

struct interest
{
  GType g_type;
  gboolean for_proxy;
  WpProxyFeatures wanted_features;
  GVariant *constraints; // aa{sv}
};

struct _WpObjectManager
{
  GObject parent;

  GWeakRef core;

  /* array of struct interest;
    pw_array has a better API for our use case than GArray */
  struct pw_array interests;

  /* objects that we are interested in, with a strong ref */
  GPtrArray *objects;

  gboolean pending_objchanged;
};

enum {
  PROP_0,
  PROP_CORE,
};

enum {
  SIGNAL_OBJECT_ADDED,
  SIGNAL_OBJECT_REMOVED,
  SIGNAL_OBJECTS_CHANGED,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (WpObjectManager, wp_object_manager, G_TYPE_OBJECT)

static void
wp_object_manager_init (WpObjectManager * self)
{
  g_weak_ref_init (&self->core, NULL);
  pw_array_init (&self->interests, sizeof (struct interest));
  self->objects = g_ptr_array_new_with_free_func (g_object_unref);
  self->pending_objchanged = FALSE;
}

static void
wp_object_manager_finalize (GObject * object)
{
  WpObjectManager *self = WP_OBJECT_MANAGER (object);
  struct interest *i;

  g_clear_pointer (&self->objects, g_ptr_array_unref);

  pw_array_for_each (i, &self->interests) {
    g_clear_pointer (&i->constraints, g_variant_unref);
  }
  pw_array_clear (&self->interests);

  g_weak_ref_clear (&self->core);

  G_OBJECT_CLASS (wp_object_manager_parent_class)->finalize (object);
}

static void
wp_object_manager_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpObjectManager *self = WP_OBJECT_MANAGER (object);

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&self->core, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_object_manager_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpObjectManager *self = WP_OBJECT_MANAGER (object);

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&self->core));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_object_manager_class_init (WpObjectManagerClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_object_manager_finalize;
  object_class->get_property = wp_object_manager_get_property;
  object_class->set_property = wp_object_manager_set_property;

  /* Install the properties */

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The WpCore", WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_OBJECT_ADDED] = g_signal_new (
      "object-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_OBJECT);

  signals[SIGNAL_OBJECT_REMOVED] = g_signal_new (
      "object-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_OBJECT);

  signals[SIGNAL_OBJECTS_CHANGED] = g_signal_new (
      "objects-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

WpObjectManager *
wp_object_manager_new (void)
{
  return g_object_new (WP_TYPE_OBJECT_MANAGER, NULL);
}

void
wp_object_manager_add_proxy_interest (WpObjectManager *self,
    GType gtype, GVariant * constraints,
    WpProxyFeatures wanted_features)
{
  struct interest *i;

  g_return_if_fail (WP_IS_OBJECT_MANAGER (self));
  g_return_if_fail (g_type_is_a (gtype, WP_TYPE_PROXY));
  g_return_if_fail (constraints == NULL ||
      g_variant_is_of_type (constraints, G_VARIANT_TYPE ("aa{sv}")));

  /* grow the array by 1 struct interest and fill it in */
  i = pw_array_add (&self->interests, sizeof (struct interest));
  i->g_type = gtype;
  i->for_proxy = TRUE;
  i->wanted_features = wanted_features;
  i->constraints = constraints ? g_variant_ref_sink (constraints) : NULL;
}

void
wp_object_manager_add_object_interest (WpObjectManager *self,
    GType gtype, GVariant * constraints)
{
  struct interest *i;

  g_return_if_fail (WP_IS_OBJECT_MANAGER (self));
  g_return_if_fail (G_TYPE_IS_OBJECT (gtype));
  g_return_if_fail (constraints == NULL ||
      g_variant_is_of_type (constraints, G_VARIANT_TYPE ("aa{sv}")));

  /* grow the array by 1 struct interest and fill it in */
  i = pw_array_add (&self->interests, sizeof (struct interest));
  i->g_type = gtype;
  i->for_proxy = FALSE;
  i->wanted_features = 0;
  i->constraints = constraints ? g_variant_ref_sink (constraints) : NULL;
}

/**
 * wp_object_manager_get_objects:
 * @self: the object manager
 * @type_filter: a #GType filter to get only the objects that are of this type,
 *   or 0 to return all the objects
 *
 * Returns: (transfer full) (element-type GObject*): all the objects managed
 *   by this #WpObjectManager that match the @type_filter
 */
GPtrArray *
wp_object_manager_get_objects (WpObjectManager *self, GType type_filter)
{
  GPtrArray *result = g_ptr_array_new_with_free_func (g_object_unref);
  guint i;

  for (i = 0; i < self->objects->len; i++) {
    gpointer obj = g_ptr_array_index (self->objects, i);
    if (type_filter == 0 || g_type_is_a (G_OBJECT_TYPE (obj), type_filter)) {
      g_ptr_array_add (result, g_object_ref (obj));
    }
  }

  return result;
}

static gboolean
check_constraints (GVariant *constraints,
    WpProperties *global_props,
    GObject *object)
{
  GVariantIter iter;
  GVariant *c;
  WpObjectManagerConstraintType ctype;
  g_autoptr (WpProperties) props = NULL;
  const gchar *prop_name, *prop_value;

  /* pipewire properties are contained in a GObj property called "properties" */
  if (object &&
      g_object_class_find_property (G_OBJECT_GET_CLASS (object), "properties"))
    g_object_get (object, "properties", &props, NULL);

  g_variant_iter_init (&iter, constraints);
  while (g_variant_iter_next (&iter, "@a{sv}", &c)) {
    GVariantDict dict = G_VARIANT_DICT_INIT (c);

    if (!g_variant_dict_lookup (&dict, "type", "i", &ctype)) {
      g_warning ("Invalid object manager constraint without a type");
      goto error;
    }

    switch (ctype) {
    case WP_OBJECT_MANAGER_CONSTRAINT_PW_GLOBAL_PROPERTY:
      if (!global_props)
        goto next;

      if (!g_variant_dict_lookup (&dict, "name", "&s", &prop_name)) {
        g_warning ("property constraint is without a property name");
        goto error;
      }
      if (!g_variant_dict_lookup (&dict, "value", "&s", &prop_value)) {
        g_warning ("property constraint is without a property value");
        goto error;
      }
      if (!g_strcmp0 (wp_properties_get (global_props, prop_name), prop_value))
        goto match;

      break;
    case WP_OBJECT_MANAGER_CONSTRAINT_PW_PROPERTY:
      if (!props)
        goto next;

      if (!g_variant_dict_lookup (&dict, "name", "&s", &prop_name)) {
        g_warning ("property constraint is without a property name");
        goto error;
      }
      if (!g_variant_dict_lookup (&dict, "value", "&s", &prop_value)) {
        g_warning ("property constraint is without a property value");
        goto error;
      }
      if (!g_strcmp0 (wp_properties_get (props, prop_name), prop_value))
        goto match;

      break;
    case WP_OBJECT_MANAGER_CONSTRAINT_G_PROPERTY:
      if (!object)
        goto next;

      if (!g_variant_dict_lookup (&dict, "name", "&s", &prop_name)) {
        g_warning ("property constraint is without a property name");
        goto error;
      }
      if (!g_variant_dict_lookup (&dict, "value", "&s", &prop_value)) {
        g_warning ("property constraint is without a property value");
        goto error;
      }

      if (!g_object_class_find_property (G_OBJECT_GET_CLASS (object), prop_name))
        goto next;

      if (({
        g_auto (GValue) value = G_VALUE_INIT;
        g_auto (GValue) str_value = G_VALUE_INIT;

        g_object_get_property (object, prop_name, &value);
        g_value_init (&str_value, G_TYPE_STRING);

        g_value_transform (&value, &str_value) &&
            !g_strcmp0 (g_value_get_string (&str_value), prop_value);
      }))
        goto match;

      break;
    default:
      g_warning ("Unknown constraint type '%d'", ctype);
      goto error;
    }

  next:
    {
      g_variant_dict_clear (&dict);
      g_clear_pointer (&c, g_variant_unref);
      continue;
    }
  match:
    {
      g_variant_dict_clear (&dict);
      g_clear_pointer (&c, g_variant_unref);
      return TRUE;
    }
  error:
    {
      g_autofree gchar *dbgstr = g_variant_print (c, TRUE);
      g_warning ("offending constraint was: %s", dbgstr);
      goto next;
    }
  }

  return FALSE;
}

static gboolean
wp_object_manager_is_interested_in_object (WpObjectManager * self,
    GObject * object)
{
  struct interest *i;

  pw_array_for_each (i, &self->interests) {
    if (!i->for_proxy
        && g_type_is_a (G_OBJECT_TYPE (object), i->g_type)
        && (!i->constraints ||
            check_constraints (i->constraints, NULL, object)))
    {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
wp_object_manager_is_interested_in_global (WpObjectManager * self,
    WpGlobal * global, WpProxyFeatures * wanted_features)
{
  struct interest *i;

  pw_array_for_each (i, &self->interests) {
    if (i->for_proxy
        && g_type_is_a (global->type, i->g_type)
        && (!i->constraints ||
            check_constraints (i->constraints, global->properties, NULL)))
    {
      *wanted_features = i->wanted_features;
      return TRUE;
    }
  }

  return FALSE;
}

static void
sync_emit_objects_changed (WpCore *core, GAsyncResult *res, gpointer data)
{
  g_autoptr (WpObjectManager) self = WP_OBJECT_MANAGER (data);

  g_signal_emit (self, signals[SIGNAL_OBJECTS_CHANGED], 0);
  self->pending_objchanged = FALSE;
}

static inline void
schedule_emit_objects_changed (WpObjectManager * self)
{
  if (self->pending_objchanged)
    return;

  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  if (core) {
    wp_core_sync (core, NULL, (GAsyncReadyCallback)sync_emit_objects_changed,
        g_object_ref (self));
    self->pending_objchanged = TRUE;
  }
}

static void
on_proxy_ready (GObject * proxy, GAsyncResult * res, gpointer data)
{
  g_autoptr (WpObjectManager) self = WP_OBJECT_MANAGER (data);

  g_ptr_array_add (self->objects, g_object_ref (proxy));
  g_signal_emit (self, signals[SIGNAL_OBJECT_ADDED], 0, proxy);
  schedule_emit_objects_changed (self);
}

void
wp_object_manager_add_global (WpObjectManager * self, WpGlobal * global)
{
  WpProxyFeatures features = 0;

  if (wp_object_manager_is_interested_in_global (self, global, &features)) {
    g_autoptr (WpProxy) proxy = g_weak_ref_get (&global->proxy);
    g_autoptr (WpCore) core = g_weak_ref_get (&self->core);

    if (!proxy) {
      proxy = wp_proxy_new_global (core, global);
      g_weak_ref_set (&global->proxy, proxy);
    }
    wp_proxy_augment (proxy, features, NULL, on_proxy_ready,
        g_object_ref (self));
  }
}

void
wp_object_manager_rm_global (WpObjectManager * self, guint32 id)
{
  guint i;
  for (i = 0; i < self->objects->len; i++) {
    gpointer obj = g_ptr_array_index (self->objects, i);
    if (WP_IS_PROXY (obj) && id == wp_proxy_get_bound_id (WP_PROXY (obj))) {
      g_signal_emit (self, signals[SIGNAL_OBJECT_REMOVED], 0, obj);
      g_ptr_array_remove_index_fast (self->objects, i);
      schedule_emit_objects_changed (self);
      return;
    }
  }
}

void
wp_object_manager_add_object (WpObjectManager * self, GObject * object)
{
  if (wp_object_manager_is_interested_in_object (self, object)) {
    g_ptr_array_add (self->objects, g_object_ref (object));
    g_signal_emit (self, signals[SIGNAL_OBJECT_ADDED], 0, object);
    schedule_emit_objects_changed (self);
  }
}

void
wp_object_manager_rm_object (WpObjectManager * self, GObject * object)
{
  guint index;
  if (g_ptr_array_find (self->objects, object, &index)) {
    g_signal_emit (self, signals[SIGNAL_OBJECT_REMOVED], 0, object);
    g_ptr_array_remove_index_fast (self->objects, index);
    schedule_emit_objects_changed (self);
  }
}
