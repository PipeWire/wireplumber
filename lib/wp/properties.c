/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-properties"

#include "properties.h"

#include <errno.h>
#include <pipewire/properties.h>

/*! \defgroup wpproperties WpProperties */
/*!
 * \struct WpProperties
 *
 * WpProperties is a data structure that contains string key-value pairs,
 * which are used to send/receive/attach arbitrary properties to PipeWire objects.
 *
 * This could be thought of as a hash table with strings as both keys and
 * values. However, the reason that this class exists instead of using
 * GHashTable directly is that in reality it wraps the PipeWire native
 * `struct spa_dict` and `struct pw_properties` and therefore it can be
 * easily passed to PipeWire function calls that require a `struct spa_dict *`
 * or a `struct pw_properties *` as arguments. Or alternatively, it can easily
 * wrap a `struct spa_dict *` or a `struct pw_properties *` that was given
 * from the PipeWire API without necessarily doing an expensive copy operation.
 *
 * WpProperties normally wraps a `struct pw_properties`, unless it was created
 * with wp_properties_new_wrap_dict(), in which case it wraps a
 * `struct spa_dict` and it is immutable (you cannot add/remove/modify any
 * key-value pair).
 *
 * In most cases, it actually owns the `struct pw_properties`
 * internally and manages its lifetime. The exception to that rule is when
 * WpProperties is constructed with wp_properties_new_wrap(), in which case
 * the ownership of the `struct pw_properties` remains outside. This must
 * be used with care, as the `struct pw_properties` may be free'ed externally.
 *
 * WpProperties is reference-counted with wp_properties_ref() and
 * wp_properties_unref().
 */

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

/*!
 * \brief Creates a new empty properties set
 * \ingroup wpproperties
 * \returns (transfer full): the newly constructed properties set
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

/*!
 * \brief Constructs a new properties set that contains the given properties
 *
 * \ingroup wpproperties
 * \param key a property name
 * \param ... a property value, followed by any number of further property
 *   key-value pairs, followed by NULL
 * \returns (transfer full): the newly constructed properties set
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

/*!
 * \brief This is the `va_list` version of wp_properties_new()
 *
 * \ingroup wpproperties
 * \param key a property name
 * \param args the variable arguments passed to wp_properties_new()
 * \returns (transfer full): the newly constructed properties set
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

/*!
 * \brief Constructs a new properties set that contains the properties that can
 * be parsed from the given string
 *
 * \ingroup wpproperties
 * \param str a string containing a whitespace separated list of key=value pairs
 *    (ex. "key1=value1 key2=value2")
 * \returns (transfer full): the newly constructed properties set
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

/*!
 * \brief Constructs a new WpProperties that wraps the given \a props structure,
 * allowing reading properties on that \a props structure through
 * the WpProperties API.
 *
 * Care must be taken when using this function, since the returned WpProperties
 * object does not own the \a props structure. Therefore, if the owner decides
 * to free \a props, the returned WpProperties will crash when used. In addition,
 * the returned WpProperties object will not try to free \a props when destroyed.
 *
 * Furthermore, note that the returned WpProperties object is immutable. That
 * means that you cannot add or modify any properties on it, unless you make
 * a copy first.
 *
 * \ingroup wpproperties
 * \param props a native `pw_properties` structure to wrap
 * \returns (transfer full): the newly constructed properties set
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

/*!
 * \brief Constructs a new WpProperties that wraps the given \a props structure,
 * allowing reading & writing properties on that \a props structure through
 * the WpProperties API.
 *
 * In constrast with wp_properties_new_wrap(), this function assumes ownership
 * of the \a props structure, so it will try to free \a props when it is destroyed.
 *
 * \ingroup wpproperties
 * \param props a native `pw_properties` structure to wrap
 * \returns (transfer full): the newly constructed properties set
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

/*!
 * \brief Constructs a new WpProperties that contains a copy of all the properties
 * contained in the given \a props structure.
 *
 * \ingroup wpproperties
 * \param props a native `pw_properties` structure to copy
 * \returns (transfer full): the newly constructed properties set
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

/*!
 * \brief Constructs a new WpProperties that wraps the given \a dict structure,
 * allowing reading properties from that \a dict through the WpProperties API.
 *
 * Note that the returned object does not own the \a dict, so care must be taken
 * not to free it externally while this WpProperties object is alive.
 *
 * In addition, note that the returned WpProperties object is immutable. That
 * means that you cannot add or modify any properties on it, since there is
 * no defined method for modifying a `struct spa_dict`. If you need to change
 * this properties set later, you should make a copy with wp_properties_copy().
 *
 * \ingroup wpproperties
 * \param dict a native `spa_dict` structure to wrap
 * \returns (transfer full): the newly constructed properties set
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

/*!
 * \brief Constructs a new WpProperties that contains a copy of all the
 * properties contained in the given \a dict structure.
 *
 * \ingroup wpproperties
 * \param dict a native `spa_dict` structure to copy
 * \returns (transfer full): the newly constructed properties set
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

/*!
 * \brief Constructs and returns a new WpProperties object that contains a copy
 * of all the properties contained in \a other.
 *
 * \ingroup wpproperties
 * \param other a properties object
 * \returns (transfer full): the newly constructed properties set
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

/*!
 * \ingroup wpproperties
 * \param self a properties object
 * \returns (transfer full): \a self with an additional reference count on it
 */
