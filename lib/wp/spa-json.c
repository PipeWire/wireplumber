/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-spa-json"

#include <spa/utils/defs.h>
#include <spa/utils/json.h>

#include "spa-json.h"

#define WP_SPA_JSON_STRING_INIT_SIZE 64
#define WP_SPA_JSON_BUILDER_INIT_SIZE 64

/*! \defgroup wpspajson WpSpaJson */
/*!
 * \struct WpSpaJson
 */
/*!
 * \struct WpSpaJsonBuilder
 */
/*!
 * \struct WpSpaJsonParser
 */

static void
builder_add_formatted (WpSpaJsonBuilder *self, const gchar *fmt, ...)
    G_GNUC_PRINTF (2, 3);

static WpSpaJsonBuilder *
wp_spa_json_builder_new_formatted (const gchar *fmt, ...)
    G_GNUC_PRINTF (1, 2);

enum {
  FLAG_NO_OWNERSHIP = (1 << 0),
};

struct _WpSpaJson
{
  grefcount ref;
  guint32 flags;

  /* only used if FLAG_NO_OWNERSHIP is not set */
  WpSpaJsonBuilder *builder;

  /* not used if constructed with _new_wrap() */
  struct spa_json json_data;

  /* json data */
  gchar *data;
  size_t size;
  struct spa_json *json;
};

G_DEFINE_BOXED_TYPE (WpSpaJson, wp_spa_json, wp_spa_json_ref, wp_spa_json_unref)

struct _WpSpaJsonBuilder
{
  gboolean add_separator;
  gchar *data;
  size_t size;
  size_t max_size;
};

G_DEFINE_BOXED_TYPE (WpSpaJsonBuilder, wp_spa_json_builder,
    wp_spa_json_builder_ref, wp_spa_json_builder_unref)

struct _WpSpaJsonParser
{
  WpSpaJson *json;
  struct spa_json data[2];
  struct spa_json *pos;
  struct spa_json curr;
};

G_DEFINE_BOXED_TYPE (WpSpaJsonParser, wp_spa_json_parser,
    wp_spa_json_parser_ref, wp_spa_json_parser_unref)

/*!
 * \brief Increases the reference count of a spa json object
 * \ingroup wpspajson
 * \param self a spa json object
 * \returns (transfer full): \a self with an additional reference count on it
 */
WpSpaJson *
wp_spa_json_ref (WpSpaJson *self)
{
  g_ref_count_inc (&self->ref);
  return self;
}

static void
wp_spa_json_free (WpSpaJson *self)
{
  g_clear_pointer (&self->builder, wp_spa_json_builder_unref);
  g_slice_free (WpSpaJson, self);
}

/*!
 * \brief Decreases the reference count on \a self and frees it when the ref
 * count reaches zero.
 * \ingroup wpspajson
 * \param self (transfer full): a spa json object
 */
void
wp_spa_json_unref (WpSpaJson *self)
{
  if (g_ref_count_dec (&self->ref))
    wp_spa_json_free (self);
}

static WpSpaJsonBuilder *
wp_spa_json_builder_new (const gchar *data, size_t size)
{
  WpSpaJsonBuilder *self = g_rc_box_new0 (WpSpaJsonBuilder);
  self->add_separator = FALSE;
  self->data = g_new0 (gchar, size + 1);
  self->max_size = size;
  memcpy (self->data, data, size),
  self->data[size] = '\0';
  self->size = size;
  return self;
}

static WpSpaJsonBuilder *
wp_spa_json_builder_new_formatted (const gchar *fmt, ...)
{
  va_list args;
  WpSpaJsonBuilder *self = g_rc_box_new0 (WpSpaJsonBuilder);
  self->add_separator = FALSE;
  va_start (args, fmt);
  self->data = g_strdup_vprintf (fmt, args);
  va_end (args);
  self->size = strlen (self->data);
  self->max_size = self->size;
  return self;
}

static WpSpaJson *
wp_spa_json_new_from_builder (WpSpaJsonBuilder *builder)
{
  WpSpaJson *self = g_slice_new0 (WpSpaJson);
  g_ref_count_init (&self->ref);
  self->flags = 0;
  self->builder = builder;
  self->data = builder->data;
  self->size = builder->size;
  spa_json_init (&self->json_data, self->data, self->size);
  self->json = &self->json_data;
  return self;
}

static WpSpaJson *
wp_spa_json_new (const gchar *data, size_t size)
{
  return wp_spa_json_new_from_builder (wp_spa_json_builder_new (data, size));
}

/*!
 * \brief Constructs a new WpSpaJson from a JSON string.
 *
 * \ingroup wpspajson
 * \param json_str a JSON string
 * \returns a new WpSpaJson that references the data in \a json_str. \a json_str
 *   is not copied, so it needs to stay alive.
 */
WpSpaJson *
wp_spa_json_new_from_string (const gchar *json_str)
{
  return wp_spa_json_new_from_stringn(json_str, strlen (json_str));
}

/*!
 * \brief Constructs a new WpSpaJson from a JSON string with specific length.
 *
 * \ingroup wpspajson
 * \param json_str a JSON string
 * \param len the specific length of the string
 * \returns a new WpSpaJson that references the data in \a json_str. \a json_str
 *   is not copied, so it needs to stay alive.
 */
