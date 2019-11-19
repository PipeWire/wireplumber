/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
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

WpProperties *
wp_properties_new_empty (void)
{
  WpProperties * self = g_rc_box_new (WpProperties);
  self->flags = 0;
  self->props = pw_properties_new (NULL, NULL);
  return self;
}

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

WpProperties *
wp_properties_new_valist (const gchar * key, va_list varargs)
{
  WpProperties * self = wp_properties_new_empty ();
  const gchar *value;

  while (key != NULL) {
    value = va_arg(varargs, gchar *);
    if (value && key[0])
      wp_properties_set (self, key, value);
    key = va_arg(varargs, gchar *);
  }

  return self;
}

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

WpProperties *
wp_properties_ref (WpProperties * self)
{
  return g_rc_box_acquire (self);
}

void
wp_properties_unref (WpProperties * self)
{
  g_rc_box_release_full (self, (GDestroyNotify) wp_properties_free);
}

gint
wp_properties_update_from_dict (WpProperties * self,
    const struct spa_dict * dict)
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);

  return pw_properties_update (self->props, dict);
}

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

const gchar *
wp_properties_get (WpProperties * self, const gchar * key)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (key != NULL, NULL);

  return spa_dict_lookup (wp_properties_peek_dict (self), key);
}

gint
wp_properties_set (WpProperties * self, const gchar * key,
    const gchar * value)
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);

  return pw_properties_set (self->props, key, value);
}

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

gint
wp_properties_setf_valist (WpProperties * self, const gchar * key,
    const gchar * format, va_list args)
{
  g_return_val_if_fail (self != NULL, -EINVAL);
  g_return_val_if_fail (!(self->flags & FLAG_IS_DICT), -EINVAL);

  return pw_properties_setva (self->props, key, format, args);
}

const struct spa_dict *
wp_properties_peek_dict (WpProperties * self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return (self->flags & FLAG_IS_DICT) ? self->dict : &self->props->dict;
}

struct pw_properties *
wp_properties_to_pw_properties (WpProperties * self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return pw_properties_new_dict (wp_properties_peek_dict (self));
}

gboolean
wp_properties_matches (WpProperties * self, WpProperties *other)
{
  const struct spa_dict * dict;
  const struct spa_dict_item *item;
  const gchar *value;

  g_return_val_if_fail (self != NULL, FALSE);

  /* Check if the property vakues matches the ones from 'other' */
  dict = wp_properties_peek_dict (self);
  spa_dict_for_each(item, dict) {
    value = wp_properties_get (other, item->key);
    if (value && g_strcmp0 (item->value, value) != 0)
      return FALSE;
  }

  return TRUE;
}

