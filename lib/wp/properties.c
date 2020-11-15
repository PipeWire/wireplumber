/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpProperties
 *
 * #WpProperties is a data structure that contains string key-value pairs,
 * which are used to send/receive/attach arbitrary properties to PipeWire
 * objects.
 *
 * This could be thought of as a hash table with strings as both keys and
 * values. However, the reason that this class exists instead of using
 * #GHashTable directly is that in reality it wraps the PipeWire native
 * `struct spa_dict` and `struct pw_properties` and therefore it can be
 * easily passed to PipeWire function calls that require a `struct spa_dict *`
 * or a `struct pw_properties *` as arguments. Or alternatively, it can easily
 * wrap a `struct spa_dict *` or a `struct pw_properties *` that was given
 * from the PipeWire API without necessarily doing an expensive copy operation.
 *
 * #WpProperties normally wraps a `struct pw_properties`, unless it was created
 * with wp_properties_new_wrap_dict(), in which case it wraps a
 * `struct spa_dict` and it is immutable (you cannot add/remove/modify any
 * key-value pair).
 *
 * In most cases, it actually owns the `struct pw_properties`
 * internally and manages its lifetime. The exception to that rule is when
 * #WpProperties is constructed with wp_properties_new_wrap(), in which case
 * the ownership of the `struct pw_properties` remains outside. This must
 * be used with care, as the `struct pw_properties` may be free'ed externally.
 *
 * #WpProperties is reference-counted with wp_properties_ref() and
 * wp_properties_unref().
 */

#define G_LOG_DOMAIN "wp-properties"

#include "properties.h"

#include <errno.h>
#include <pipewire/properties.h>

enum {
  FLAG_IS_DICT = (1<<1),
  FLAG_NO_OWNERSHIP = (1<<2),
};

struct _WpProperties
{
  grefcount ref;
  guint32 flags;
  union {
    struct pw_properties *props;
    const struct spa_dict *dict;
  };
};

G_DEFINE_BOXED_TYPE(WpProperties, wp_properties, wp_properties_ref, wp_properties_unref)

/**
 * wp_properties_new_empty:
 *
 * Creates a new empty properties set
 *
 * Returns: (transfer full): the newly constructed properties set
 */
WpProperties *
wp_properties_new_empty (void)
{
  WpProperties * self = g_slice_new0 (WpProperties);
  g_ref_count_init (&self->ref);
  self->flags = 0;
  self->props = pw_properties_new (NULL, NULL);
  return self;
}

/**
 * wp_properties_new:
 * @key: a property name
 * @...: a property value, followed by any number of further property
 *   key-value pairs, followed by %NULL
 *
 * Constructs a new properties set that contains the given properties
 *
 * Returns: (transfer full): the newly constructed properties set
 */
WpProperties *
wp_properties_new (const gchar * key, ...)
{
  WpProperties * self;
  va_list varargs;

  va_start(varargs, key);
  self = wp_properties_new_valist (key, varargs);
  va_end(varargs);

  return self;
}

/**
 * wp_properties_new_valist:
 * @key: a property name
 * @args: the variable arguments passed to wp_properties_new()
 *
 * This is the `va_list` version of wp_properties_new()
 *
 * Returns: (transfer full): the newly constructed properties set
 */
WpProperties *
wp_properties_new_valist (const gchar * key, va_list args)
{
  WpProperties * self = wp_properties_new_empty ();
  const gchar *value;

  while (key != NULL) {
    value = va_arg(args, gchar *);
    if (value && key[0])
      wp_properties_set (self, key, value);
    key = va_arg(args, gchar *);
  }

  return self;
}

/**
 * wp_properties_new_string:
 * @str: a string containing a whitespace separated list of key=value pairs
 *    (ex. "key1=value1 key2=value2")
 *
 * Constructs a new properties set that contains the properties that can
 * be parsed from the given string
 *
 * Returns: (transfer full): the newly constructed properties set
 */
WpProperties *
wp_properties_new_string (const gchar * str)
{
  WpProperties * self;

  g_return_val_if_fail (str != NULL, NULL);

  self = g_slice_new0 (WpProperties);
  g_ref_count_init (&self->ref);
  self->flags = 0;
  self->props = pw_properties_new_string (str);
  return self;
}