WpSpaJson *
wp_spa_json_new_from_stringn (const gchar *json_str, size_t len)
{
  WpSpaJson *self = g_slice_new0 (WpSpaJson);
  g_ref_count_init (&self->ref);
  self->flags = FLAG_NO_OWNERSHIP;
  spa_json_init (&self->json_data, json_str, len);
  self->builder = NULL;
  self->data = (gchar *)self->json_data.cur;
  self->size = self->json_data.end - self->json_data.cur;
  self->json = &self->json_data;
  return self;
}

/*!
 * \brief Constructs a new WpSpaJson that wraps the given `spa_json`.
 *
 * \ingroup wpspajson
 * \param json a spa_json
 * \returns a new WpSpaJson that references the data in \a json. \a json is not
 *   copied, so it needs to stay alive.
 */
WpSpaJson *
wp_spa_json_new_wrap (struct spa_json *json)
{
  WpSpaJson *self = g_slice_new0 (WpSpaJson);
  g_ref_count_init (&self->ref);
  self->flags = FLAG_NO_OWNERSHIP;
  self->builder = NULL;
  self->data = (gchar *)json->cur;
  self->size = json->end - json->cur;
  self->json = json;
  return self;
}

/*!
 * \brief Converts a WpSpaJson pointer to a `struct spa_json` one, for use with
 * native pipewire & spa functions. The returned pointer is owned by WpSpaJson
 * and may not be modified or freed.
 *
 * \ingroup wpspajson
 * \param self a spa json object
 * \returns a const pointer to the underlying spa_json structure
 */
const struct spa_json *
wp_spa_json_get_spa_json (const WpSpaJson *self)
{
  return self->json;
}

/*!
 * \brief Returns the json data
 *
 * \ingroup wpspajson
 * \param self a spa json object
 * \returns a const pointer to the json data
 */
const gchar *
wp_spa_json_get_data (const WpSpaJson *self)
{
  return self->data;
}

/*!
 * \brief Returns the json data size
 *
 * \ingroup wpspajson
 * \param self a spa json object
 * \returns the json data size
 */
size_t
wp_spa_json_get_size (const WpSpaJson *self)
{
  return self->size;
}

/*!
 * \brief Returns a newly allocated json string with length matching the size
 *
 * \ingroup wpspajson
 * \param self a spa json object
 * \returns (transfer full): the json string with length matching the size
 * \since 0.4.11
 */
gchar *
wp_spa_json_to_string (const WpSpaJson *self)
{
  return g_strndup (self->data, self->size);
}

/*!
 * \brief Copies a spa json object
 *
 * \ingroup wpspajson
 * \param other a spa json object
 * \returns (transfer full): The newly copied spa json
 */
WpSpaJson *
wp_spa_json_copy (WpSpaJson *other)
{
  g_return_val_if_fail (other, NULL);
  g_return_val_if_fail (other->json, NULL);
  return wp_spa_json_new (other->data, other->size);
}

/*!
 * \brief Checks if the json is the unique owner of its data or not
 *
 * \ingroup wpspajson
 * \param self a spa json object
 * \returns TRUE if the json owns the data, FALSE otherwise.
 */
gboolean
wp_spa_json_is_unique_owner (WpSpaJson *self)
{
  return g_ref_count_compare (&self->ref, 1) &&
      !(self->flags & FLAG_NO_OWNERSHIP);
}

/*!
 * \brief If \a self is not uniquely owned already, then it is unrefed and a
 * copy of it is returned instead. You should always consider \a self as unsafe
 * to use after this call and you should use the returned object instead.
 *
 * \ingroup wpspajson
 * \param self (transfer full): a spa json object
 * \returns (transfer full): the uniquely owned spa json object which may or may
 * not be the same as \a self.
 */
WpSpaJson *
wp_spa_json_ensure_unique_owner (WpSpaJson *self)
{
  WpSpaJson *copy = NULL;

  if (wp_spa_json_is_unique_owner (self))
    return self;

  copy = wp_spa_json_copy (self);
  wp_spa_json_unref (self);
  return copy;
}

/*!
 * \brief Creates a spa json of type NULL
 * \ingroup wpspajson
 * \returns (transfer full): The new spa json
 */
WpSpaJson *
wp_spa_json_new_null (void)
{
  return wp_spa_json_new_from_builder (
      wp_spa_json_builder_new_formatted ("%s", "null"));
}

/*!
 * \brief Creates a spa json of type boolean
 *
 * \ingroup wpspajson
 * \param value the boolean value
 * \returns (transfer full): The new spa json
 */
WpSpaJson *
wp_spa_json_new_boolean (gboolean value)
{
  return wp_spa_json_new_from_builder (
      wp_spa_json_builder_new_formatted ("%s", value ? "true" : "false"));
}

/*!
 * \brief Creates a spa json of type int
 *
 * \ingroup wpspajson
 * \param value the int value
 * \returns (transfer full): The new spa json
 */
WpSpaJson *
wp_spa_json_new_int (gint value)
{
  return wp_spa_json_new_from_builder (
      wp_spa_json_builder_new_formatted ("%d", value));
}

/*!
 * \brief Creates a spa json of type float
 *
 * \ingroup wpspajson
 * \param value the float value
 * \returns (transfer full): The new spa json
 */
WpSpaJson *
wp_spa_json_new_float (float value)
{
  return wp_spa_json_new_from_builder (
      wp_spa_json_builder_new_formatted ("%.6f", value));
}

/*!
 * \brief Creates a spa json of type string
 *
 * \ingroup wpspajson
 * \param value the string value
 * \returns (transfer full): The new spa json
 */
