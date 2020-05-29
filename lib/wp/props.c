/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: props
 * @title: Dynamic Proxy Properties
 */

#define G_LOG_DOMAIN "wp-props"

#include "props.h"
#include "debug.h"
#include "spa-type.h"
#include "wpenums.h"
#include <spa/param/param.h>

struct entry
{
  guint32 id;
  gchar *description;
  WpSpaPod *type;
  WpSpaPod *value;
};

struct entry *
entry_new (void)
{
  struct entry *e = g_slice_new0 (struct entry);
  return e;
}

static void
entry_free (struct entry *e)
{
  g_free (e->description);
  g_clear_pointer (&e->type, wp_spa_pod_unref);
  g_clear_pointer (&e->value, wp_spa_pod_unref);
  g_slice_free (struct entry, e);
}

struct _WpProps
{
  GObject parent;

  GWeakRef proxy;
  WpPropsMode mode;
  GList *entries;
};

enum {
  PROP_0,
  PROP_PROXY,
  PROP_MODE,
};

enum
{
  SIGNAL_PROP_CHANGED,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

/**
 * WpProps:
 *
 * #WpProps handles dynamic properties on PipeWire objects, which are
 * known in PipeWire as "params" of type `SPA_PARAM_Props`.
 *
 * #WpProps has two modes of operation:
 *  - %WP_PROPS_MODE_CACHE: In this mode, this object caches properties that are
 *    actually stored and discovered from the associated proxy object.
 *    When setting a property, the property is first set on the proxy and the
 *    cache is updated asynchronously (so wp_props_get() will not immediately
 *    return the value that was set with wp_props_set()).
 *  - %WP_PROPS_MODE_STORE: In this mode, this object is the actual store of
 *    properties. This is used by object implementations, such as #WpImplSession.
 *    Before storing anything, properties need to be registered with
 *    wp_props_register().
 */
G_DEFINE_TYPE (WpProps, wp_props, G_TYPE_OBJECT)

static void
wp_props_init (WpProps * self)
{
  g_weak_ref_init (&self->proxy, NULL);
}

static void
wp_props_finalize (GObject * object)
{
  WpProps * self = WP_PROPS (object);

  g_list_free_full (self->entries, (GDestroyNotify) entry_free);
  g_weak_ref_clear (&self->proxy);

  G_OBJECT_CLASS (wp_props_parent_class)->finalize (object);
}

static void
wp_props_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpProps *self = WP_PROPS (object);