WpProperties *
wp_properties_ref (WpProperties * self)
{
  g_ref_count_inc (&self->ref);
  return self;
}

/*!
 * \brief Decreases the reference count on \a self and frees it when the ref
 * count reaches zero.
 *
 * \ingroup wpproperties
 * \param self (transfer full): a properties object
 */
void
wp_properties_unref (WpProperties * self)
{
  if (g_ref_count_dec (&self->ref))
    wp_properties_free (self);
}

/*!
 * \brief Ensures that the given properties set is uniquely owned.
 *
 * "Uniquely owned" means that:
 *  - its reference count is 1
 *  - it is not wrapping a native `spa_dict` or `pw_properties` object
 *
 * If \a self is not uniquely owned already, then it is unrefed and a copy of
 * it is returned instead. You should always consider \a self as unsafe to use
 * after this call and you should use the returned object instead.
 *
 * \ingroup wpproperties
 * \param self (transfer full): a properties object
 * \returns (transfer full): the uniquely owned properties object
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

/*!
 * \brief Updates (adds new or modifies existing) properties in \a self,
 * using the given \a props as a source.
 *
 * Any properties that are not contained in \a props are left untouched.
 *
 * \ingroup wpproperties
 * \param self a properties object
 * \param props a properties set that contains properties to update
 * \returns the number of properties that were changed
 */
gint
wp_properties_update (WpProperties * self, WpProperties * props)
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  return pw_properties_update (self->props, wp_properties_peek_dict (props));
}

/*!
 * \brief Updates (adds new or modifies existing) properties in \a self,
 * using the given \a dict as a source.
 *
 * Any properties that are not contained in \a dict are left untouched.
 *
 * \ingroup wpproperties
 * \param self a properties object
 * \param dict a `spa_dict` that contains properties to update
 * \returns the number of properties that were changed
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

/*!
 * \brief Adds new properties in \a self, using the given \a props as a source.
 *
 * Properties (keys) from \a props that are already contained in \a self
 * are not modified, unlike what happens with wp_properties_update().
 * Properties in \a self that are not contained in \a props are left untouched.
 *
 * \ingroup wpproperties
 * \param self a properties object
 * \param props a properties set that contains properties to add
 * \returns the number of properties that were changed
 */
gint
wp_properties_add (WpProperties * self, WpProperties * props)
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_NO_OWNERSHIP), -EINVAL);

  return pw_properties_add (self->props, wp_properties_peek_dict (props));
}