WpSpaJson *
wp_spa_json_new_string (const gchar *value)
{
  size_t size = (strlen (value) * 4) + 2;
  gchar dst[size];
  spa_json_encode_string (dst, sizeof(dst), value);
  return wp_spa_json_new_from_builder (
      wp_spa_json_builder_new_formatted ("%s", dst));
}

/* Args is not a pointer in some architectures, so this needs to be a macro to
 * avoid args being copied */
#define wp_spa_json_builder_add_value(self,fmt,args)                           \
do {                                                                           \
  switch (*fmt) {                                                              \
    case 'n':                                                                  \
      wp_spa_json_builder_add_null (self);                                     \
      break;                                                                   \
    case 'b':                                                                  \
      wp_spa_json_builder_add_boolean (self, va_arg(args, gboolean));          \
      break;                                                                   \
    case 'i':                                                                  \
      wp_spa_json_builder_add_int (self, va_arg(args, gint));                  \
      break;                                                                   \
    case 'f':                                                                  \
      wp_spa_json_builder_add_float (self, (float)va_arg(args, double));       \
      break;                                                                   \
    case 's':                                                                  \
      wp_spa_json_builder_add_string (self, va_arg(args, const gchar *));      \
      break;                                                                   \
    case 'J':                                                                  \
      wp_spa_json_builder_add_json (self, va_arg(args, WpSpaJson *));          \
      break;                                                                   \
    default:                                                                   \
      break;                                                                   \
  }								               \
} while(false)

/*!
 * \brief Creates a spa json of type array
 *
 * \ingroup wpspajson
 * \param format (nullable): the first value format ("n", "b", "i", "f", "s" or "J")
 * \param ... a list of array types and values, followed by NULL
 * \returns (transfer full): The new spa json
 */
WpSpaJson *
wp_spa_json_new_array (const gchar *format, ...)
{
  va_list args;
  WpSpaJson *res;

  va_start (args, format);
  res = wp_spa_json_new_array_valist (format, args);
  va_end (args);
  return res;
}

/*!
 * \brief This is the `va_list` version of wp_spa_json_new_array()
 *
 * \ingroup wpspajson
 * \param format (nullable): the first value format ("n", "b", "i", "f", "s" or "J")
 * \param args the variable arguments passed to wp_spa_json_new_array()
 * \returns (transfer full): The new spa json
 */
WpSpaJson *
wp_spa_json_new_array_valist (const gchar *format, va_list args)
{
  g_autoptr (WpSpaJsonBuilder) b = wp_spa_json_builder_new_array ();

  if (!format)
    return wp_spa_json_builder_end (b);

  wp_spa_json_builder_add_value (b, format, args);
  wp_spa_json_builder_add_valist (b, args);
  return wp_spa_json_builder_end (b);
}

/*!
 * \brief Creates a spa json of type object
 *
 * \ingroup wpspajson
 * \param key (nullable): the first object property key
 * \param format (nullable): the first property format ("n", "b", "i", "f", "s" or "J")
 * \param ... a list of object properties and values, followed by NULL
 * \returns (transfer full): The new spa json
 */
WpSpaJson *
wp_spa_json_new_object (const gchar *key, const gchar *format, ...)
{
  va_list args;
  WpSpaJson *res;

  va_start (args, format);
  res = wp_spa_json_new_object_valist (key, format, args);
  va_end (args);
  return res;
}

/*!
 * \brief This is the `va_list` version of wp_spa_json_new_object()
 *
 * \ingroup wpspajson
 * \param key (nullable): the first object property key
 * \param format (nullable): the first property format ("n", "b", "i", "f", "s" or "J")
 * \param args the variable arguments passed to wp_spa_json_new_object()
 * \returns (transfer full): The new spa json
 */
WpSpaJson *
wp_spa_json_new_object_valist (const gchar *key, const gchar *format,
    va_list args)
{
  g_autoptr (WpSpaJsonBuilder) b = wp_spa_json_builder_new_object ();

  if (!key || !format)
    return wp_spa_json_builder_end (b);

  wp_spa_json_builder_add_property (b, key);
  wp_spa_json_builder_add_value (b, format, args);
  wp_spa_json_builder_add_valist (b, args);
  return wp_spa_json_builder_end (b);
}

/*!
 * \brief Checks wether the spa json is of type null or not
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \returns TRUE if it is of type null, FALSE otherwise
 */
gboolean
wp_spa_json_is_null (WpSpaJson *self)
{
  return spa_json_is_null (self->data, self->size);
}

/*!
 * \brief Checks wether the spa json is of type boolean or not
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \returns TRUE if it is of type boolean, FALSE otherwise
 */
gboolean
wp_spa_json_is_boolean (WpSpaJson *self)
{
  return spa_json_is_bool (self->data, self->size);
}

/*!
 * \brief Checks wether the spa json is of type int or not
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \returns TRUE if it is of type int, FALSE otherwise
 */
gboolean
wp_spa_json_is_int (WpSpaJson *self)
{
  return spa_json_is_int (self->data, self->size);
}

/*!
 * \brief Checks wether the spa json is of type float or not
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \returns TRUE if it is of type float, FALSE otherwise
 */
gboolean
wp_spa_json_is_float (WpSpaJson *self)
{
  return spa_json_is_float (self->data, self->size);
}

/*!
 * \brief Checks wether the spa json is of type string or not
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \returns TRUE if it is of type string, FALSE otherwise
 */