/**
 * wp_properties_new_wrap:
 * @props: a native `pw_properties` structure to wrap
 *
 * Constructs a new #WpProperties that wraps the given @props structure,
 * allowing reading properties on that @props structure through
 * the #WpProperties API.
 *
 * Care must be taken when using this function, since the returned #WpProperties
 * object does not own the @props structure. Therefore, if the owner decides
 * to free @props, the returned #WpProperties will crash when used. In addition,
 * the returned #WpProperties object will not try to free @props when destroyed.
 *
 * Furthermore, note that the returned #WpProperties object is immutable. That
 * means that you cannot add or modify any properties on it, unless you make
 * a copy first.
 *
 * Returns: (transfer full): the newly constructed properties set
 */
WpProperties *
wp_properties_new_wrap (const struct pw_properties * props)
{
  WpProperties * self;

  g_return_val_if_fail (props != NULL, NULL);

  self = g_slice_new0 (WpProperties);
  g_ref_count_init (&self->ref);
  self->flags = FLAG_NO_OWNERSHIP;
  self->props = (struct pw_properties *) props;
  return self;
}

/**
 * wp_properties_new_take:
 * @props: a native `pw_properties` structure to wrap
 *
 * Constructs a new #WpProperties that wraps the given @props structure,
 * allowing reading & writing properties on that @props structure through
 * the #WpProperties API.
 *
 * In constrast with wp_properties_new_wrap(), this function assumes ownership
 * of the @props structure, so it will try to free @props when it is destroyed.
 *
 * Returns: (transfer full): the newly constructed properties set
 */
WpProperties *
wp_properties_new_take (struct pw_properties * props)
{
  WpProperties * self;

  g_return_val_if_fail (props != NULL, NULL);

  self = g_slice_new0 (WpProperties);
  g_ref_count_init (&self->ref);
  self->flags = 0;
  self->props = props;
  return self;
}

/**
 * wp_properties_new_copy:
 * @props: a native `pw_properties` structure to copy
 *
 * Constructs a new #WpProperties that contains a copy of all the properties
 * contained in the given @props structure.
 *
 * Returns: (transfer full): the newly constructed properties set
 */
WpProperties *
wp_properties_new_copy (const struct pw_properties * props)
{
  WpProperties * self;

  g_return_val_if_fail (props != NULL, NULL);

  self = g_slice_new0 (WpProperties);
  g_ref_count_init (&self->ref);
  self->flags = 0;
  self->props = pw_properties_copy (props);
  return self;
}

/**
 * wp_properties_new_wrap_dict:
 * @dict: a native `spa_dict` structure to wrap
 *
 * Constructs a new #WpProperties that wraps the given @dict structure,
 * allowing reading properties from that @dict through the #WpProperties API.
 *
 * Note that the returned object does not own the @dict, so care must be taken
 * not to free it externally while this #WpProperties object is alive.
 *
 * In addition, note that the returned #WpProperties object is immutable. That
 * means that you cannot add or modify any properties on it, since there is
 * no defined method for modifying a `struct spa_dict`. If you need to change
 * this properties set later, you should make a copy with wp_properties_copy().
 *
 * Returns: (transfer full): the newly constructed properties set
 */
WpProperties *
wp_properties_new_wrap_dict (const struct spa_dict * dict)
{
  WpProperties * self;

  g_return_val_if_fail (dict != NULL, NULL);

  self = g_slice_new0 (WpProperties);
  g_ref_count_init (&self->ref);
  self->flags = FLAG_NO_OWNERSHIP | FLAG_IS_DICT;
  self->dict = dict;
  return self;
}

/**
 * wp_properties_new_copy_dict:
 * @dict: a native `spa_dict` structure to copy
 *
 * Constructs a new #WpProperties that contains a copy of all the properties
 * contained in the given @dict structure.
 *
 * Returns: (transfer full): the newly constructed properties set
 */
WpProperties *
wp_properties_new_copy_dict (const struct spa_dict * dict)
{
  WpProperties * self;

  g_return_val_if_fail (dict != NULL, NULL);

  self = g_slice_new0 (WpProperties);
  g_ref_count_init (&self->ref);
  self->flags = 0;
  self->props = pw_properties_new_dict (dict);
  return self;
}

/**
 * wp_properties_copy:
 * @other: a properties object
 *
 * Constructs and returns a new #WpProperties object that contains a copy
 * of all the properties contained in @other.
 *
 * Returns: (transfer full): the newly constructed properties set
 */
