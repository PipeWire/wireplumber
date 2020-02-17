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

#include "properties.h"

#include <errno.h>
#include <pipewire/properties.h>

enum {
  FLAG_IS_DICT = (1<<1),
  FLAG_NO_OWNERSHIP = (1<<2),
};

struct _WpProperties
{
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
  WpProperties * self = g_rc_box_new (WpProperties);
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

  self = g_rc_box_new (WpProperties);
  self->flags = 0;
  self->props = pw_properties_new_string (str);
  return self;
}

/**
 * wp_properties_new_wrap:
 * @props: a native `pw_properties` structure to wrap
 *
 * Constructs a new #WpProperties that wraps the given @props structure,
 * allowing reading & writing properties on that @props structure through
 * the #WpProperties API.
 *
 * Care must be taken when using this function, since the returned #WpProperties
 * object does not own the @props structure. Therefore, if the owner decides
 * to free @props, the returned #WpProperties will crash when used. In addition,
 * the returned #WpProperties object will not try to free @props when destroyed.
 *
 * Returns: (transfer full): the newly constructed properties set
 */
WpProperties *
wp_properties_new_wrap (struct pw_properties * props)
{
  WpProperties * self;

  g_return_val_if_fail (props != NULL, NULL);

  self = g_rc_box_new (WpProperties);
  self->flags = FLAG_NO_OWNERSHIP;
  self->props = props;
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

  self = g_rc_box_new (WpProperties);
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

  self = g_rc_box_new (WpProperties);
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

  self = g_rc_box_new (WpProperties);
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

  self = g_rc_box_new (WpProperties);
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
  return g_rc_box_acquire (self);
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
  g_rc_box_release_full (self, (GDestroyNotify) wp_properties_free);
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

  return pw_properties_update (self->props, dict);
}

/**
 * wp_properties_copy_keys:
 * @src: the source properties set
 * @dst: the destination properties set
 * @key1: a property to copy
 * @...: a list of additional properties to copy, followed by %NULL
 *
 * Copies the specified properties from @src to @dst.
 *
 * Returns: the number of properties that were changed
 */
gint
wp_properties_copy_keys (WpProperties * src, WpProperties * dst,
    const gchar *key1, ...)
{
  gint ret;
  va_list args;
  va_start (args, key1);
  ret = wp_properties_copy_keys_valist (src, dst, key1, args);
  va_end (args);
  return ret;
}

/**
 * wp_properties_copy_keys_valist:
 * @src: the source properties set
 * @dst: the destination properties set
 * @key1: a property to copy
 * @args: the variable arguments passed to wp_properties_copy_keys()
 *
 * This is the `va_list` version of wp_properties_copy_keys()
 *
 * Returns: the number of properties that were changed
 */
gint
wp_properties_copy_keys_valist (WpProperties * src, WpProperties * dst,
    const gchar *key1, va_list args)
{
  gint changed = 0;
  const gchar *value;

  for (; key1; key1 = va_arg (args, const gchar *)) {
    if ((value = wp_properties_get (src, key1)) != NULL)
      changed += wp_properties_set (dst, key1, value);
  }
  return changed;
}


/**
 * wp_properties_copy_all:
 * @src: the source properties set
 * @dst: the destination properties set
 *
 * Copies all the properties contained in @src into @dst
 */
void
wp_properties_copy_all (WpProperties * src, WpProperties * dst)
{
  const struct spa_dict * dict;
  const struct spa_dict_item *item;

  g_return_if_fail (src != NULL);
  g_return_if_fail (dst != NULL);

  dict = wp_properties_peek_dict (src);
  spa_dict_for_each(item, dict) {
    wp_properties_set (dst, item->key, item->value);
  }
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

  return pw_properties_setva (self->props, key, format, args);
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
 * wp_properties_matches:
 * @self: a properties object
 * @other: a set of properties to match
 *
 * Checks if the property values contained in @other are matching with the
 * values in @self.
 *
 * If a property is contained in one set and not the other, the result is not
 * affected. If a property is contained in both sets, then the value of the
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

  /* Check if the property values match the ones from 'other' */
  dict = wp_properties_peek_dict (self);
  spa_dict_for_each(item, dict) {
    value = wp_properties_get (other, item->key);
    if (value && !g_pattern_match_simple (value, item->value))
      return FALSE;
  }

  return TRUE;
}