gboolean
wp_spa_json_is_string (WpSpaJson *self)
{
  return spa_json_is_string (self->data, self->size);
}

/*!
 * \brief Checks wether the spa json is of type array or not
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \returns TRUE if it is of type array, FALSE otherwise
 */
gboolean
wp_spa_json_is_array (WpSpaJson *self)
{
  return spa_json_is_array (self->data, self->size);
}

/*!
 * \brief Checks wether the spa json is of type object or not
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \returns TRUE if it is of type object, FALSE otherwise
 */
gboolean
wp_spa_json_is_object (WpSpaJson *self)
{
  return spa_json_is_object (self->data, self->size);
}

gboolean
wp_spa_json_parse_boolean_internal (const gchar *data, int len, gboolean *value)
{
  bool v = false;
  if (spa_json_parse_bool (data, len, &v) < 0)
    return FALSE;
  *value = v ? TRUE : FALSE;
  return TRUE;
}

/*!
 * \brief Parses the boolean value of a spa json object
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \param value (out): the boolean value
 * \returns TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_json_parse_boolean (WpSpaJson *self, gboolean *value)
{
  return wp_spa_json_parse_boolean_internal (self->data, self->size, value);
}

/*!
 * \brief Parses the int value of a spa json object
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \param value (out): the int value
 * \returns TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_json_parse_int (WpSpaJson *self, gint *value)
{
  return spa_json_parse_int (self->data, self->size, value) >= 0;
}

/*!
 * \brief Parses the float value of a spa json object
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \param value (out): the float value
 * \returns TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_json_parse_float (WpSpaJson *self, float *value)
{
  return spa_json_parse_float (self->data, self->size, value) >= 0;
}

static gchar *
wp_spa_json_parse_string_internal (const gchar *data, int len)
{
  gchar *res = g_new0 (gchar, len+1);
  if (res)
    spa_json_parse_string (data, len, res);
  return res;
}

/*!
 * \brief Parses the string value of a spa json object
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \returns (transfer full): The newly allocated parsed string
 */
gchar *
wp_spa_json_parse_string (WpSpaJson *self)
{
  return wp_spa_json_parse_string_internal (self->data, self->size);
}

/*!
 * \brief Parses the array types and values of a spa json object
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \param ... (out): the list of array types and values, followed by NULL
 * \returns TRUE if the types and values were obtained, FALSE otherwise
 */
gboolean
wp_spa_json_parse_array (WpSpaJson *self, ...)
{
  va_list args;
  gboolean res;
  va_start (args, self);
  res = wp_spa_json_parse_array_valist (self, args);
  va_end (args);
  return res;
}

/*!
 * \brief This is the `va_list` version of wp_spa_json_parse_array()
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \param args (out): the variable arguments passed to wp_spa_json_parse_array()
 * \returns TRUE if the types and values were obtained, FALSE otherwise
 */
gboolean
wp_spa_json_parse_array_valist (WpSpaJson *self, va_list args)
{
  g_autoptr (WpSpaJsonParser) p = wp_spa_json_parser_new_array (self);
  gboolean res = wp_spa_json_parser_get_valist (p, args);
  if (res)
    wp_spa_json_parser_end (p);
  return res;
}

/*!
 * \brief Parses the object properties and values of a spa json object
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \param ... (out): the list of object properties and values, followed by NULL
 * \returns TRUE if the properties and values were obtained, FALSE otherwise
 */
gboolean
wp_spa_json_parse_object (WpSpaJson *self, ...)
{
  va_list args;
  gboolean res;
  va_start (args, self);
  res = wp_spa_json_parse_object_valist (self, args);
  va_end (args);
  return res;
}

/*!
 * \brief This is the `va_list` version of wp_spa_json_parse_object()
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \param args (out): the variable arguments passed to wp_spa_json_parse_object()
 * \returns TRUE if the properties and values were obtained, FALSE otherwise
 */
gboolean
wp_spa_json_parse_object_valist (WpSpaJson *self, va_list args)
{
  g_autoptr (WpSpaJsonParser) p = wp_spa_json_parser_new_object (self);
  gboolean res = wp_spa_json_parser_get_valist (p, args);
  if (res)
    wp_spa_json_parser_end (p);
  return res;
}

/* Args is not a pointer in some architectures, so this needs to be a macro to
 * avoid args being copied */
#define wp_spa_json_parse_value(data,len,fmt,args)                             \
do {                                                                           \
  switch (*fmt) {                                                              \
    case 'n':                                                                  \
      if (!spa_json_is_null (data, len))                                       \
        return FALSE;                                                          \
      break;                                                                   \
    case 'b':                                                                  \
      if (!wp_spa_json_parse_boolean_internal (data, len,                      \
          va_arg(args, gboolean *)))                                           \
        return FALSE;                                                          \
      break;                                                                   \
    case 'i':                                                                  \
      if (spa_json_parse_int (data, len, va_arg(args, gint *)) < 0)            \
        return FALSE;                                                          \
      break;                                                                   \
    case 'f':                                                                  \
      if (spa_json_parse_float (data, len, va_arg(args, float *)) < 0)         \
        return FALSE;                                                          \
      break;                                                                   \
    case 's': {                                                                \
      gchar *str = wp_spa_json_parse_string_internal (data, len);              \
      if (!str)                                                                \
        return FALSE;                                                          \
      *va_arg(args, gchar **) = str;                                           \
      break;                                                                   \
    }                                                                          \
    case 'J': {                                                                \
      WpSpaJson *j = wp_spa_json_new (data, len);                              \
      if (!j)                                                                  \
        return FALSE;                                                          \
      *va_arg(args, WpSpaJson **) = j;                                         \
      break;                                                                   \
    }                                                                          \
    default:                                                                   \
      return FALSE;                                                            \
  }                                                                            \
} while(false)