/*!
 * \brief Adds new properties in \a self, using the given \a dict as a source.
 *
 * Properties (keys) from \a dict that are already contained in \a self
 * are not modified, unlike what happens with wp_properties_update_from_dict().
 * Properties in \a self that are not contained in \a dict are left untouched.
 *
 * \ingroup wpproperties
 * \param self a properties object
 * \param dict a `spa_dict` that contains properties to add
 * \returns the number of properties that were changed
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

/*!
 * \brief Updates (adds new or modifies existing) properties in \a self,
 * using the given \a props as a source.
 *
 * Unlike wp_properties_update(), this function only updates properties that
 * have one of the specified keys; the rest is left untouched.
 *
 * \ingroup wpproperties
 * \param self a properties set
 * \param props a properties set that contains properties to update
 * \param key1 a property to update
 * \param ... a list of additional properties to update, followed by NULL
 * \returns the number of properties that were changed
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
  va_end (args);
  return changed;
}

/*!
 * \brief Updates (adds new or modifies existing) properties in \a self,
 * using the given \a dict as a source.
 *
 * Unlike wp_properties_update_from_dict(), this function only updates
 * properties that have one of the specified keys; the rest is left untouched.
 *
 * \ingroup wpproperties
 * \param self a properties set
 * \param dict a `spa_dict` that contains properties to update
 * \param key1 a property to update
 * \param ... a list of additional properties to update, followed by NULL
 * \returns the number of properties that were changed
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
  va_end (args);
  return changed;
}

/*!
 * \brief The same as wp_properties_update_keys(), using a NULL-terminated array
 * for specifying the keys to update
 *
 * \ingroup wpproperties
 * \param self a properties set
 * \param props a properties set that contains properties to update
 * \param keys (array zero-terminated=1): the properties to update
 * \returns the number of properties that were changed
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

/*!
 * \brief Adds new properties in \a self, using the given \a props as a source.
 *
 * Unlike wp_properties_add(), this function only adds properties that
 * have one of the specified keys; the rest is left untouched.
 *
 * \ingroup wpproperties
 * \param self a properties set
 * \param props a properties set that contains properties to add
 * \param key1 a property to add
 * \param ... a list of additional properties to add, followed by NULL
 * \returns the number of properties that were changed
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
  va_end (args);
  return changed;
}

/*!
 * \brief Adds new properties in \a self, using the given \a dict as a source.
 *
 * Unlike wp_properties_add_from_dict(), this function only adds
 * properties that have one of the specified keys; the rest is left untouched.
 *
 * \ingroup wpproperties
 * \param self a properties set
 * \param dict a `spa_dict` that contains properties to add
 * \param key1 a property to add
 * \param ... a list of additional properties to add, followed by NULL
 * \returns the number of properties that were changed
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
  va_end (args);
  return changed;
}

/*!
 * \brief The same as wp_properties_add_keys(), using a NULL-terminated array
 * for specifying the keys to add
 *
 * \ingroup wpproperties
 * \param self a properties set
 * \param props a properties set that contains properties to add
 * \param keys (array zero-terminated=1): the properties to add
 * \returns the number of properties that were changed
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

/*!
 * \brief Looks up a given property value from a key
 *
 * \ingroup wpproperties
 * \param self a properties object
 * \param key a property key
 * \returns (transfer none) (nullable): the value of the property identified
 *   with \a key, or NULL if this property is not contained in \a self
 */
const gchar *
wp_properties_get (WpProperties * self, const gchar * key)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (key != NULL, NULL);

  return spa_dict_lookup (wp_properties_peek_dict (self), key);
}