WpProperties *
wp_properties_copy (WpProperties * other)
{
  return wp_properties_new_copy_dict (wp_properties_peek_dict (other));
}

static void
wp_properties_free (WpProperties * self)
{
  if (!(self->flags & FLAG_NO_OWNERSHIP))
    pw_properties_free (self->props);
  g_slice_free (WpProperties, self);
}

/**
 * wp_properties_ref:
 * @self: a properties object
 *
 * Returns: (transfer full): @self with an additional reference count on it
 */
WpProperties *
wp_properties_ref (WpProperties * self)
{
  g_ref_count_inc (&self->ref);
  return self;
}

/**
 * wp_properties_unref:
 * @self: (transfer full): a properties object
 *
 * Decreases the reference count on @self and frees it when the ref count
 * reaches zero.
 */
void
wp_properties_unref (WpProperties * self)
{
  if (g_ref_count_dec (&self->ref))
    wp_properties_free (self);
}

/**
 * wp_properties_ensure_unique_owner:
 * @self: (transfer full): a properties object
 *
 * Ensures that the given properties set is uniquely owned, which means:
 *  - its reference count is 1
 *  - it is not wrapping a native `spa_dict` or `pw_properties` object
 *
 * If @self is not uniquely owned already, then it is unrefed and a copy of
 * it is returned instead. You should always consider @self as unsafe to use
 * after this call and you should use the returned object instead.
 *
 * Returns: (transfer full): the uniquely owned properties object
 */
WpProperties *
wp_properties_ensure_unique_owner (WpProperties * self)
{
  if (!g_ref_count_compare (&self->ref, 1) ||
      self->flags & (FLAG_IS_DICT | FLAG_NO_OWNERSHIP))
  {
    WpProperties *copy = wp_properties_copy (self);
    wp_properties_unref (self);
    return copy;
  }
  return self;
}

/**
 * wp_properties_update:
 * @self: a properties object
 * @props: a properties set that contains properties to update
 *
 * Updates (adds new or modifies existing) properties in @self, using the
 * given @props as a source. Any properties that are not contained in @props
 * are left untouched.
 *
 * Returns: the number of properties that were changed
 */
gint
wp_properties_update (WpProperties * self, WpProperties * props)
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  return pw_properties_update (self->props, wp_properties_peek_dict (props));
}

/**
 * wp_properties_update_from_dict:
 * @self: a properties object
 * @dict: a `spa_dict` that contains properties to update
 *
 * Updates (adds new or modifies existing) properties in @self, using the
 * given @dict as a source. Any properties that are not contained in @dict
 * are left untouched.
 *
 * Returns: the number of properties that were changed
 */
gint
wp_properties_update_from_dict (WpProperties * self,
    const struct spa_dict * dict)
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  return pw_properties_update (self->props, dict);
}

/**
 * wp_properties_add:
 * @self: a properties object
 * @props: a properties set that contains properties to add
 *
 * Adds new properties in @self, using the given @props as a source.
 * Properties (keys) from @props that are already contained in @self
 * are not modified, unlike what happens with wp_properties_update().
 * Properties in @self that are not contained in @props are left untouched.
 *
 * Returns: the number of properties that were changed
 */
gint
wp_properties_add (WpProperties * self, WpProperties * props)
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  return pw_properties_add (self->props, wp_properties_peek_dict (props));
}

/**
 * wp_properties_add_from_dict:
 * @self: a properties object
 * @dict: a `spa_dict` that contains properties to add
 *
 * Adds new properties in @self, using the given @dict as a source.
 * Properties (keys) from @dict that are already contained in @self
 * are not modified, unlike what happens with wp_properties_update_from_dict().
 * Properties in @self that are not contained in @dict are left untouched.
 *
 * Returns: the number of properties that were changed
 */
gint
wp_properties_add_from_dict (WpProperties * self,
    const struct spa_dict * dict)
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  return pw_properties_add (self->props, dict);
}

/**
 * wp_properties_update_keys:
 * @self: a properties set
 * @props: a properties set that contains properties to update
 * @key1: a property to update
 * @...: a list of additional properties to update, followed by %NULL
 *
 * Updates (adds new or modifies existing) properties in @self, using the
 * given @props as a source.
 * Unlike wp_properties_update(), this function only updates properties that
 * have one of the specified keys; the rest is left untouched.
 *
 * Returns: the number of properties that were changed
 */