/*!
 * \brief Parses the object property values of a spa json object
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \param ... the list of property names, formats and values, followed by NULL
 * \returns TRUE if the properties and values were obtained, FALSE otherwise
 */
gboolean
wp_spa_json_object_get (WpSpaJson *self, ...)
{
  va_list args;
  gboolean res;
  va_start (args, self);
  res = wp_spa_json_object_get_valist (self, args);
  va_end (args);
  return res;
}

/*!
 * \brief This is the `va_list` version of wp_spa_json_object_get()
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \param args the variable arguments passed to wp_spa_json_object_get()
 * \returns TRUE if the properties and values were obtained, FALSE otherwise
 */
gboolean
wp_spa_json_object_get_valist (WpSpaJson *self, va_list args)
{
  g_auto (GValue) item = G_VALUE_INIT;
  g_autoptr (WpIterator) it = NULL;
  const gchar *lookup_key = NULL;
  const gchar *lookup_fmt = NULL;

  g_return_val_if_fail (wp_spa_json_is_object (self), FALSE);

  lookup_key = va_arg(args, const gchar *);
  if (!lookup_key)
    return TRUE;
  lookup_fmt = va_arg(args, const gchar *);
  if (!lookup_fmt)
    return FALSE;

  it = wp_spa_json_new_iterator (self);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *key = g_value_get_boxed (&item);
    g_autofree gchar *key_str = NULL;
    WpSpaJson *value = NULL;

    key_str = wp_spa_json_parse_string (key);
    g_return_val_if_fail (key_str, FALSE);

    g_value_unset (&item);
    if (!wp_iterator_next (it, &item))
      return FALSE;
    value = g_value_get_boxed (&item);

    if (g_strcmp0 (key_str, lookup_key) == 0) {
      wp_spa_json_parse_value (value->data, value->size, lookup_fmt, args);
      lookup_key = va_arg(args, const gchar *);
      if (!lookup_key)
        return TRUE;
      lookup_fmt = va_arg(args, const gchar *);
      if (!lookup_fmt)
        return FALSE;
      wp_iterator_reset (it);
    }
  }

  return FALSE;
}

/*!
 * \brief Increases the reference count of a spa json builder
 *
 * \ingroup wpspajson
 * \param self a spa json builder object
 * \returns (transfer full): \a self with an additional reference count on it
 */
WpSpaJsonBuilder *
wp_spa_json_builder_ref (WpSpaJsonBuilder *self)
{
  return (WpSpaJsonBuilder *) g_rc_box_acquire ((gpointer) self);
}

static void
wp_spa_json_builder_free (WpSpaJsonBuilder *self)
{
  g_clear_pointer (&self->data, g_free);
}

/*!
 * \brief Decreases the reference count on \a self and frees it when the ref
 * count reaches zero.
 *
 * \ingroup wpspajson
 * \param self (transfer full): a spa json builder object
 */
void
wp_spa_json_builder_unref (WpSpaJsonBuilder *self)
{
  g_rc_box_release_full (self, (GDestroyNotify) wp_spa_json_builder_free);
}

/*!
 * \brief Creates a spa json builder of type array
 *
 * \ingroup wpspajson
 * \returns (transfer full): the new spa json builder
 */
WpSpaJsonBuilder *
wp_spa_json_builder_new_array (void)
{
  WpSpaJsonBuilder *self = g_rc_box_new0 (WpSpaJsonBuilder);
  self->add_separator = FALSE;
  self->data = g_new0 (gchar, WP_SPA_JSON_BUILDER_INIT_SIZE);
  self->max_size = WP_SPA_JSON_BUILDER_INIT_SIZE;
  self->data[0] = '[';
  self->size = 1;
  return self;
}

/*!
 * \brief Creates a spa json builder of type object
 *
 * \ingroup wpspajson
 * \returns (transfer full): the new spa json builder
 */
WpSpaJsonBuilder *
wp_spa_json_builder_new_object (void)
{
  WpSpaJsonBuilder *self = g_rc_box_new0 (WpSpaJsonBuilder);
  self->add_separator = FALSE;
  self->data = g_new0 (gchar, WP_SPA_JSON_BUILDER_INIT_SIZE);
  self->max_size = WP_SPA_JSON_BUILDER_INIT_SIZE;
  self->data[0] = '{';
  self->size = 1;
  return self;
}

static void
ensure_allocated_max_size (WpSpaJsonBuilder *self, size_t size)
{
  size_t new_size = self->size + size + 1;  /* '\0' because of vsnprintf */
  if (new_size > self->max_size) {
    size_t next_size = new_size * 2;
    self->data = g_realloc (self->data, next_size);
    self->max_size = next_size;
  }
}

static void
ensure_separator (WpSpaJsonBuilder *self, gboolean for_property)
{
  gboolean insert = (self->data[0] == '{' && for_property) ||
      (self->data[0] == '[' && !for_property);
  if (insert) {
    if (self->add_separator) {
      ensure_allocated_max_size (self, 2);
      self->data[self->size++] = ',';
      self->data[self->size++] = ' ';
    } else {
      self->add_separator = TRUE;
    }
  }
}