/*!
 * \brief Sets the given property \a key - \a value pair on \a self.
 *
 * If the property already existed, the value is overwritten with the new one.
 *
 * If the \a value is NULL, then the specified property is removed from \a self
 *
 * \ingroup wpproperties
 * \param self a properties object
 * \param key a property key
 * \param value (nullable): a property value
 * \returns 1 if the property was changed. 0 if nothing was changed because
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

/*!
 * \brief Formats the given \a format string with the specified arguments
 * and sets the result as a value of the property specified with \a key
 *
 * \ingroup wpproperties
 * \param self a properties object
 * \param key a property key
 * \param format a printf-style format to be formatted and set as a value for
 *   this property \a key
 * \param ... arguments for \a format
 * \returns 1 if the property was changed. 0 if nothing was changed because
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

/*!
 * \brief This is the `va_list` version of wp_properties_setf()
 *
 * \ingroup wpproperties
 * \param self a properties object
 * \param key a property key
 * \param format a printf-style format to be formatted and set as a value for
 *   this property \a key
 * \param args the variable arguments passed to wp_properties_setf()
 * \returns 1 if the property was changed. 0 if nothing was changed because
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

struct _WpPropertiesItem
{
  WpProperties *props;
  const struct spa_dict_item *item;
};

G_DEFINE_BOXED_TYPE (WpPropertiesItem, wp_properties_item,
    wp_properties_item_ref, wp_properties_item_unref)

static WpPropertiesItem *
wp_properties_item_new (WpProperties *props, const struct spa_dict_item *item)
{
  WpPropertiesItem *self = g_rc_box_new0 (WpPropertiesItem);
  self->props = wp_properties_ref (props);
  self->item = item;
  return self;
}

static void
wp_properties_item_free (gpointer p)
{
  WpPropertiesItem *self = p;
  wp_properties_unref (self->props);
}

/*!
 * \brief Increases the reference count of a properties item object
 * \ingroup wpproperties
 * \param self a properties item object
 * \returns (transfer full): \a self with an additional reference count on it
 * \since 0.4.2
 */
WpPropertiesItem *
wp_properties_item_ref (WpPropertiesItem *self)
{
  return g_rc_box_acquire (self);
}

/*!
 * \brief Decreases the reference count on \a self and frees it when the ref
 * count reaches zero.
 * \ingroup wpproperties
 * \param self (transfer full): a properties item object
 * \since 0.4.2
 */
void
wp_properties_item_unref (WpPropertiesItem *self)
{
  g_rc_box_release_full (self, wp_properties_item_free);
}

/*!
 * \brief Gets the key from a properties item
 *
 * \ingroup wpproperties
 * \param self the item held by the GValue that was returned from the WpIterator
 *   of wp_properties_new_iterator()
 * \returns (transfer none): the property key of the \a item
 * \since 0.4.2
 */
const gchar *
wp_properties_item_get_key (WpPropertiesItem * self)
{
  return self->item->key;
}

/*!
 * \brief Gets the value from a properties item
 *
 * \ingroup wpproperties
 * \param self the item held by the GValue that was returned from the WpIterator
 *   of wp_properties_new_iterator()
 * \returns (transfer none): the property value of the \a item
 * \since 0.4.2
 */
const gchar *
wp_properties_item_get_value (WpPropertiesItem * self)
{
  return self->item->value;
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
    g_value_init (item, WP_TYPE_PROPERTIES_ITEM);
    g_autoptr (WpPropertiesItem) pi = wp_properties_item_new (
        it_data->properties, it_data->item);
    g_value_take_boxed (item, g_steal_pointer (&pi));
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
    g_autoptr (WpPropertiesItem) pi = wp_properties_item_new (
        it_data->properties, i);
    g_value_init (&item, WP_TYPE_PROPERTIES_ITEM);
    g_value_take_boxed (&item, g_steal_pointer (&pi));
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
  .version = WP_ITERATOR_METHODS_VERSION,
  .reset = dict_iterator_reset,
  .next = dict_iterator_next,
  .fold = dict_iterator_fold,
  .finalize = dict_iterator_finalize,
};

/*!
 * \brief Iterates through all the properties in the properties object
 *
 * \ingroup wpproperties
 * \param self a properties object
 * \returns (transfer full): an iterator that iterates over the properties.
 *   The items in the iterator are of type WpPropertiesItem.
 *   Use wp_properties_item_get_key() and
 *   wp_properties_item_get_value() to retrieve their contents.
 */
WpIterator *
wp_properties_new_iterator (WpProperties * self)
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