gint
wp_properties_update_keys (WpProperties * self, WpProperties * props,
    const gchar * key1, ...)
{
  gint changed = 0;
  const gchar *value;

  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  va_list args;
  va_start (args, key1);
  for (; key1; key1 = va_arg (args, const gchar *)) {
    if ((value = wp_properties_get (props, key1)) != NULL)
      changed += wp_properties_set (self, key1, value);
  }
  return changed;
}

/**
 * wp_properties_update_keys_from_dict:
 * @self: a properties set
 * @dict: a `spa_dict` that contains properties to update
 * @key1: a property to update
 * @...: a list of additional properties to update, followed by %NULL
 *
 * Updates (adds new or modifies existing) properties in @self, using the
 * given @dict as a source.
 * Unlike wp_properties_update_from_dict(), this function only updates
 * properties that have one of the specified keys; the rest is left untouched.
 *
 * Returns: the number of properties that were changed
 */
gint
wp_properties_update_keys_from_dict (WpProperties * self,
    const struct spa_dict * dict, const gchar * key1, ...)
{
  gint changed = 0;
  const gchar *value;

  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  va_list args;
  va_start (args, key1);
  for (; key1; key1 = va_arg (args, const gchar *)) {
    if ((value = spa_dict_lookup (dict, key1)) != NULL)
      changed += wp_properties_set (self, key1, value);
  }
  return changed;
}

/**
 * wp_properties_update_keys_array:
 * @self: a properties set
 * @props: a properties set that contains properties to update
 * @keys: (array zero-terminated=1): the properties to update
 *
 * The same as wp_properties_update_keys(), using a NULL-terminated array
 * for specifying the keys to update
 *
 * Returns: the number of properties that were changed
 */
gint
wp_properties_update_keys_array (WpProperties * self, WpProperties * props,
    const gchar * keys[])
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  return pw_properties_update_keys (self->props,
      wp_properties_peek_dict (props), keys);
}

/**
 * wp_properties_add_keys:
 * @self: a properties set
 * @props: a properties set that contains properties to add
 * @key1: a property to add
 * @...: a list of additional properties to add, followed by %NULL
 *
 * Adds new properties in @self, using the given @props as a source.
 * Unlike wp_properties_add(), this function only adds properties that
 * have one of the specified keys; the rest is left untouched.
 *
 * Returns: the number of properties that were changed
 */
gint
wp_properties_add_keys (WpProperties * self, WpProperties * props,
    const gchar * key1, ...)
{
  gint changed = 0;
  const gchar *value;

  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  va_list args;
  va_start (args, key1);
  for (; key1; key1 = va_arg (args, const gchar *)) {
    if ((value = wp_properties_get (props, key1)) == NULL)
      continue;
    if (wp_properties_get (self, key1) == NULL)
      changed += wp_properties_set (self, key1, value);
  }
  return changed;
}

/**
 * wp_properties_add_keys_from_dict:
 * @self: a properties set
 * @dict: a `spa_dict` that contains properties to add
 * @key1: a property to add
 * @...: a list of additional properties to add, followed by %NULL
 *
 * Adds new properties in @self, using the given @dict as a source.
 * Unlike wp_properties_add_from_dict(), this function only adds
 * properties that have one of the specified keys; the rest is left untouched.
 *
 * Returns: the number of properties that were changed
 */
gint
wp_properties_add_keys_from_dict (WpProperties * self,
    const struct spa_dict * dict, const gchar * key1, ...)
{
  gint changed = 0;
  const gchar *value;

  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  va_list args;
  va_start (args, key1);
  for (; key1; key1 = va_arg (args, const gchar *)) {
    if ((value = spa_dict_lookup (dict, key1)) == NULL)
      continue;
    if (wp_properties_get (self, key1) == NULL)
      changed += wp_properties_set (self, key1, value);
  }
  return changed;
}

/**
 * wp_properties_add_keys_array:
 * @self: a properties set
 * @props: a properties set that contains properties to add
 * @keys: (array zero-terminated=1): the properties to add
 *
 * The same as wp_properties_add_keys(), using a NULL-terminated array
 * for specifying the keys to add
 *
 * Returns: the number of properties that were changed
 */
gint
wp_properties_add_keys_array (WpProperties * self, WpProperties * props,
    const gchar * keys[])
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  return pw_properties_add_keys (self->props,
      wp_properties_peek_dict (props), keys);
}