static void
builder_add_formatted (WpSpaJsonBuilder *self, const gchar *fmt, ...)
{
  int s;
  va_list args;
  va_start (args, fmt);
  s = vsnprintf (self->data + self->size, self->max_size - self->size, fmt, args);
  va_end (args);
  g_return_if_fail (s > 0);
  self->size += s;
}

static void
builder_add_json (WpSpaJsonBuilder *self, WpSpaJson *json)
{
  g_return_if_fail (self->max_size - self->size >= json->size + 1);
  snprintf (self->data + self->size, json->size + 1, "%s", json->data);
  self->size += json->size;
}

/*!
 * \brief Adds a property into the builder
 *
 * \ingroup wpspajson
 * \param self the spa json builder object
 * \param key the name of the property
 */
void
wp_spa_json_builder_add_property (WpSpaJsonBuilder *self, const gchar *key)
{
  size_t size = (strlen (key) * 4) + 3;
  gchar dst[size];
  ensure_separator (self, TRUE);
  ensure_allocated_max_size (self, size);
  spa_json_encode_string (dst, sizeof(dst), key);
  builder_add_formatted (self, "%s:", dst);
}

/*!
 * \brief Adds a null value into the builder
 *
 * \ingroup wpspajson
 * \param self the spa json builder object
 */
void
wp_spa_json_builder_add_null (WpSpaJsonBuilder *self)
{
  ensure_separator (self, FALSE);
  ensure_allocated_max_size (self, 4);
  builder_add_formatted (self, "%s", "null");
}

/*!
 * \brief Adds a boolean value into the builder
 *
 * \ingroup wpspajson
 * \param self the spa json builder object
 * \param value the boolean value
 */
void
wp_spa_json_builder_add_boolean (WpSpaJsonBuilder *self, gboolean value)
{
  ensure_separator (self, FALSE);
  ensure_allocated_max_size (self, value ? 4 : 5);
  builder_add_formatted (self, "%s", value ? "true" : "false");
}

/*!
 * \brief Adds a int value into the builder
 *
 * \ingroup wpspajson
 * \param self the spa json builder object
 * \param value the int value
 */
void
wp_spa_json_builder_add_int (WpSpaJsonBuilder *self, gint value)
{
  ensure_separator (self, FALSE);
  ensure_allocated_max_size (self, 16);
  builder_add_formatted (self, "%d", value);
}

/*!
 * \brief Adds a float value into the builder
 *
 * \ingroup wpspajson
 * \param self the spa json builder object
 * \param value the float value
 */
void
wp_spa_json_builder_add_float (WpSpaJsonBuilder *self, float value)
{
  ensure_separator (self, FALSE);
  ensure_allocated_max_size (self, 32);
  builder_add_formatted (self, "%.6f", value);
}

/*!
 * \brief Adds a string value into the builder
 *
 * \ingroup wpspajson
 * \param self the spa json builder object
 * \param value the string value
 */
void
wp_spa_json_builder_add_string (WpSpaJsonBuilder *self, const gchar *value)
{
  size_t size = (strlen (value) * 4) + 2;
  gchar dst[size];
  ensure_separator (self, FALSE);
  ensure_allocated_max_size (self, size);
  spa_json_encode_string (dst, sizeof(dst), value);
  builder_add_formatted (self, "%s", dst);
}

/*!
 * \brief Adds a json value into the builder
 *
 * \ingroup wpspajson
 * \param self the spa json builder object
 * \param json (transfer none): the json value
 */
void
wp_spa_json_builder_add_json (WpSpaJsonBuilder *self, WpSpaJson *json)
{
  ensure_separator (self, FALSE);
  ensure_allocated_max_size (self, json->size);
  builder_add_json (self, json);
}

/*!
 * \brief Adds values into the builder
 *
 * \ingroup wpspajson
 * \param self the spa json builder object
 * \param ... the json values
 */
void
wp_spa_json_builder_add (WpSpaJsonBuilder *self, ...)
{
  va_list args;
  va_start (args, self);
  wp_spa_json_builder_add_valist (self, args);
  va_end (args);
}

/*!
 * \brief This is the `va_list` version of wp_spa_json_builder_add()
 *
 * \ingroup wpspajson
 * \param self the spa json builder object
 * \param args the variable arguments passed to wp_spa_json_builder_add()
 */
void
wp_spa_json_builder_add_valist (WpSpaJsonBuilder *self, va_list args)
{
  do {
    const char *format = NULL;

    /* add property key if object */
    if (self->data[0] == '{') {
      const gchar *key = va_arg(args, const gchar *);
      if (!key)
        return;
      wp_spa_json_builder_add_property (self, key);
    }

    /* get value format */
    format = va_arg(args, const gchar *);
    if (!format)
      return;

    /* add value */
    wp_spa_json_builder_add_value (self, format, args);
  } while (TRUE);
}

/*!
 * \brief Ends the builder process and returns the constructed spa json object
 *
 * \ingroup wpspajson
 * \param self the spa json builder object
 * \returns (transfer full): the constructed spa json object
 */
WpSpaJson *
wp_spa_json_builder_end (WpSpaJsonBuilder *self)
{
  /* close */
  switch (self->data[0]) {
    case '[':  /* array */
      ensure_allocated_max_size (self, 2);
      self->data[self->size++] = ']';
      self->data[self->size] = '\0';
      break;
    case '{':  /* object */
      ensure_allocated_max_size (self, 2);
      self->data[self->size++] = '}';
      self->data[self->size] = '\0';
      break;
    default:
      break;
  }

  return wp_spa_json_new_from_builder (wp_spa_json_builder_ref (self));
}