  switch (property_id) {
  case PROP_PROXY:
    g_weak_ref_set (&self->proxy, g_value_get_object (value));
    break;
  case PROP_MODE:
    self->mode = g_value_get_enum (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_props_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpProps *self = WP_PROPS (object);

  switch (property_id) {
  case PROP_PROXY:
    g_value_take_object (value, g_weak_ref_get (&self->proxy));
    break;
  case PROP_MODE:
    g_value_set_enum (value, self->mode);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_props_class_init (WpPropsClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;

  object_class->finalize = wp_props_finalize;
  object_class->set_property = wp_props_set_property;
  object_class->get_property = wp_props_get_property;

  g_object_class_install_property (object_class, PROP_PROXY,
      g_param_spec_object ("proxy", "proxy", "The proxy", WP_TYPE_PROXY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_MODE,
      g_param_spec_enum ("mode", "mode", "The mode",
          WP_TYPE_PROPS_MODE, WP_PROPS_MODE_CACHE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  /**
   * WpProps::prop-changed:
   * @self: the props
   * @name: the name of the property that changed
   */
  signals[SIGNAL_PROP_CHANGED] = g_signal_new (
      "prop-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static struct entry *
find_entry (WpProps * self, const gchar * name)
{
  GList *l = self->entries;
  guint32 id;

  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_PROPS, name, &id,
          NULL, NULL)) {
    wp_critical_object (self, "prop id name '%s' is not registered", name);
    return NULL;
  }

  while (l && ((struct entry *) l->data)->id != id)
    l = g_list_next (l);
  if (!l)
    return NULL;

  return (struct entry *) l->data;
}

/* public */

/**
 * wp_props_new:
 * @mode: the mode
 * @proxy: (transfer none) (nullable): the associated proxy; can be %NULL
 *   if @mode is %WP_PROPS_MODE_STORE
 *
 * Returns: (transfer full): the newly created #WpProps object
 */
WpProps *
wp_props_new (WpPropsMode mode, WpProxy * proxy)
{
  return g_object_new (WP_TYPE_PROPS, "mode", mode, "proxy", proxy, NULL);
}

/**
 * wp_props_register:
 * @self: the props
 * @name: the name (registered spa type nick) of the property
 * @description: the description of the property
 * @pod: (transfer full): a pod that gives the type and the default value
 *
 * Registers a new property. This can only be used in %WP_PROPS_MODE_STORE mode.
 *
 * @name must be a valid spa type nickname, registered in the
 * %WP_SPA_TYPE_TABLE_PROPS table.
 *
 * @pod can be a value (which is taken as the default value) or a choice
 * (which defines the allowed values for this property)
 */
void
wp_props_register (WpProps * self, const gchar * name,
    const gchar * description, WpSpaPod * pod)
{
  guint32 id;

  g_return_if_fail (self->mode == WP_PROPS_MODE_STORE);

  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_PROPS, name, &id,
          NULL, NULL)) {
    wp_critical_object (self, "prop id name '%s' is not registered", name);
    return;
  }

  struct entry *e = entry_new ();
  e->id = id;
  e->description = g_strdup (description);
  e->type = pod;
  e->value = wp_spa_pod_is_choice (e->type) ?
      wp_spa_pod_get_choice_child (e->type) : wp_spa_pod_ref (e->type);
  self->entries = g_list_append (self->entries, e);
}

/**
 * wp_props_register_from_info:
 * @self: the props
 * @pod: (transfer full): a `SPA_TYPE_OBJECT_PropInfo` pod
 *
 * Registers a new property using the information of the provided PropInfo @pod
 */
void
wp_props_register_from_info (WpProps * self, WpSpaPod * pod)
{
  g_autoptr (WpSpaPod) prop_info = pod;
  guint32 id;
  const gchar *description;
  g_autoptr (WpSpaPod) type = NULL;

  if (!wp_spa_pod_get_object (prop_info,
        "PropInfo", NULL,
        "id", "I", &id,
        "name", "s", &description,
        "type", "P", &type,
        NULL)) {
    wp_warning_boxed (WP_TYPE_SPA_POD, prop_info, "bad prop info object");
    return;
  }

  struct entry *e = entry_new ();
  e->id = id;
  e->description = g_strdup (description);
  e->type = wp_spa_pod_ref (type);
  e->value = wp_spa_pod_is_choice (e->type) ?
      wp_spa_pod_get_choice_child (e->type) : wp_spa_pod_ref (e->type);
  self->entries = g_list_append (self->entries, e);
}

/**
 * wp_props_iterate_prop_info:
 * @self: the props
 *
 * Returns: (transfer full): a #WpIterator that iterates over #WpSpaPod items
 *   where each pod is an object of type `SPA_TYPE_OBJECT_PropInfo`, and thus
 *   contains the id, the description and the type of each property.
 */
WpIterator *
wp_props_iterate_prop_info (WpProps * self)
{
  g_autoptr (GPtrArray) res =
      g_ptr_array_new_with_free_func ((GDestroyNotify) wp_spa_pod_unref);

  g_return_val_if_fail (WP_IS_PROPS (self), NULL);

  for (GList *l = self->entries; l != NULL; l = g_list_next (l)) {
    struct entry * e = (struct entry *) l->data;
    g_ptr_array_add (res, wp_spa_pod_new_object (
        "PropInfo", "PropInfo",
        "id", "I", e->id,
        "name", "s", e->description,
        "type", "P", e->type,
        NULL));
  }

  return wp_iterator_new_ptr_array (g_steal_pointer (&res), WP_TYPE_SPA_POD);
}

/**
 * wp_props_get_all:
 * @self: the props
 *
 * Returns: (transfer full): a pod object of type `SPA_TYPE_OBJECT_Props`
 *   that contains all the properties, as they would appear on the PipeWire
 *   object
 */
WpSpaPod *
wp_props_get_all (WpProps * self)
{
  g_autoptr (WpSpaPodBuilder) b = NULL;

  g_return_val_if_fail (WP_IS_PROPS (self), NULL);

  b = wp_spa_pod_builder_new_object ("Props", "Props");
  for (GList *l = self->entries; l != NULL; l = g_list_next (l)) {
    struct entry * e = (struct entry *) l->data;
    if (e->id && e->value) {
      wp_spa_pod_builder_add_property_id (b, e->id);
      wp_spa_pod_builder_add_pod (b, e->value);
    }
  }

  return wp_spa_pod_builder_end (b);
}

/**
 * wp_props_get:
 * @self: the props
 * @name: the name (registered spa type nick) of the property to get
 *
 * Returns: (transfer full) (nullable): a pod with the current value of the
 *   property or %NULL if the property is not found
 */
WpSpaPod *
wp_props_get (WpProps * self, const gchar * name)
{
  struct entry * e;

  g_return_val_if_fail (WP_IS_PROPS (self), NULL);

  if (!(e = find_entry (self, name)))
    return NULL;
  return wp_spa_pod_ref (e->value);
}

static void
wp_props_set_on_proxy (WpProps * self, const gchar * name, WpSpaPod * pod)
{
  g_autoptr (WpSpaPod) val = pod;
  g_autoptr (WpProxy) proxy = g_weak_ref_get (&self->proxy);
  g_autoptr (WpSpaPod) param = NULL;

  g_return_if_fail (proxy != NULL);

  if (name) {
    param = wp_spa_pod_new_object (
        "Props", "Props",
        name, "P", val,
        NULL);
  } else {
    param = wp_spa_pod_ref (pod);
  }

  /* our store will be updated by the param event */
  wp_proxy_set_param (proxy, "Props", param);
}

static void
wp_props_store_single (WpProps * self, const gchar * name, WpSpaPod * pod)
{
  g_autoptr (WpSpaPod) val = pod;
  struct entry * e;

  if (!(e = find_entry (self, name))) {
    wp_warning_object (self, "prop '%s' is not registered", name);
    return;
  }

  wp_trace_object (self, "storing '%s', entry:%p", name, e);

  /* TODO check the type */

  if (!wp_spa_pod_equal (e->value, val)) {
    g_clear_pointer (&e->value, wp_spa_pod_unref);
    e->value = wp_spa_pod_ensure_unique_owner (g_steal_pointer (&val));
    g_signal_emit (self, signals[SIGNAL_PROP_CHANGED], 0, name);
  }
}

static void
wp_props_store_many (WpProps * self, WpSpaPod * pod)
{
  g_autoptr (WpSpaPod) props = pod;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  for (it = wp_spa_pod_iterate (props);
       wp_iterator_next (it, &item);
       g_value_unset (&item)) {
    WpSpaPod *p = g_value_get_boxed (&item);
    const char *name = NULL;
    WpSpaPod *val = NULL;

    if (!wp_spa_pod_get_property (p, &name, &val)) {
      wp_warning_object (self, "failed to get property name & value");
      continue;
    }
    wp_props_store (self, name, val);
  }
}

/**
 * wp_props_set:
 * @self: the props
 * @name: (nullable): the name (registered spa type nick) of the property to set
 * @value: (transfer full): the value to set
 *
 * Sets the property specified with @name to have the given @value.
 * If the mode is %WP_PROPS_MODE_CACHE, this property will be set on the
 * associated proxy first and will be updated asynchronously.
 *
 * If @name is %NULL, then @value must be an object of type
 * `SPA_TYPE_OBJECT_Props`, which may contain multiple properties to set.
 *
 * If any value actually changes, the #WpProps::prop-changed signal will be
 * emitted.
 */
void
wp_props_set (WpProps * self, const gchar * name, WpSpaPod * value)
{
  g_return_if_fail (WP_IS_PROPS (self));
  g_return_if_fail (value != NULL);

  switch (self->mode) {
    case WP_PROPS_MODE_CACHE:
      wp_props_set_on_proxy (self, name, value);
      break;
    case WP_PROPS_MODE_STORE:
      if (name)
        wp_props_store_single (self, name, value);
      else
        wp_props_store_many (self, value);
      break;
    default:
      g_return_if_reached ();
  }
}

/**
 * wp_props_store:
 * @self: the props
 * @name: (nullable): the name (registered spa type nick) of the property to set
 * @value: (transfer full): the value to set
 *
 * Stores the given @value for the property specified with @name.
 * This method always stores, even if the mode is %WP_PROPS_MODE_CACHE. This is
 * useful for caching implementations only.
 *
 * If @name is %NULL, then @value must be an object of type
 * `SPA_TYPE_OBJECT_Props`, which may contain multiple properties to set.
 *
 * If any value actually changes, the #WpProps::prop-changed signal will be
 * emitted.
 */
void
wp_props_store (WpProps * self, const gchar * name, WpSpaPod * value)
{
  g_return_if_fail (WP_IS_PROPS (self));
  g_return_if_fail (value != NULL);

  if (name)
    wp_props_store_single (self, name, value);
  else
    wp_props_store_many (self, value);
}