/**
 * wp_properties_get:
 * @self: a properties object
 * @key: a property key
 *
 * Returns: (transfer none) (nullable): the value of the property identified
 *   with @key, or %NULL if this property is not contained in @self
 */
const gchar *
wp_properties_get (WpProperties * self, const gchar * key)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (key != NULL, NULL);

  return spa_dict_lookup (wp_properties_peek_dict (self), key);
}

/**
 * wp_properties_set:
 * @self: a properties object
 * @key: a property key
 * @value: (nullable): a property value
 *
 * Sets the given property @key - @value pair on @self. If the property
 * already existed, the value is overwritten with the new one.
 *
 * If the @value is %NULL, then the specified property is removed from @self
 *
 * Returns: %1 if the property was changed. %0 if nothing was changed because
 *   the property already existed with the same value or because the key to
 *   remove did not exist.
 */
gint
wp_properties_set (WpProperties * self, const gchar * key,
    const gchar * value)
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  return pw_properties_set (self->props, key, value);
}

/**
 * wp_properties_setf:
 * @self: a properties object
 * @key: a property key
 * @format: a printf-style format to be formatted and set as a value for
 *   this property @key
 * @...: arguments for @format
 *
 * Formats the given @format string with the specified arguments and sets the
 * result as a value of the property specified with @key
 *
 * Returns: %1 if the property was changed. %0 if nothing was changed because
 *   the property already existed with the same value
 */
gint
wp_properties_setf (WpProperties * self, const gchar * key,
    const gchar * format, ...)
{
  gint res;
  va_list varargs;

  va_start (varargs, format);
  res = wp_properties_setf_valist (self, key, format, varargs);
  va_end (varargs);

  return res;
}

/**
 * wp_properties_setf_valist:
 * @self: a properties object
 * @key: a property key
 * @format: a printf-style format to be formatted and set as a value for
 *   this property @key
 * @args: the variable arguments passed to wp_properties_setf()
 *
 * This is the `va_list` version of wp_properties_setf()
 *
 * Returns: %1 if the property was changed. %0 if nothing was changed because
 *   the property already existed with the same value
 */
gint
wp_properties_setf_valist (WpProperties * self, const gchar * key,
    const gchar * format, va_list args)
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  return pw_properties_setva (self->props, key, format, args);
}

struct dict_iterator_data
{
  WpProperties *properties;
  const struct spa_dict_item *item;
};

static void
dict_iterator_reset (WpIterator *it)
{
  struct dict_iterator_data *it_data = wp_iterator_get_user_data (it);
  it_data->item = wp_properties_peek_dict (it_data->properties)->items;
}

static gboolean
dict_iterator_next (WpIterator *it, GValue *item)
{
  struct dict_iterator_data *it_data = wp_iterator_get_user_data (it);
  const struct spa_dict *dict = wp_properties_peek_dict (it_data->properties);

  if ((it_data->item - dict->items) < dict->n_items) {
    g_value_init (item, G_TYPE_POINTER);
    g_value_set_pointer (item, (gpointer) it_data->item);
    it_data->item++;
    return TRUE;
  }
  return FALSE;
}

static gboolean
dict_iterator_fold (WpIterator *it, WpIteratorFoldFunc func, GValue *ret,
    gpointer data)
{
  struct dict_iterator_data *it_data = wp_iterator_get_user_data (it);
  const struct spa_dict *dict = wp_properties_peek_dict (it_data->properties);
  const struct spa_dict_item *i;

  spa_dict_for_each (i, dict) {
    g_auto (GValue) item = G_VALUE_INIT;
    g_value_init (&item, G_TYPE_POINTER);
    g_value_set_pointer (&item, (gpointer) i);
    if (!func (&item, ret, data))
      return FALSE;
  }
  return TRUE;
}

static void
dict_iterator_finalize (WpIterator *it)
{
  struct dict_iterator_data *it_data = wp_iterator_get_user_data (it);
  wp_properties_unref (it_data->properties);
}

static const WpIteratorMethods dict_iterator_methods = {
  .reset = dict_iterator_reset,
  .next = dict_iterator_next,
  .fold = dict_iterator_fold,
  .finalize = dict_iterator_finalize,
};

/**
 * wp_properties_iterate:
 * @self: a properties object
 *
 * Returns: (transfer full): an iterator that iterates over the properties.
 *   Use wp_properties_iterator_item_get_key() and
 *   wp_properties_iterator_item_get_value() to parse the items returned by
 *   this iterator.
 */