/*!
 * \brief Gets the key from a properties iterator item
 *
 * \ingroup wpproperties
 * \param item a GValue that was returned from the WpIterator of
 *   wp_properties_new_iterator()
 * \returns (transfer none): the property key of the \a item
 * \deprecated Use wp_properties_item_get_key() instead
 */
const gchar *
wp_properties_iterator_item_get_key (const GValue * item)
{
  WpPropertiesItem *pi = g_value_get_boxed (item);
  g_return_val_if_fail (pi != NULL, NULL);
  return wp_properties_item_get_key (pi);
}

/*!
 * \brief Gets the value from a properties iterator item
 *
 * \ingroup wpproperties
 * \param item a GValue that was returned from the WpIterator of
 *   wp_properties_new_iterator()
 * \returns (transfer none): the property value of the \a item
 * \deprecated Use wp_properties_item_get_value() instead
 */
const gchar *
wp_properties_iterator_item_get_value (const GValue * item)
{
  WpPropertiesItem *pi = g_value_get_boxed (item);
  g_return_val_if_fail (pi != NULL, NULL);
  return wp_properties_item_get_value (pi);
}

/*!
 * \brief gets the number of items
 * \ingroup wpproperties
 * \param self a properties object
 */
guint
wp_properties_get_count (WpProperties * self)
{
  const struct spa_dict *dict = wp_properties_peek_dict(self);
  g_return_val_if_fail (dict != NULL, 0);

  return dict->n_items;
}


/*!
 * \brief Sorts the keys in alphabetical order
 * \ingroup wpproperties
 * \param self a properties object
 */
void
wp_properties_sort (WpProperties * self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (!(self->flags & FLAG_IS_DICT));
  g_return_if_fail (!(self->flags & FLAG_NO_OWNERSHIP));

  return spa_dict_qsort (&self->props->dict);
}

/*!
 * \brief Gets the dictionary wrapped by a properties object
 *
 * \ingroup wpproperties
 * \param self a properties object
 * \returns (transfer none): the internal properties set as a `struct spa_dict *`
 */
const struct spa_dict *
wp_properties_peek_dict (WpProperties * self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return (self->flags & FLAG_IS_DICT) ? self->dict : &self->props->dict;
}

/*!
 * \brief Gets a copy of the properties object as a `struct pw_properties`
 *
 * \ingroup wpproperties
 * \param self a properties object
 * \returns (transfer full): a copy of the properties in \a self
 *   as a `struct pw_properties`
 */
struct pw_properties *
wp_properties_to_pw_properties (WpProperties * self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return pw_properties_new_dict (wp_properties_peek_dict (self));
}

/*!
 * \brief Similar to wp_properties_to_pw_properties(), but this method avoids making
 * a copy of the properties by returning the `struct pw_properties` that is
 * stored internally and then freeing the WpProperties wrapper.
 *
 * If \a self is not uniquely owned (see wp_properties_ensure_unique_owner()),
 * then this method does make a copy and is the same as
 * wp_properties_to_pw_properties(), performance-wise.
 *
 * \ingroup wpproperties
 * \param self (transfer full): a properties object
 * \returns (transfer full): the properties in \a self as a `struct pw_properties`
 */
struct pw_properties *
wp_properties_unref_and_take_pw_properties (WpProperties * self)
{
  g_return_val_if_fail (self != NULL, NULL);

  g_autoptr (WpProperties) unique = wp_properties_ensure_unique_owner (self);
  /* set the flag so that unref-ing \a unique will not destroy unique->props */
  unique->flags = FLAG_NO_OWNERSHIP;
  return unique->props;
}

/*!
 * \brief Checks if all property values contained in \a other are matching with
 * the values in \a self.
 *
 * If a property is contained in \a other and not in \a self, the result is not
 * matched. If a property is contained in both sets, then the value of the
 * property in \a other is interpreted as a glob-style pattern
 * (using g_pattern_match_simple()) and the value in \a self is checked to
 * see if it matches with this pattern.
 *
 * \ingroup wpproperties
 * \param self a properties object
 * \param other a set of properties to match
 * \returns TRUE if all matches were successfull, FALSE if at least one
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