/*!
 * \brief Increases the reference count of a spa json parser
 *
 * \ingroup wpspajson
 * \param self a spa json parser object
 * \returns (transfer full): \a self with an additional reference count on it
 */
WpSpaJsonParser *
wp_spa_json_parser_ref (WpSpaJsonParser *self)
{
  return (WpSpaJsonParser *) g_rc_box_acquire ((gpointer) self);
}

static void
wp_spa_json_parser_free (WpSpaJsonParser *self)
{
  self->json = NULL;
}

/*!
 * \brief Decreases the reference count on \a self and frees it when the ref
 * count reaches zero.
 *
 * \ingroup wpspajson
 * \param self (transfer full): a spa json parser object
 */
void
wp_spa_json_parser_unref (WpSpaJsonParser *self)
{
  g_rc_box_release_full (self, (GDestroyNotify) wp_spa_json_parser_free);
}

/*!
 * \brief Creates a spa json array parser. The \a json object must be valid for
 * the entire life-cycle of the returned parser.
 *
 * \ingroup wpspajson
 * \param json the spa json array to parse
 * \returns (transfer full): The new spa json parser
 */
WpSpaJsonParser *
wp_spa_json_parser_new_array (WpSpaJson *json)
{
  WpSpaJsonParser *self;

  g_return_val_if_fail (wp_spa_json_is_array (json), NULL);

  self = g_rc_box_new0 (WpSpaJsonParser);
  self->json = json;
  self->data[0] = *json->json;
  spa_json_enter_array (&self->data[0], &self->data[1]);
  self->pos = &self->data[1];
  return self;
}

/*!
 * \brief Creates a spa json object parser. The \a json object must be valid for
 * the entire life-cycle of the returned parser.
 *
 * \ingroup wpspajson
 * \param json the spa json object to parse
 * \returns (transfer full): The new spa json parser
 */
WpSpaJsonParser *
wp_spa_json_parser_new_object (WpSpaJson *json)
{
  WpSpaJsonParser *self;

  g_return_val_if_fail (wp_spa_json_is_object (json), NULL);

  self = g_rc_box_new0 (WpSpaJsonParser);
  self->json = json;
  self->data[0] = *json->json;
  spa_json_enter_object (&self->data[0], &self->data[1]);
  self->pos = &self->data[1];
  return self;
}

static int
check_nested_size (struct spa_json *parent, const gchar *data, int size)
{
  const gchar *nested_data;
  int nested_size;
  struct spa_json nested[2];

  /* only arrays and objects are considered nested data */
  if (!spa_json_is_array (data, size) && !spa_json_is_object (data, size))
    return 0;

  /* enter */
  nested[0] = *parent;
  spa_json_enter (&nested[0], &nested[1]);

  /* recursively advance */
  while ((nested_size = spa_json_next (&nested[1], &nested_data)) > 0) {
    if (check_nested_size (&nested[1], nested_data, nested_size) < 0)
      return -1;
  }
  if (nested_size < 0)
    return -1;

  /* advance one more time to reach end of nested data */
  if (spa_json_next (&nested[1], &nested_data) < 0)
    return -1;

  return nested_data - data;
}

static gboolean
wp_spa_json_parser_advance (WpSpaJsonParser *self)
{
  const gchar *data = NULL;
  int size, nested_size;

  if (!self->pos)
    return FALSE;

  /* advance */
  size = spa_json_next (self->pos, &data);
  if (size <= 0)
    return FALSE;
  g_return_val_if_fail (data != NULL, FALSE);

  /* if array or object, add the nested size */
  nested_size = check_nested_size (self->pos, data, size);
  if (nested_size < 0)
    return FALSE;
  size += nested_size;

  /* update current */
  spa_json_init (&self->curr, data, size);
  return TRUE;
}

/*!
 * \brief Gets the null value from a spa json parser
 *
 * \ingroup wpspajson
 * \param self the spa json parser object
 * \returns TRUE if the null value is present, FALSE otherwise
 */
gboolean
wp_spa_json_parser_get_null (WpSpaJsonParser *self)
{
  return wp_spa_json_parser_advance (self) &&
      spa_json_is_null (self->curr.cur, self->curr.end - self->curr.cur);
}

/*!
 * \brief Gets the boolean value from a spa json parser
 *
 * \ingroup wpspajson
 * \param self the spa json parser object
 * \param value (out): the boolean value
 * \returns TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_json_parser_get_boolean (WpSpaJsonParser *self, gboolean *value)
{
  return wp_spa_json_parser_advance (self) &&
      wp_spa_json_parse_boolean_internal (self->curr.cur,
          self->curr.end - self->curr.cur, value);
}

/*!
 * \brief Gets the int value from a spa json parser object
 *
 * \ingroup wpspajson
 * \param self the spa json parser object
 * \param value (out): the int value
 * \returns TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_json_parser_get_int (WpSpaJsonParser *self, gint *value)
{
  return wp_spa_json_parser_advance (self) &&
      spa_json_parse_int (self->curr.cur,
          self->curr.end - self->curr.cur, value) >= 0;
}

/*!
 * \brief Gets the float value from a spa json parser object
 *
 * \ingroup wpspajson
 * \param self the spa json parser object
 * \param value (out): the float value
 * \returns TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_json_parser_get_float (WpSpaJsonParser *self, float *value)
{
  return wp_spa_json_parser_advance (self) &&
      spa_json_parse_float (self->curr.cur,
          self->curr.end - self->curr.cur, value) >= 0;
}

/*!
 * \brief Gets the string value from a spa json parser object
 *
 * \ingroup wpspajson
 * \param self the spa json parser object
 * \returns (transfer full): The newly allocated parsed string
 */