WpIterator *
wp_properties_iterate (WpProperties * self)
{
  g_autoptr (WpIterator) it = NULL;
  struct dict_iterator_data *it_data;

  g_return_val_if_fail (self != NULL, NULL);

  it = wp_iterator_new (&dict_iterator_methods,
      sizeof (struct dict_iterator_data));
  it_data = wp_iterator_get_user_data (it);
  it_data->properties = wp_properties_ref (self);
  it_data->item = wp_properties_peek_dict (it_data->properties)->items;
  return g_steal_pointer (&it);
}

/**
 * wp_properties_iterator_item_get_key:
 * @item: a #GValue that was returned from the #WpIterator of
 *   wp_properties_iterate()
 *
 * Returns: (transfer none): the property key of the @item
 */
const gchar *
wp_properties_iterator_item_get_key (const GValue * item)
{
  const struct spa_dict_item *dict_item = g_value_get_pointer (item);
  g_return_val_if_fail (dict_item != NULL, NULL);
  return dict_item->key;
}

/**
 * wp_properties_iterator_item_get_value:
 * @item: a #GValue that was returned from the #WpIterator of
 *   wp_properties_iterate()
 *
 * Returns: (transfer none): the property value of the @item
 */
const gchar *
wp_properties_iterator_item_get_value (const GValue * item)
{
  const struct spa_dict_item *dict_item = g_value_get_pointer (item);
  g_return_val_if_fail (dict_item != NULL, NULL);
  return dict_item->value;
}

/**
 * wp_properties_peek_dict:
 * @self: a properties object
 *
 * Returns: (transfer none): the internal properties set as a `struct spa_dict *`
 */
const struct spa_dict *
wp_properties_peek_dict (WpProperties * self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return (self->flags & FLAG_IS_DICT) ? self->dict : &self->props->dict;
}

/**
 * wp_properties_to_pw_properties:
 * @self: a properties object
 *
 * Returns: (transfer full): a copy of the properties in @self
 *   as a `struct pw_properties`
 */
struct pw_properties *
wp_properties_to_pw_properties (WpProperties * self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return pw_properties_new_dict (wp_properties_peek_dict (self));
}

/**
 * wp_properties_unref_and_take_pw_properties:
 * @self: (transfer full): a properties object
 *
 * Similar to wp_properties_to_pw_properties(), but this method avoids making
 * a copy of the properties by returning the `struct pw_properties` that is
 * stored internally and then freeing the #WpProperties wrapper.
 *
 * If @self is not uniquely owned (see wp_properties_ensure_unique_owner()),
 * then this method does make a copy and is the same as
 * wp_properties_to_pw_properties(), performance-wise.
 *
 * Returns: (transfer full): the properties in @self as a `struct pw_properties`
 */
struct pw_properties *
wp_properties_unref_and_take_pw_properties (WpProperties * self)
{
  g_return_val_if_fail (self != NULL, NULL);

  g_autoptr (WpProperties) unique = wp_properties_ensure_unique_owner (self);
  /* set the flag so that unref-ing @unique will not destroy unique->props */
  unique->flags = FLAG_NO_OWNERSHIP;
  return unique->props;
}

/**
 * wp_properties_matches:
 * @self: a properties object
 * @other: a set of properties to match
 *
 * Checks if all property values contained in @other are matching with the
 * values in @self.
 *
 * If a property is contained in @other and not in @self, the result is not
 * matched. If a property is contained in both sets, then the value of the
 * property in @other is interpreted as a glob-style pattern
 * (using g_pattern_match_simple()) and the value in @self is checked to
 * see if it matches with this pattern.
 *
 * Returns: %TRUE if all matches were successfull, %FALSE if at least one
 *   property value did not match
 */
gboolean
wp_properties_matches (WpProperties * self, WpProperties *other)
{
  const struct spa_dict * dict;
  const struct spa_dict_item *item;
  const gchar *value;

  g_return_val_if_fail (self != NULL, FALSE);

  /* Check if the property values match the ones from 'self' */
  dict = wp_properties_peek_dict (other);
  spa_dict_for_each(item, dict) {
    value = wp_properties_get (self, item->key);
    if (!value || !g_pattern_match_simple (value, item->value))
      return FALSE;
  }

  return TRUE;
}