gchar *
wp_spa_json_parser_get_string (WpSpaJsonParser *self)
{
  return wp_spa_json_parser_advance (self) ?
      wp_spa_json_parse_string_internal (self->curr.cur,
          self->curr.end - self->curr.cur) : NULL;
}

/*!
 * \brief Gets the spa json value from a spa json parser object
 *
 * \ingroup wpspajson
 * \param self the spa json parser object
 * \returns (transfer full): The spa json value or NULL if it could not be
 * obtained
 */
WpSpaJson *
wp_spa_json_parser_get_json (WpSpaJsonParser *self)
{
  return wp_spa_json_parser_advance (self) ?
      wp_spa_json_new_wrap (&self->curr) : NULL;
}

gboolean
wp_spa_json_parser_get_value (WpSpaJsonParser *self, const gchar *fmt,
    va_list args)
{
  if (wp_spa_json_parser_advance (self)) {
    wp_spa_json_parse_value (self->curr.cur, self->curr.end - self->curr.cur,
        fmt, args);
    return TRUE;
  }
  return FALSE;
}

/*!
 * \brief Gets the values from a spa json parser object
 *
 * \ingroup wpspajson
 * \param self the spa json parser object
 * \param ... (out): a list of values to get, followed by NULL
 * \returns TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_json_parser_get (WpSpaJsonParser *self, ...)
{
  gboolean res;
  va_list args;
  va_start (args, self);
  res = wp_spa_json_parser_get_valist (self, args);
  va_end (args);
  return res;
}

/*!
 * \brief This is the `va_list` version of wp_spa_json_parser_get()
 *
 * \ingroup wpspajson
 * \param self the spa json parser object
 * \param args the variable arguments passed to wp_spa_json_parser_get()
 * \returns TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_json_parser_get_valist (WpSpaJsonParser *self, va_list args)
{
  do {
    const char *format = NULL;

    /* parse property key if object */
    if (self->json->data[0] == '{') {
      gchar **key = va_arg(args, gchar **);
      if (!key)
        return TRUE;
      *key = wp_spa_json_parser_get_string (self);
      if (!(*key))
        return FALSE;
    }

    /* parse format */
    format = va_arg(args, const gchar *);
    if (!format)
      return TRUE;

    /* advance */
    if (!wp_spa_json_parser_advance (self))
      return FALSE;

    /* parse value */
    wp_spa_json_parse_value (self->curr.cur, self->curr.end - self->curr.cur,
        format, args);
  } while (TRUE);

  return FALSE;
}

void
wp_spa_json_parser_end (WpSpaJsonParser *self)
{
  self->pos = NULL;
}

struct _WpSpaJsonIterator
{
  WpSpaJson *json;
  WpSpaJsonParser *parser;
};
typedef struct _WpSpaJsonIterator WpSpaJsonIterator;

static void
wp_spa_json_iterator_reset (WpIterator *iterator)
{
  WpSpaJsonIterator *self = wp_iterator_get_user_data (iterator);
  g_clear_pointer (&self->parser, wp_spa_json_parser_unref);
}

static gboolean
wp_spa_json_iterator_next (WpIterator *iterator, GValue *item)
{
  WpSpaJsonIterator *self = wp_iterator_get_user_data (iterator);

  /* init iterator if first time */
  if (!self->parser) {
    switch (self->json->json->cur[0]) {
      case '[':  /* array */
        self->parser = wp_spa_json_parser_new_array (self->json);
        break;
      case '{':  /* object */
        self->parser = wp_spa_json_parser_new_object (self->json);
        break;
      default:
        return FALSE;
    }
  }

  /* advance */
  if (!wp_spa_json_parser_advance (self->parser))
    return FALSE;

  if (item) {
    g_value_init (item, WP_TYPE_SPA_JSON);
    g_value_take_boxed (item, wp_spa_json_new_wrap (&self->parser->curr));
  }

  return TRUE;
}

static void
wp_spa_json_iterator_finalize (WpIterator *iterator)
{
  WpSpaJsonIterator *self = wp_iterator_get_user_data (iterator);
  g_clear_pointer (&self->parser, wp_spa_json_parser_unref);
  g_clear_pointer (&self->json, wp_spa_json_unref);
}

/*!
 * \brief Creates a new iterator for a spa json object.
 *
 * \ingroup wpspajson
 * \param self the spa json object
 * \returns (transfer full): the new spa json iterator
 */
WpIterator *
wp_spa_json_new_iterator (WpSpaJson *self)
{
  static const WpIteratorMethods methods = {
    .version = WP_ITERATOR_METHODS_VERSION,
    .reset = wp_spa_json_iterator_reset,
    .next = wp_spa_json_iterator_next,
    .fold = NULL,
    .foreach = NULL,
    .finalize = wp_spa_json_iterator_finalize
  };
  WpIterator *it = wp_iterator_new (&methods, sizeof (WpSpaJsonIterator));
  WpSpaJsonIterator *jit = wp_iterator_get_user_data (it);

  jit->json = wp_spa_json_ref (self);
  jit->parser = NULL;

  return it;
}
