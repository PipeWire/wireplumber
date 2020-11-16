/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-spa-pod"

#include "spa-pod.h"
#include "spa-type.h"

#include <spa/utils/type-info.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>

#define WP_SPA_POD_BUILDER_REALLOC_STEP_SIZE 64

enum {
  FLAG_NO_OWNERSHIP = (1 << 0),
  FLAG_CONSTANT = (1 << 1)
};

typedef enum {
  WP_SPA_POD_REGULAR = 0,
  WP_SPA_POD_PROPERTY,
  WP_SPA_POD_CONTROL,
} WpSpaPodType;

struct _WpSpaPod
{
  grefcount ref;
  guint32 flags;

  /* The pipewire spa pod API does not have a type for Property and Control,
   * so we create our own and separate them with their data from the regular
   * spa pod types */
  WpSpaPodType type;

  /* Pod */
  union {
    struct spa_pod pod_none;
    struct spa_pod_bool pod_bool;
    struct spa_pod_id pod_id;
    struct spa_pod_int pod_int;
    struct spa_pod_long pod_long;
    struct spa_pod_float pod_float;
    struct spa_pod_double pod_double;
    struct spa_pod_pointer pod_pointer;
    struct spa_pod_fd pod_fd;
    struct spa_pod_rectangle pod_rectangle;
    struct spa_pod_fraction pod_fraction;
    struct wp_property_data {
      WpSpaTypeTable table;
      guint32 key;
      guint32 flags;
    } data_property;         /* Only used for property pods */
    struct wp_control_data {
      guint32 offset;
      guint32 type;
    } data_control;          /* Only used for control pods */
  } static_pod;              /* Only used for statically allocated pods */
  WpSpaPodBuilder *builder;  /* Only used for dynamically allocated pods */
  struct spa_pod *pod;
};

G_DEFINE_BOXED_TYPE (WpSpaPod, wp_spa_pod, wp_spa_pod_ref, wp_spa_pod_unref)

struct _WpSpaPodBuilder
{
  size_t size;
  guint8 *buf;
  struct spa_pod_builder builder;
  uint32_t type;
  struct spa_pod_frame frame;
  WpSpaTypeTable prop_table;  /* Only used when parsing properties */
};

G_DEFINE_BOXED_TYPE (WpSpaPodBuilder, wp_spa_pod_builder,
    wp_spa_pod_builder_ref, wp_spa_pod_builder_unref)

struct _WpSpaPodParser
{
  guint32 type;
  struct spa_pod_parser parser;
  WpSpaPod *pod;
  struct spa_pod_frame frame;
  WpSpaTypeTable prop_table;  /* Only used when parsing properties */
};

G_DEFINE_BOXED_TYPE (WpSpaPodParser, wp_spa_pod_parser,
    wp_spa_pod_parser_ref, wp_spa_pod_parser_unref)

static int
wp_spa_pod_builder_overflow (gpointer data, uint32_t size)
{
  WpSpaPodBuilder *self = data;
  const uint32_t next_size = self->size + WP_SPA_POD_BUILDER_REALLOC_STEP_SIZE;
  const uint32_t new_size = size > next_size ? size : next_size;
  self->buf = g_realloc (self->buf, new_size);
  self->builder.data = self->buf;
  self->builder.size = new_size;
  self->size = new_size;
  return 0;
}

static const struct spa_pod_builder_callbacks builder_callbacks = {
  SPA_VERSION_POD_BUILDER_CALLBACKS,
  .overflow = wp_spa_pod_builder_overflow
};

static WpSpaPodBuilder *
wp_spa_pod_builder_new (size_t size, uint32_t type)
{
  WpSpaPodBuilder *self = g_rc_box_new0 (WpSpaPodBuilder);
  self->size = size;
  self->buf = g_new0 (guint8, self->size);
  self->builder = SPA_POD_BUILDER_INIT (self->buf, self->size);
  self->type = type;

  spa_pod_builder_set_callbacks (&self->builder, &builder_callbacks, self);

  return self;
}


/**
 * wp_spa_pod_ref:
 * @self: a spa pod object
 *
 * Returns: (transfer full): @self with an additional reference count on it
 */
WpSpaPod *
wp_spa_pod_ref (WpSpaPod *self)
{
  g_ref_count_inc (&self->ref);
  return self;
}

static void
wp_spa_pod_free (WpSpaPod *self)
{
  g_clear_pointer (&self->builder, wp_spa_pod_builder_unref);
  self->pod = NULL;
  g_slice_free (WpSpaPod, self);
}

/**
 * wp_spa_pod_unref:
 * @self: (transfer full): a spa pod object
 *
 * Decreases the reference count on @self and frees it when the ref count
 * reaches zero.
 */
void
wp_spa_pod_unref (WpSpaPod *self)
{
  if (g_ref_count_dec (&self->ref))
    wp_spa_pod_free (self);
}

static WpSpaPod *
wp_spa_pod_new (const struct spa_pod *pod, WpSpaPodType type, guint32 flags)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->flags = flags;
  self->type = type;

  /* Copy the reference if no ownership, otherwise copy the pod */
  if (self->flags & FLAG_NO_OWNERSHIP) {
    self->pod = (struct spa_pod *)pod;
  } else {
    self->builder = wp_spa_pod_builder_new (
      SPA_ROUND_UP_N (sizeof (*pod) + pod->size, 8), pod->type);
    self->pod = self->builder->builder.data;
    spa_pod_builder_primitive (&self->builder->builder, pod);
  }

  /* Set the prop table if it is an object */
  if (pod->type == SPA_TYPE_Object) {
    WpSpaTypeTable prop_table = 0;
    wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_BASIC,
        ((struct spa_pod_object *) pod)->body.type, NULL, NULL, &prop_table);
    self->static_pod.data_property.table = prop_table;
  }

  return self;
}

/**
 * wp_spa_pod_new_wrap:
 * @pod: a spa_pod
 *
 * Returns: a new #WpSpaPod that references the data in @pod. @pod is not
 *   copied, so it needs to stay alive. The returned #WpSpaPod can be modified
 *   by using the setter functions, in which case @pod will be modified
 *   underneath.
 */
WpSpaPod *
wp_spa_pod_new_wrap (struct spa_pod *pod)
{
  return wp_spa_pod_new (pod, WP_SPA_POD_REGULAR, FLAG_NO_OWNERSHIP);
}

/**
 * wp_spa_pod_new_wrap_const:
 * @pod: a constant spa_pod
 *
 * Returns: a new #WpSpaPod that references the data in @pod. @pod is not
 *   copied, so it needs to stay alive. The returned #WpSpaPod cannot be
 *   modified, unless it's copied first.
 */
WpSpaPod *
wp_spa_pod_new_wrap_const (const struct spa_pod *pod)
{
  return wp_spa_pod_new (pod, WP_SPA_POD_REGULAR,
      FLAG_NO_OWNERSHIP | FLAG_CONSTANT);
}

static WpSpaPod *
wp_spa_pod_new_property_wrap (WpSpaTypeTable table, guint32 key, guint32 flags,
    struct spa_pod *pod)
{
  WpSpaPod *self = wp_spa_pod_new (pod, WP_SPA_POD_PROPERTY, FLAG_NO_OWNERSHIP);
  self->static_pod.data_property.table = table;
  self->static_pod.data_property.key = key;
  self->static_pod.data_property.flags = flags;
  return self;
}

static WpSpaPod *
wp_spa_pod_new_control_wrap (guint32 offset, guint32 type,
    struct spa_pod *pod)
{
  WpSpaPod *self = wp_spa_pod_new (pod, WP_SPA_POD_CONTROL, FLAG_NO_OWNERSHIP);
  self->static_pod.data_control.offset = offset;
  self->static_pod.data_control.type = type;
  return self;
}

#if 0
/* there is no use for these _const variants, but let's keep them just in case */

static WpSpaPod *
wp_spa_pod_new_property_wrap_const (WpSpaTypeTable table, guint32 key,
    guint32 flags, const struct spa_pod *pod)
{
  WpSpaPod *self = wp_spa_pod_new (pod, WP_SPA_POD_PROPERTY,
      FLAG_NO_OWNERSHIP | FLAG_CONSTANT);
  self->static_pod.data_property.table = table;
  self->static_pod.data_property.key = key;
  self->static_pod.data_property.flags = flags;
  return self;
}

static WpSpaPod *
wp_spa_pod_new_control_wrap_const (guint32 offset, guint32 type,
    const struct spa_pod *pod)
{
  WpSpaPod *self = wp_spa_pod_new (pod, WP_SPA_POD_CONTROL,
      FLAG_NO_OWNERSHIP | FLAG_CONSTANT);
  self->static_pod.data_control.offset = offset;
  self->static_pod.data_control.type = type;
  return self;
}
#endif

static WpSpaPod *
wp_spa_pod_new_wrap_copy (const struct spa_pod *pod)
{
  return wp_spa_pod_new (pod, WP_SPA_POD_REGULAR, 0);
}

static WpSpaPod *
wp_spa_pod_new_property_wrap_copy (WpSpaTypeTable table, guint32 key,
    guint32 flags, const struct spa_pod *pod)
{
  WpSpaPod *self = wp_spa_pod_new (pod, WP_SPA_POD_PROPERTY, 0);
  self->static_pod.data_property.table = table;
  self->static_pod.data_property.key = key;
  self->static_pod.data_property.flags = flags;
  return self;
}

static WpSpaPod *
wp_spa_pod_new_control_wrap_copy (guint32 offset, guint32 type,
    const struct spa_pod *pod)
{
  WpSpaPod *self = wp_spa_pod_new (pod, WP_SPA_POD_CONTROL, 0);
  self->static_pod.data_control.offset = offset;
  self->static_pod.data_control.type = type;
  return self;
}

/**
 * wp_spa_pod_get_spa_pod:
 * @self: a spa pod object
 *
 * Converts a #WpSpaPod pointer to a `struct spa_pod` one, for use with
 * native pipewire & spa functions. The returned pointer is owned by #WpSpaPod
 * and may not be modified or freed.
 *
 * Returns: a const pointer to the underlying spa_pod structure
 */
const struct spa_pod *
wp_spa_pod_get_spa_pod (const WpSpaPod *self)
{
  return self->pod;
}

/**
 * wp_spa_pod_get_type_name:
 * @self: a spa pod object
 *
 * Gets the type name of the spa pod object
 *
 * Returns: the type name of the spa pod object
 */
const char *
wp_spa_pod_get_type_name (WpSpaPod *self)
{
  const char *nick = NULL;
  if (!wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_BASIC, SPA_POD_TYPE (self->pod),
      NULL, &nick, NULL))
    g_return_val_if_reached (NULL);
  return nick;
}

const char *
wp_spa_pod_get_choice_type_name (WpSpaPod *self)
{
  g_return_val_if_fail (wp_spa_pod_is_choice (self), NULL);

  const char *nick = NULL;
  if (!wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_CHOICE,
      SPA_POD_CHOICE_TYPE (self->pod), NULL, &nick, NULL))
    g_return_val_if_reached (NULL);
  return nick;
}

const char *
wp_spa_pod_get_object_type_name (WpSpaPod *self)
{
  g_return_val_if_fail (wp_spa_pod_is_object (self), NULL);

  const char *nick = NULL;
  if (!wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_BASIC,
      SPA_POD_OBJECT_TYPE (self->pod), NULL, &nick, NULL))
    g_return_val_if_reached (NULL);
  return nick;
}

/**
 * wp_spa_pod_copy:
 * @other: a spa pod object
 *
 * Copies a spa pod object
 *
 * Returns: (transfer full): The newly copied spa pod
 */
WpSpaPod *
wp_spa_pod_copy (WpSpaPod *other)
{
  g_return_val_if_fail (other, NULL);
  switch (other->type) {
  case WP_SPA_POD_PROPERTY:
    return wp_spa_pod_new_property_wrap_copy (
        other->static_pod.data_property.table,
        other->static_pod.data_property.key,
        other->static_pod.data_property.flags, other->pod);
  case WP_SPA_POD_CONTROL:
    return wp_spa_pod_new_control_wrap_copy (
        other->static_pod.data_control.offset,
        other->static_pod.data_control.type, other->pod);
  case WP_SPA_POD_REGULAR:
  default:
    break;
  }
  return wp_spa_pod_new_wrap_copy (other->pod);
}

/**
 * wp_spa_pod_is_unique_owner:
 * @self: a spa pod object
 *
 * Checks if the pod is the unique owner of its data or not
 *
 * Returns: TRUE if the pod owns the data, FALSE otherwise.
 */
gboolean
wp_spa_pod_is_unique_owner (WpSpaPod *self)
{
  return g_ref_count_compare (&self->ref, 1) &&
      !(self->flags & FLAG_NO_OWNERSHIP);
}

/**
 * wp_spa_pod_ensure_unique_owner:
 * @self (transfer full): a spa pod object
 *
 * If @self is not uniquely owned already, then it is unrefed and a copy of
 * it is returned instead. You should always consider @self as unsafe to use
 * after this call and you should use the returned object instead.
 *
 * Returns: (transfer full): the uniquely owned spa pod object which may or may
 * not be the same as @self.
 */
WpSpaPod *
wp_spa_pod_ensure_unique_owner (WpSpaPod *self)
{
  WpSpaPod *copy = NULL;

  if (wp_spa_pod_is_unique_owner (self))
    return self;

  copy = wp_spa_pod_copy (self);
  wp_spa_pod_unref (self);
  return copy;
}

/**
 * wp_spa_pod_new_none:
 *
 * Creates a spa pod of type None
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_none (void)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;
  self->static_pod.pod_none = SPA_POD_INIT_None();
  self->pod = &self->static_pod.pod_none;
  return self;
}

/**
 * wp_spa_pod_new_boolean:
 * @value: the boolean value
 *
 * Creates a spa pod of type boolean
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_boolean (gboolean value)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;
  self->static_pod.pod_bool = SPA_POD_INIT_Bool (value ? true : false);
  self->pod = &self->static_pod.pod_bool.pod;
  return self;
}

/**
 * wp_spa_pod_new_id:
 * @value: the Id value
 *
 * Creates a spa pod of type Id
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_id (guint32 value)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;
  self->static_pod.pod_id = SPA_POD_INIT_Id (value);
  self->pod = &self->static_pod.pod_id.pod;
  return self;
}

/**
 * wp_spa_pod_new_int:
 * @value: the int value
 *
 * Creates a spa pod of type int
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_int (gint value)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;
  self->static_pod.pod_int = SPA_POD_INIT_Int (value);
  self->pod = &self->static_pod.pod_int.pod;
  return self;
}

/**
 * wp_spa_pod_new_long:
 * @value: the long value
 *
 * Creates a spa pod of type long
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_long (glong value)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;
  self->static_pod.pod_long = SPA_POD_INIT_Long (value);
  self->pod = &self->static_pod.pod_long.pod;
  return self;
}

/**
 * wp_spa_pod_new_float:
 * @value: the float value
 *
 * Creates a spa pod of type float
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_float (float value)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;
  self->static_pod.pod_float = SPA_POD_INIT_Float (value);
  self->pod = &self->static_pod.pod_float.pod;
  return self;
}

/**
 * wp_spa_pod_new_double:
 * @value: the double value
 *
 * Creates a spa pod of type double
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_double (double value)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;
  self->static_pod.pod_double = SPA_POD_INIT_Double (value);
  self->pod = &self->static_pod.pod_double.pod;
  return self;
}

/**
 * wp_spa_pod_new_string:
 * @value: the string value
 *
 * Creates a spa pod of type string
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_string (const char *value)
{
  const uint32_t len = value ? strlen (value) : 0;
  const char *str = value ? value : "";
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;

  struct spa_pod_string p = SPA_POD_INIT_String (len + 1);
  self->builder = wp_spa_pod_builder_new (
      SPA_ROUND_UP_N (sizeof (p) + len + 1, 8), SPA_TYPE_String);

  self->pod = self->builder->builder.data;

  spa_pod_builder_raw (&self->builder->builder, &p, sizeof(p));
  spa_pod_builder_write_string (&self->builder->builder, str, len);
  return self;
}

/**
 * wp_spa_pod_new_bytes:
 * @value: the bytes value
 * @len: the length of the bytes value
 *
 * Creates a spa pod of type bytes
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_bytes (gconstpointer value, guint32 len)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;
  const struct spa_pod_bytes p = SPA_POD_INIT_Bytes (len);
  self->builder = wp_spa_pod_builder_new (
      SPA_ROUND_UP_N (sizeof (p) + p.pod.size, 8),
      SPA_TYPE_Bytes);

  self->pod = self->builder->builder.data;

  spa_pod_builder_raw (&self->builder->builder, &p, sizeof(p));
  spa_pod_builder_raw_padded (&self->builder->builder, value, len);
  return self;
}

/**
 * wp_spa_pod_new_pointer:
 * @type_name: the type name the pointer points to
 * @value: the pointer value
 *
 * Creates a spa pod of type pointer
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_pointer (const char *type_name, gconstpointer value)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;
  guint32 type = 0;
  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_BASIC, type_name, &type,
      NULL, NULL))
    g_return_val_if_reached (NULL);
  self->static_pod.pod_pointer = SPA_POD_INIT_Pointer (type, value);
  self->pod = &self->static_pod.pod_pointer.pod;
  return self;
}

/**
 * wp_spa_pod_new_fd:
 * @value: the Fd value
 *
 * Creates a spa pod of type Fd
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_fd (gint64 value)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;
  self->static_pod.pod_fd = SPA_POD_INIT_Fd (value);
  self->pod = &self->static_pod.pod_fd.pod;
  return self;
}

/**
 * wp_spa_pod_new_rectangle:
 * @width: the width value of the rectangle
 * @height: the height value of the rectangle
 *
 * Creates a spa pod of type rectangle
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_rectangle (guint32 width, guint32 height)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;
  self->static_pod.pod_rectangle =
      SPA_POD_INIT_Rectangle (SPA_RECTANGLE (width, height));
  self->pod = &self->static_pod.pod_rectangle.pod;
  return self;
}

/**
 * wp_spa_pod_new_fraction:
 * @num: the numerator value of the fraction
 * @denom: the denominator value of the fraction
 *
 * Creates a spa pod of type fraction
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_fraction (guint32 num, guint32 denom)
{
  WpSpaPod *self = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&self->ref);
  self->type = WP_SPA_POD_REGULAR;
  self->static_pod.pod_fraction =
      SPA_POD_INIT_Fraction (SPA_FRACTION (num, denom));
  self->pod = &self->static_pod.pod_fraction.pod;
  return self;
}

/**
 * wp_spa_pod_new_choice:
 * @type_name: the type name of the choice type
 * @...: a list of choice values, followed by %NULL
 *
 * Creates a spa pod of type choice
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_choice (const char *type_name, ...)
{
  WpSpaPod *self;
  va_list args;

  va_start (args, type_name);
  self = wp_spa_pod_new_choice_valist (type_name, args);
  va_end (args);

  return self;
}

/**
 * wp_spa_pod_new_choice_valist:
 * @type_name: the type name of the choice type
 * @args: the variable arguments passed to wp_spa_pod_new_choice()
 *
 * This is the `va_list` version of wp_spa_pod_new_choice()
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_choice_valist (const char *type_name, va_list args)
{
  g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_choice (type_name);
  wp_spa_pod_builder_add_valist (b, args);
  return wp_spa_pod_builder_end (b);
}

/**
 * wp_spa_pod_new_object:
 * @type_name: the type name of the object type
 * @id_name: the id name of the object
 * @...: a list of object properties with their values, followed by %NULL
 *
 * Creates a spa pod of type object
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_object (const char *type_name, const char *id_name, ...)
{
  WpSpaPod *self;
  va_list args;

  va_start (args, id_name);
  self = wp_spa_pod_new_object_valist (type_name, id_name, args);
  va_end (args);

  return self;
}

/**
 * wp_spa_pod_new_object_valist:
 * @type_name: the type name of the object type
 * @id_name: the id name of the object
 * @args: the variable arguments passed to wp_spa_pod_new_object()
 *
 * This is the `va_list` version of wp_spa_pod_new_object()
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_object_valist (const char *type_name, const char *id_name,
    va_list args)
{
  g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_object (type_name,
      id_name);
  wp_spa_pod_builder_add_valist (b, args);
  return wp_spa_pod_builder_end (b);
}

/**
 * wp_spa_pod_new_sequence:
 * @unit: the unit of the sequence
 * @...: a list of sequence controls with their values, followed by %NULL
 *
 * Creates a spa pod of type sequence
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_sequence (guint unit, ...)
{
  WpSpaPod *self;
  va_list args;

  va_start(args, unit);
  self = wp_spa_pod_new_sequence_valist (unit, args);
  va_end(args);

  return self;
}

/**
 * wp_spa_pod_new_sequence_valist:
 * @unit: the unit of the sequence
 * @args: the variable arguments passed to wp_spa_pod_new_sequence()
 *
 * This is the `va_list` version of wp_spa_pod_new_sequence()
 *
 * Returns: (transfer full): The new spa pod
 */
WpSpaPod *
wp_spa_pod_new_sequence_valist (guint unit, va_list args)
{
  g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_sequence (unit);
  wp_spa_pod_builder_add_valist (b, args);
  return wp_spa_pod_builder_end (b);
}

/**
 * wp_spa_pod_is_none:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type none or not
 *
 * Returns: TRUE if it is of type none, FALSE otherwise
 */
gboolean
wp_spa_pod_is_none (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_none (self->pod);
}

/**
 * wp_spa_pod_is_boolean:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type boolean or not
 *
 * Returns: TRUE if it is of type boolean, FALSE otherwise
 */
gboolean
wp_spa_pod_is_boolean (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR &&
      spa_pod_is_bool (self->pod) ? TRUE : FALSE;
}

/**
 * wp_spa_pod_is_id:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type Id or not
 *
 * Returns: TRUE if it is of type Id, FALSE otherwise
 */
gboolean
wp_spa_pod_is_id (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_id (self->pod);
}

/**
 * wp_spa_pod_is_int:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type int or not
 *
 * Returns: TRUE if it is of type int, FALSE otherwise
 */
gboolean
wp_spa_pod_is_int (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_int (self->pod);
}

/**
 * wp_spa_pod_is_long:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type long or not
 *
 * Returns: TRUE if it is of type long, FALSE otherwise
 */
gboolean
wp_spa_pod_is_long (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_long (self->pod);
}

/**
 * wp_spa_pod_is_float:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type float or not
 *
 * Returns: TRUE if it is of type float, FALSE otherwise
 */
gboolean
wp_spa_pod_is_float (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_float (self->pod);
}

/**
 * wp_spa_pod_is_double:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type double or not
 *
 * Returns: TRUE if it is of type double, FALSE otherwise
 */
gboolean
wp_spa_pod_is_double (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_double (self->pod);
}

/**
 * wp_spa_pod_is_string:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type string or not
 *
 * Returns: TRUE if it is of type string, FALSE otherwise
 */
gboolean
wp_spa_pod_is_string (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_string (self->pod);
}

/**
 * wp_spa_pod_is_bytes:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type bytes or not
 *
 * Returns: TRUE if it is of type bytes, FALSE otherwise
 */
gboolean
wp_spa_pod_is_bytes (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_bytes (self->pod);
}

/**
 * wp_spa_pod_is_pointer:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type pointer or not
 *
 * Returns: TRUE if it is of type pointer, FALSE otherwise
 */
gboolean
wp_spa_pod_is_pointer (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_pointer (self->pod);
}

/**
 * wp_spa_pod_is_fd:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type Fd or not
 *
 * Returns: TRUE if it is of type Fd, FALSE otherwise
 */
gboolean
wp_spa_pod_is_fd (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_fd (self->pod);
}

/**
 * wp_spa_pod_is_rectangle:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type rectangle or not
 *
 * Returns: TRUE if it is of type rectangle, FALSE otherwise
 */
gboolean
wp_spa_pod_is_rectangle (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_rectangle (self->pod);
}

/**
 * wp_spa_pod_is_fraction:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type fraction or not
 *
 * Returns: TRUE if it is of type fraction, FALSE otherwise
 */
gboolean
wp_spa_pod_is_fraction (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_fraction (self->pod);
}

/**
 * wp_spa_pod_is_array:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type array or not
 *
 * Returns: TRUE if it is of type array, FALSE otherwise
 */
gboolean
wp_spa_pod_is_array (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_array (self->pod);
}

/**
 * wp_spa_pod_is_choice:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type choice or not
 *
 * Returns: TRUE if it is of type choice, FALSE otherwise
 */
gboolean
wp_spa_pod_is_choice (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_choice (self->pod);
}

/**
 * wp_spa_pod_is_object:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type object or not
 *
 * Returns: TRUE if it is of type object, FALSE otherwise
 */
gboolean
wp_spa_pod_is_object (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_object (self->pod);
}

/**
 * wp_spa_pod_is_struct:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type struct or not
 *
 * Returns: TRUE if it is of type struct, FALSE otherwise
 */
gboolean
wp_spa_pod_is_struct (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_struct (self->pod);
}

/**
 * wp_spa_pod_is_sequence:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type sequence or not
 *
 * Returns: TRUE if it is of type sequence, FALSE otherwise
 */
gboolean
wp_spa_pod_is_sequence (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_REGULAR && spa_pod_is_sequence (self->pod);
}

/**
 * wp_spa_pod_is_property:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type property or not
 *
 * Returns: TRUE if it is of type property, FALSE otherwise
 */
gboolean
wp_spa_pod_is_property (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_PROPERTY;
}

/**
 * wp_spa_pod_is_control:
 * @self: the spa pod object
 *
 * Checks wether the spa pod is of type control or not
 *
 * Returns: TRUE if it is of type control, FALSE otherwise
 */
gboolean
wp_spa_pod_is_control (WpSpaPod *self)
{
  return self->type == WP_SPA_POD_CONTROL;
}

/**
 * wp_spa_pod_get_boolean:
 * @self: the spa pod object
 * @value: (out): the boolean value
 *
 * Gets the boolean value of a spa pod object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_boolean (WpSpaPod *self, gboolean *value)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (value, FALSE);
  bool v = FALSE;
  const int res = spa_pod_get_bool (self->pod, &v);
  *value = v ? TRUE : FALSE;
  return res >= 0;
}

/**
 * wp_spa_pod_get_id:
 * @self: the spa pod object
 * @value: (out): the Id value
 *
 * Gets the Id value of a spa pod object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_id (WpSpaPod *self, guint32 *value)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (value, FALSE);
  uint32_t v = 0;
  const int res = spa_pod_get_id (self->pod, &v);
  *value = v;
  return res >= 0;
}

/**
 * wp_spa_pod_get_int:
 * @self: the spa pod object
 * @value: (out): the int value
 *
 * Gets the int value of a spa pod object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_int (WpSpaPod *self, gint *value)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (value, FALSE);
  return spa_pod_get_int (self->pod, value) >= 0;
}

/**
 * wp_spa_pod_get_long:
 * @self: the spa pod object
 * @value: (out): the long value
 *
 * Gets the long value of a spa pod object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_long (WpSpaPod *self, glong *value)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (value, FALSE);
  return spa_pod_get_long (self->pod, value) >= 0;
}

/**
 * wp_spa_pod_get_float:
 * @self: the spa pod object
 * @value: (out): the float value
 *
 * Gets the float value of a spa pod object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_float (WpSpaPod *self, float *value)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (value, FALSE);
  return spa_pod_get_float (self->pod, value) >= 0;
}

/**
 * wp_spa_pod_get_double:
 * @self: the spa pod object
 * @value: (out): the double value
 *
 * Gets the double value of a spa pod object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_double (WpSpaPod *self, double *value)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (value, FALSE);
  return spa_pod_get_double (self->pod, value) >= 0;
}

/**
 * wp_spa_pod_get_string:
 * @self: the spa pod object
 * @value: (out): the string value
 *
 * Gets the string value of a spa pod object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_string (WpSpaPod *self, const char **value)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (value, FALSE);
  return spa_pod_get_string (self->pod, value) >= 0;
}

/**
 * wp_spa_pod_get_bytes:
 * @self: the spa pod object
 * @value: (out): the bytes value
 * @len: (out): the length of the bytes value
 *
 * Gets the bytes value and its len of a spa pod object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_bytes (WpSpaPod *self, gconstpointer *value, guint32 *len)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (value, FALSE);
  g_return_val_if_fail (len, FALSE);
  return spa_pod_get_bytes (self->pod, value, len) >= 0;
}

/**
 * wp_spa_pod_get_pointer:
 * @self: the spa pod object
 * @type_name: (out): the type name of the pointer value
 * @value: (out): the pointer value
 *
 * Gets the pointer value and its type name of a spa pod object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_pointer (WpSpaPod *self, const char **type_name,
    gconstpointer *value)
{
  gboolean res;

  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (type_name, FALSE);
  g_return_val_if_fail (value, FALSE);

  guint32 type = 0;
  res = spa_pod_get_pointer (self->pod, &type, value) >= 0;
  if (!wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_BASIC, type, NULL, type_name,
      NULL))
    g_return_val_if_reached (FALSE);
  return res;
}

/**
 * wp_spa_pod_get_fd:
 * @self: the spa pod object
 * @value: (out): the Fd value
 *
 * Gets the Fd value of a spa pod object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_fd (WpSpaPod *self, gint64 *value)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (value, FALSE);
  return spa_pod_get_fd (self->pod, value) >= 0;
}

/**
 * wp_spa_pod_get_rectangle:
 * @self: the spa pod object
 * @width: (out): the rectangle's width value
 * @height: (out): the rectangle's height value
 *
 * Gets the rectangle's width and height value of a spa pod object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_rectangle (WpSpaPod *self, guint32 *width, guint32 *height)
{
  g_return_val_if_fail (self, FALSE);
  struct spa_rectangle rectangle = { 0, };
  const gboolean res = spa_pod_get_rectangle (self->pod, &rectangle) >= 0;
  if (width)
    *width = rectangle.width;
  if (height)
    *height = rectangle.height;
  return res;
}

/**
 * wp_spa_pod_get_fraction:
 * @self: the spa pod object
 * @num: (out): the fractions's numerator value
 * @denom: (out): the fractions's denominator value
 *
 * Gets the fractions's numerator and denominator value of a spa pod object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_fraction (WpSpaPod *self, guint32 *num, guint32 *denom)
{
  g_return_val_if_fail (self, FALSE);
  struct spa_fraction fraction = { 0, };
  const gboolean res = spa_pod_get_fraction (self->pod, &fraction) >= 0;
  if (num)
    *num = fraction.num;
  if (denom)
    *denom = fraction.denom;
  return res;
}

/**
 * wp_spa_pod_set_boolean:
 * @self: the spa pod object
 * @value: the boolean value
 *
 * Sets a boolean value in the spa pod object.
 *
 * Returns: TRUE if the value could be set, FALSE othewrise.
 */
gboolean
wp_spa_pod_set_boolean (WpSpaPod *self, gboolean value)
{
  g_return_val_if_fail (wp_spa_pod_is_boolean (self), FALSE);
  g_return_val_if_fail (!(self->flags & FLAG_CONSTANT), FALSE);
  ((struct spa_pod_bool *)self->pod)->value = value ? true : false;
  return TRUE;
}

/**
 * wp_spa_pod_set_id:
 * @self: the spa pod object
 * @value: the Id value
 *
 * Sets an Id value in the spa pod object.
 *
 * Returns: TRUE if the value could be set, FALSE othewrise.
 */
gboolean
wp_spa_pod_set_id (WpSpaPod *self, guint32 value)
{
  g_return_val_if_fail (wp_spa_pod_is_id (self), FALSE);
  g_return_val_if_fail (!(self->flags & FLAG_CONSTANT), FALSE);
  ((struct spa_pod_id *)self->pod)->value = value;
  return TRUE;
}

/**
 * wp_spa_pod_set_int:
 * @self: the spa pod object
 * @value: the int value
 *
 * Sets an int value in the spa pod object.
 *
 * Returns: TRUE if the value could be set, FALSE othewrise.
 */
gboolean
wp_spa_pod_set_int (WpSpaPod *self, gint value)
{
  g_return_val_if_fail (wp_spa_pod_is_int (self), FALSE);
  g_return_val_if_fail (!(self->flags & FLAG_CONSTANT), FALSE);
  ((struct spa_pod_int *)self->pod)->value = value;
  return TRUE;
}

/**
 * wp_spa_pod_set_long:
 * @self: the spa pod object
 * @value: the long value
 *
 * Sets a long value in the spa pod object.
 *
 * Returns: TRUE if the value could be set, FALSE othewrise.
 */
gboolean
wp_spa_pod_set_long (WpSpaPod *self, glong value)
{
  g_return_val_if_fail (wp_spa_pod_is_long (self), FALSE);
  g_return_val_if_fail (!(self->flags & FLAG_CONSTANT), FALSE);
  ((struct spa_pod_long *)self->pod)->value = value;
  return TRUE;
}

/**
 * wp_spa_pod_set_float:
 * @self: the spa pod object
 * @value: the float value
 *
 * Sets a float value in the spa pod object.
 *
 * Returns: TRUE if the value could be set, FALSE othewrise.
 */
gboolean
wp_spa_pod_set_float (WpSpaPod *self, float value)
{
  g_return_val_if_fail (wp_spa_pod_is_float (self), FALSE);
  g_return_val_if_fail (!(self->flags & FLAG_CONSTANT), FALSE);
  ((struct spa_pod_float *)self->pod)->value = value;
  return TRUE;
}

/**
 * wp_spa_pod_set_double:
 * @self: the spa pod object
 * @value: the double value
 *
 * Sets a double value in the spa pod object.
 *
 * Returns: TRUE if the value could be set, FALSE othewrise.
 */
gboolean
wp_spa_pod_set_double (WpSpaPod *self, double value)
{
  g_return_val_if_fail (wp_spa_pod_is_double (self), FALSE);
  g_return_val_if_fail (!(self->flags & FLAG_CONSTANT), FALSE);
  ((struct spa_pod_double *)self->pod)->value = value;
  return TRUE;
}

/**
 * wp_spa_pod_set_pointer:
 * @self: the spa pod object
 * @type_name: the type name the pointer points to
 * @value: the pointer value
 *
 * Sets a pointer value with its type name in the spa pod object.
 *
 * Returns: TRUE if the value could be set, FALSE othewrise.
 */
gboolean
wp_spa_pod_set_pointer (WpSpaPod *self, const char *type_name,
    gconstpointer value)
{
  guint32 id = 0;

  g_return_val_if_fail (wp_spa_pod_is_pointer (self), FALSE);
  g_return_val_if_fail (!(self->flags & FLAG_CONSTANT), FALSE);

  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_BASIC, type_name, &id, NULL,
      NULL))
    g_return_val_if_reached (FALSE);

  ((struct spa_pod_pointer *)self->pod)->body.type = id;
  ((struct spa_pod_pointer *)self->pod)->body.value = value;
  return TRUE;
}

/**
 * wp_spa_pod_set_fd:
 * @self: the spa pod object
 * @value: the Fd value
 *
 * Sets a Fd value in the spa pod object.
 *
 * Returns: TRUE if the value could be set, FALSE othewrise.
 */
gboolean
wp_spa_pod_set_fd (WpSpaPod *self, gint64 value)
{
  g_return_val_if_fail (wp_spa_pod_is_fd (self), FALSE);
  g_return_val_if_fail (!(self->flags & FLAG_CONSTANT), FALSE);
  ((struct spa_pod_fd *)self->pod)->value = value;
  return TRUE;
}

/**
 * wp_spa_pod_set_rectangle:
 * @self: the spa pod object
 * @width: the width value of the rectangle
 * @height: the height value of the rectangle
 *
 * Sets the width and height values of a rectangle in the spa pod object.
 *
 * Returns: TRUE if the value could be set, FALSE othewrise.
 */
gboolean
wp_spa_pod_set_rectangle (WpSpaPod *self, guint32 width, guint32 height)
{
  g_return_val_if_fail (wp_spa_pod_is_rectangle (self), FALSE);
  g_return_val_if_fail (!(self->flags & FLAG_CONSTANT), FALSE);
  ((struct spa_pod_rectangle *)self->pod)->value.width = width;
  ((struct spa_pod_rectangle *)self->pod)->value.height = height;
  return TRUE;
}

/**
 * wp_spa_pod_set_fraction:
 * @self: the spa pod object
 * @num: the numerator value of the farction
 * @denom: the denominator value of the fraction
 *
 * Sets the numerator and denominator values of a fraction in the spa pod object.
 *
 * Returns: TRUE if the value could be set, FALSE othewrise.
 */
gboolean
wp_spa_pod_set_fraction (WpSpaPod *self, guint32 num, guint32 denom)
{
  g_return_val_if_fail (wp_spa_pod_is_fraction (self), FALSE);
  g_return_val_if_fail (!(self->flags & FLAG_CONSTANT), FALSE);
  ((struct spa_pod_fraction *)self->pod)->value.num = num;
  ((struct spa_pod_fraction *)self->pod)->value.denom = denom;
  return TRUE;
}

/**
 * wp_spa_pod_set_pod:
 * @self: the spa pod object
 * @pod: the pod with the value to be set
 *
 * Sets the value of a spa pod object in the current spa pod object. THe spa pod
 * objects must be of the same value.
 *
 * Returns: TRUE if the value could be set, FALSE othewrise.
 */
gboolean
wp_spa_pod_set_pod (WpSpaPod *self, WpSpaPod *pod)
{
  g_return_val_if_fail (self->type == pod->type, FALSE);
  g_return_val_if_fail (SPA_POD_TYPE (self->pod) == SPA_POD_TYPE (pod->pod), FALSE);
  g_return_val_if_fail (!(self->flags & FLAG_CONSTANT), FALSE);

  switch (SPA_POD_TYPE (self->pod)) {
  case SPA_TYPE_None:
    break;
  case SPA_TYPE_Bool:
    ((struct spa_pod_bool *)self->pod)->value = ((struct spa_pod_bool *)pod->pod)->value;
    break;
  case SPA_TYPE_Id:
    ((struct spa_pod_id *)self->pod)->value = ((struct spa_pod_id *)pod->pod)->value;
    break;
  case SPA_TYPE_Int:
    ((struct spa_pod_int *)self->pod)->value = ((struct spa_pod_int *)pod->pod)->value;
    break;
  case SPA_TYPE_Long:
    ((struct spa_pod_long *)self->pod)->value = ((struct spa_pod_long *)pod->pod)->value;
    break;
  case SPA_TYPE_Float:
    ((struct spa_pod_float *)self->pod)->value = ((struct spa_pod_float *)pod->pod)->value;
    break;
  case SPA_TYPE_Double:
    ((struct spa_pod_double *)self->pod)->value = ((struct spa_pod_double *)pod->pod)->value;
    break;
  case SPA_TYPE_Pointer:
    ((struct spa_pod_pointer *)self->pod)->body.type = ((struct spa_pod_pointer *)pod->pod)->body.type;
    ((struct spa_pod_pointer *)self->pod)->body.value = ((struct spa_pod_pointer *)pod->pod)->body.value;
    break;
  case SPA_TYPE_Fd:
    ((struct spa_pod_fd *)self->pod)->value = ((struct spa_pod_fd *)pod->pod)->value;
    break;
  case SPA_TYPE_Rectangle:
    ((struct spa_pod_rectangle *)self->pod)->value.width = ((struct spa_pod_rectangle *)pod->pod)->value.width;
    ((struct spa_pod_rectangle *)self->pod)->value.height = ((struct spa_pod_rectangle *)pod->pod)->value.height;
    break;
  case SPA_TYPE_Fraction:
    ((struct spa_pod_fraction *)self->pod)->value.num = ((struct spa_pod_fraction *)pod->pod)->value.num;
    ((struct spa_pod_fraction *)self->pod)->value.denom = ((struct spa_pod_fraction *)pod->pod)->value.denom;
    break;
  default:
    g_return_val_if_fail (self->pod->size >= pod->pod->size, FALSE);
    memcpy (SPA_MEMBER (self->pod, sizeof (struct spa_pod), void),
        SPA_MEMBER (pod->pod, sizeof (struct spa_pod), void),
        SPA_MIN (self->pod->size, pod->pod->size));
    self->pod->type = pod->pod->type;
    self->pod->size = pod->pod->size;
    break;
  }

  switch (self->type) {
  case WP_SPA_POD_PROPERTY:
    self->static_pod.data_property.table = pod->static_pod.data_property.table;
    self->static_pod.data_property.key = pod->static_pod.data_property.key;
    self->static_pod.data_property.flags = pod->static_pod.data_property.flags;
    break;
  case WP_SPA_POD_CONTROL:
    self->static_pod.data_control.offset = pod->static_pod.data_control.offset;
    self->static_pod.data_control.type = pod->static_pod.data_control.type;
    break;
  case WP_SPA_POD_REGULAR:
  default:
    break;
  }

  return TRUE;
}

/**
 * wp_spa_pod_equal:
 * @self: the spa pod object
 * @pod: the pod with the value to be compared with
 *
 * Checks whether two spa pod objects have the same value or not
 *
 * Returns: TRUE if both spa pod objects have the same values, FALSE othewrise.
 */
gboolean
wp_spa_pod_equal (WpSpaPod *self, WpSpaPod *pod)
{
  if (self->type != pod->type)
    return FALSE;
  if (SPA_POD_TYPE (self->pod) != SPA_POD_TYPE (pod->pod))
    return FALSE;

  switch (SPA_POD_TYPE (self->pod)) {
  case SPA_TYPE_None:
    break;
  case SPA_TYPE_Bool:
    if (((struct spa_pod_bool *)self->pod)->value != ((struct spa_pod_bool *)pod->pod)->value)
      return FALSE;
    break;
  case SPA_TYPE_Id:
    if (((struct spa_pod_id *)self->pod)->value != ((struct spa_pod_id *)pod->pod)->value)
      return FALSE;
    break;
  case SPA_TYPE_Int:
    if (((struct spa_pod_int *)self->pod)->value != ((struct spa_pod_int *)pod->pod)->value)
      return FALSE;
    break;
  case SPA_TYPE_Long:
    if (((struct spa_pod_long *)self->pod)->value != ((struct spa_pod_long *)pod->pod)->value)
      return FALSE;
    break;
  case SPA_TYPE_Float:
    if (((struct spa_pod_float *)self->pod)->value != ((struct spa_pod_float *)pod->pod)->value)
      return FALSE;
    break;
  case SPA_TYPE_Double:
    if (((struct spa_pod_double *)self->pod)->value != ((struct spa_pod_double *)pod->pod)->value)
      return FALSE;
    break;
  case SPA_TYPE_Pointer:
    if (((struct spa_pod_pointer *)self->pod)->body.type != ((struct spa_pod_pointer *)pod->pod)->body.type ||
        ((struct spa_pod_pointer *)self->pod)->body.value != ((struct spa_pod_pointer *)pod->pod)->body.value)
      return FALSE;
    break;
  case SPA_TYPE_Fd:
    if (((struct spa_pod_fd *)self->pod)->value != ((struct spa_pod_fd *)pod->pod)->value)
      return FALSE;
    break;
  case SPA_TYPE_Rectangle:
    if (((struct spa_pod_rectangle *)self->pod)->value.width != ((struct spa_pod_rectangle *)pod->pod)->value.width ||
        ((struct spa_pod_rectangle *)self->pod)->value.height != ((struct spa_pod_rectangle *)pod->pod)->value.height)
      return FALSE;
    break;
  case SPA_TYPE_Fraction:
    if (((struct spa_pod_fraction *)self->pod)->value.num != ((struct spa_pod_fraction *)pod->pod)->value.num ||
        ((struct spa_pod_fraction *)self->pod)->value.denom != ((struct spa_pod_fraction *)pod->pod)->value.denom)
      return FALSE;
    break;
  default:
    if (self->pod->size != pod->pod->size ||
        memcmp (SPA_MEMBER (self->pod, sizeof (struct spa_pod), void),
            SPA_MEMBER (pod->pod, sizeof (struct spa_pod), void),
            self->pod->size) != 0)
      return FALSE;
    break;
  }

  switch (self->type) {
  case WP_SPA_POD_PROPERTY:
    if (self->static_pod.data_property.table != pod->static_pod.data_property.table ||
        self->static_pod.data_property.table != pod->static_pod.data_property.table ||
        self->static_pod.data_property.flags != pod->static_pod.data_property.flags)
      return FALSE;
    break;
  case WP_SPA_POD_CONTROL:
    if (self->static_pod.data_control.offset != pod->static_pod.data_control.offset ||
        self->static_pod.data_control.type != pod->static_pod.data_control.type)
      return FALSE;
    break;
  case WP_SPA_POD_REGULAR:
  default:
    break;
  }

  return TRUE;
}

/**
 * wp_spa_pod_get_object:
 * @self: the spa pod object
 * @type_name: the type name of the object type
 * @id_name: (out): the id name of the object
 * @...: (out): the list of the object properties values, followed by %NULL
 *
 * Gets the object properties values of a spa pod object
 *
 * Returns: TRUE if the object properties values were obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_object (WpSpaPod *self, const char *type_name,
    const char **id_name, ...)
{
  va_list args;
  gboolean res;
  va_start (args, id_name);
  res = wp_spa_pod_get_object_valist (self, type_name, id_name, args);
  va_end (args);
  return res;
}

/**
 * wp_spa_pod_get_object_valist:
 * @self: the spa pod object
 * @type_name: the type name of the object type
 * @id_name: (out): the id name of the object
 * @args: (out): the variable arguments passed to wp_spa_pod_get_object()
 *
 * This is the `va_list` version of wp_spa_pod_get_object()
 *
 * Returns: TRUE if the object properties values were obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_object_valist (WpSpaPod *self, const char *type_name,
    const char **id_name, va_list args)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (wp_spa_pod_is_object (self), FALSE);
  g_autoptr (WpSpaPodParser) p = wp_spa_pod_parser_new_object (self, type_name,
      id_name);
  const gboolean res = wp_spa_pod_parser_get_valist (p, args);
  wp_spa_pod_parser_end (p);
  return res;
}

/**
 * wp_spa_pod_get_struct:
 * @self: the spa pod object
 * @...: (out): the list of the struct values, followed by %NULL
 *
 * Gets the struct's values of a spa pod object
 *
 * Returns: TRUE if the struct values were obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_struct (WpSpaPod *self, ...)
{
  va_list args;
  gboolean res;
  va_start (args, self);
  res = wp_spa_pod_get_struct_valist (self, args);
  va_end (args);
  return res;
}

/**
 * wp_spa_pod_get_struct_valist:
 * @self: the spa pod object
 * @args: (out): the variable arguments passed to wp_spa_pod_get_struct()
 *
 * This is the `va_list` version of wp_spa_pod_get_struct()
 *
 * Returns: TRUE if the struct values were obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_struct_valist (WpSpaPod *self, va_list args)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (wp_spa_pod_is_struct (self), FALSE);
  g_autoptr (WpSpaPodParser) p = wp_spa_pod_parser_new_struct (self);
  const gboolean res = wp_spa_pod_parser_get_valist (p, args);
  wp_spa_pod_parser_end (p);
  return res;
}

/**
 * wp_spa_pod_get_property:
 * @self: the spa pod object
 * @key: (out) (optional): the name of the property
 * @value: (out) (optional): the spa pod value of the property
 *
 * Gets the name, flags and spa pod value of a spa pod property
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_property (WpSpaPod *self, const char **key,
    WpSpaPod **value)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (wp_spa_pod_is_property (self), FALSE);

  if (key && !wp_spa_type_get_by_id (self->static_pod.data_property.table,
      self->static_pod.data_property.key, NULL, key, NULL))
    return FALSE;
  if (value)
    *value = wp_spa_pod_new_wrap (self->pod);

  return TRUE;
}

/**
 * wp_spa_pod_get_control:
 * @self: the spa pod object
 * @offset: (out) (optional): the offset of the control
 * @type_name: (out) (optional): the type name of the control
 * @value: (out) (optional): the spa pod value of the control
 *
 * Gets the offset, type name and spa pod value of a spa pod control
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_get_control (WpSpaPod *self, guint32 *offset, const char **type_name,
    WpSpaPod **value)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (wp_spa_pod_is_control (self), FALSE);

  if (offset)
    *offset = self->static_pod.data_control.offset;
  if (type_name && !wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_CONTROL,
      self->static_pod.data_control.type, NULL, type_name, NULL))
    g_return_val_if_reached (FALSE);
  if (value)
    *value = wp_spa_pod_new_wrap (self->pod);

  return TRUE;
}

/**
 * wp_spa_pod_get_choice_child:
 * @self: a spa pod choice object
 *
 * Gets the child of a spa pod choice object
 *
 * Returns: (transfer full): the child of the spa pod choice object
 */
WpSpaPod *
wp_spa_pod_get_choice_child (WpSpaPod *self)
{
  g_return_val_if_fail (wp_spa_pod_is_choice (self), NULL);
  return wp_spa_pod_new_wrap (SPA_POD_CHOICE_CHILD (self->pod));
}

/**
 * wp_spa_pod_get_array_child:
 * @self: a spa pod choice object
 *
 * Gets the child of a spa pod array object
 *
 * Returns: (transfer full): the child of the spa pod array object
 */
WpSpaPod *
wp_spa_pod_get_array_child (WpSpaPod *self)
{
  g_return_val_if_fail (wp_spa_pod_is_array (self), NULL);
  return wp_spa_pod_new_wrap (SPA_POD_ARRAY_CHILD (self->pod));
}

/**
 * wp_spa_pod_builder_ref:
 * @self: a spa pod builder object
 *
 * Returns: (transfer full): @self with an additional reference count on it
 */
WpSpaPodBuilder *
wp_spa_pod_builder_ref (WpSpaPodBuilder *self)
{
  return (WpSpaPodBuilder *) g_rc_box_acquire ((gpointer) self);
}

static void
wp_spa_pod_builder_free (WpSpaPodBuilder *self)
{
  g_clear_pointer (&self->buf, g_free);
}

/**
 * wp_spa_pod_builder_unref:
 * @self: (transfer full): a spa pod builder object
 *
 * Decreases the reference count on @self and frees it when the ref count
 * reaches zero.
 */
void
wp_spa_pod_builder_unref (WpSpaPodBuilder *self)
{
  g_rc_box_release_full (self, (GDestroyNotify) wp_spa_pod_builder_free);
}

/**
 * wp_spa_pod_builder_new_array:
 *
 * Creates a spa pod builder of type array
 *
 * Returns: (transfer full): the new spa pod builder
 */
WpSpaPodBuilder *
wp_spa_pod_builder_new_array (void)
{
  WpSpaPodBuilder *self = wp_spa_pod_builder_new (
      WP_SPA_POD_BUILDER_REALLOC_STEP_SIZE, SPA_TYPE_Array);
  spa_pod_builder_push_array (&self->builder, &self->frame);
  return self;
}

/**
 * wp_spa_pod_builder_new_choice:
 * @type_name: the type name of the choice type
 *
 * Creates a spa pod builder of type choice
 *
 * Returns: (transfer full): the new spa pod builder
 */
WpSpaPodBuilder *
wp_spa_pod_builder_new_choice (const char *type_name)
{
  WpSpaPodBuilder *self = NULL;
  guint32 type = 0;

  /* Construct the builder */
  self = wp_spa_pod_builder_new (WP_SPA_POD_BUILDER_REALLOC_STEP_SIZE,
      SPA_TYPE_Choice);

  /* Find the choice type */
  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_CHOICE, type_name, &type,
      NULL, NULL))
    g_return_val_if_reached (NULL);

  /* Push the array */
  spa_pod_builder_push_choice (&self->builder, &self->frame, type,
      WP_SPA_TYPE_TABLE_CHOICE);

  return self;
}

/**
 * wp_spa_pod_builder_new_object:
 * @type_name: the type name of the object type
 * @id_name: the Id name of the object
 *
 * Creates a spa pod builder of type object
 *
 * Returns: (transfer full): the new spa pod builder
 */
WpSpaPodBuilder *
wp_spa_pod_builder_new_object (const char *type_name, const char *id_name)
{
  WpSpaPodBuilder *self = NULL;
  guint32 type = 0;
  guint32 id = 0;

  /* Construct the builder */
  self = wp_spa_pod_builder_new (WP_SPA_POD_BUILDER_REALLOC_STEP_SIZE,
      SPA_TYPE_Object);

  /* Find the type */
  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_BASIC, type_name, &type,
      NULL, &self->prop_table))
    g_return_val_if_reached (NULL);

  /* Find the id */
  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_PARAM, id_name, &id, NULL,
      NULL))
    g_return_val_if_reached (NULL);

  /* Push the object */
  spa_pod_builder_push_object (&self->builder, &self->frame, type, id);

  return self;
}

/**
 * wp_spa_pod_builder_new_struct:
 *
 * Creates a spa pod builder of type struct
 *
 * Returns: (transfer full): the new spa pod builder
 */
WpSpaPodBuilder *
wp_spa_pod_builder_new_struct (void)
{
  WpSpaPodBuilder *self = NULL;
  self = wp_spa_pod_builder_new (WP_SPA_POD_BUILDER_REALLOC_STEP_SIZE,
      SPA_TYPE_Struct);
  spa_pod_builder_push_struct (&self->builder, &self->frame);
  return self;
}

/**
 * wp_spa_pod_builder_new_sequence:
 *
 * Creates a spa pod builder of type sequence
 *
 * Returns: (transfer full): the new spa pod builder
 */
WpSpaPodBuilder *
wp_spa_pod_builder_new_sequence (guint unit)
{
  WpSpaPodBuilder *self = NULL;
  self = wp_spa_pod_builder_new (WP_SPA_POD_BUILDER_REALLOC_STEP_SIZE,
      SPA_TYPE_Sequence);
  spa_pod_builder_push_sequence (&self->builder, &self->frame, unit);
  return self;
}

/**
 * wp_spa_pod_builder_add_none:
 * @self: the spa pod builder object
 *
 * Adds a none value into the builder
 */
void
wp_spa_pod_builder_add_none (WpSpaPodBuilder *self)
{
  spa_pod_builder_none (&self->builder);
}

/**
 * wp_spa_pod_builder_add_boolean:
 * @self: the spa pod builder object
 * @value: the boolean value
 *
 * Adds a boolean value into the builder
 */
void
wp_spa_pod_builder_add_boolean (WpSpaPodBuilder *self, gboolean value)
{
  spa_pod_builder_bool (&self->builder, value ? true : false);
}

/**
 * wp_spa_pod_builder_add_id:
 * @self: the spa pod builder object
 * @value: the Id value
 *
 * Adds a Id value into the builder
 */
void
wp_spa_pod_builder_add_id (WpSpaPodBuilder *self, guint32 value)
{
  spa_pod_builder_id (&self->builder, value);
}

/**
 * wp_spa_pod_builder_add_int:
 * @self: the spa pod builder object
 * @value: the int value
 *
 * Adds a int value into the builder
 */
void
wp_spa_pod_builder_add_int (WpSpaPodBuilder *self, gint value)
{
  spa_pod_builder_int (&self->builder, value);
}

/**
 * wp_spa_pod_builder_add_long:
 * @self: the spa pod builder object
 * @value: the long value
 *
 * Adds a long value into the builder
 */
void
wp_spa_pod_builder_add_long (WpSpaPodBuilder *self, glong value)
{
  spa_pod_builder_long (&self->builder, value);
}

/**
 * wp_spa_pod_builder_add_float:
 * @self: the spa pod builder object
 * @value: the float value
 *
 * Adds a float value into the builder
 */
void
wp_spa_pod_builder_add_float (WpSpaPodBuilder *self, float value)
{
  spa_pod_builder_float (&self->builder, value);
}

/**
 * wp_spa_pod_builder_add_double:
 * @self: the spa pod builder object
 * @value: the double value
 *
 * Adds a double value into the builder
 */
void
wp_spa_pod_builder_add_double (WpSpaPodBuilder *self, double value)
{
  spa_pod_builder_double (&self->builder, value);
}

/**
 * wp_spa_pod_builder_add_string:
 * @self: the spa pod builder object
 * @value: the string value
 *
 * Adds a string value into the builder
 */
void
wp_spa_pod_builder_add_string (WpSpaPodBuilder *self, const char *value)
{
  spa_pod_builder_string (&self->builder, value);
}

/**
 * wp_spa_pod_builder_add_bytes:
 * @self: the spa pod builder object
 * @value: the bytes value
 * @len: the length of the bytes value
 *
 * Adds a bytes value with its length into the builder
 */
void
wp_spa_pod_builder_add_bytes (WpSpaPodBuilder *self, gconstpointer value,
    guint32 len)
{
  spa_pod_builder_bytes (&self->builder, value, len);
}

/**
 * wp_spa_pod_builder_add_pointer:
 * @self: the spa pod builder object
 * @type_name: the type name that the pointer points to
 * @value: the pointer vaue
 *
 * Adds a pointer value with its type name into the builder
 */
void
wp_spa_pod_builder_add_pointer (WpSpaPodBuilder *self, const char *type_name,
    gconstpointer value)
{
  guint32 type = 0;
  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_BASIC, type_name, &type,
      NULL, NULL))
    g_return_if_reached ();
  spa_pod_builder_pointer (&self->builder, type, value);
}

/**
 * wp_spa_pod_builder_add_fd:
 * @self: the spa pod builder object
 * @value: the Fd value
 *
 * Adds a Fd value into the builder
 */
void
wp_spa_pod_builder_add_fd (WpSpaPodBuilder *self, gint64 value)
{
  spa_pod_builder_fd (&self->builder, value);
}

/**
 * wp_spa_pod_builder_add_rectangle:
 * @self: the spa pod builder object
 * @width: the width value of the rectangle
 * @height: the height value of the rectangle
 *
 * Adds the width and height values of a rectangle into the builder
 */
void
wp_spa_pod_builder_add_rectangle (WpSpaPodBuilder *self, guint32 width,
    guint32 height)
{
  spa_pod_builder_rectangle (&self->builder, width, height);
}

/**
 * wp_spa_pod_builder_add_fraction:
 * @self: the spa pod builder object
 * @num: the numerator value of the fraction
 * @denom: the denominator value of the fraction
 *
 * Adds the numerator and denominator values of a fraction into the builder
 */
void
wp_spa_pod_builder_add_fraction (WpSpaPodBuilder *self, guint32 num,
    guint32 denom)
{
  spa_pod_builder_fraction (&self->builder, num, denom);
}

/**
 * wp_spa_pod_builder_add_pod:
 * @self: the spa pod builder object
 * @pod: the pod value
 *
 * Adds a pod value into the builder
 */
void
wp_spa_pod_builder_add_pod (WpSpaPodBuilder *self, WpSpaPod *pod)
{
  spa_pod_builder_primitive (&self->builder, pod->pod);
}

/**
 * wp_spa_pod_builder_add_property:
 * @self: the spa pod builder object
 * @key: the name of the property
 *
 * Adds a property into the builder
 */
void
wp_spa_pod_builder_add_property (WpSpaPodBuilder *self, const char *key)
{
  guint32 id = 0;
  if (!wp_spa_type_get_by_nick (self->prop_table, key, &id, NULL, NULL))
      g_return_if_reached ();
  spa_pod_builder_prop (&self->builder, id, 0);
}

/**
 * wp_spa_pod_builder_add_property_id:
 * @self: the spa pod builder object
 * @id: the id of the property
 *
 * Adds a property into the builder
 */
void
wp_spa_pod_builder_add_property_id (WpSpaPodBuilder *self, guint32 id)
{
  spa_pod_builder_prop (&self->builder, id, 0);
}

/**
 * wp_spa_pod_builder_add_control:
 * @self: the spa pod builder object
 * @offset: the offset of the control
 * @type_name: the type name of the control
 *
 * Adds a control into the builder
 */
void
wp_spa_pod_builder_add_control (WpSpaPodBuilder *self, guint32 offset,
    const char *type_name)
{
  guint type = 0;
  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_CONTROL, type_name, &type,
      NULL, NULL))
    g_return_if_reached ();
  spa_pod_builder_control (&self->builder, offset, type);
}

/**
 * wp_spa_pod_builder_add:
 * @self: the spa pod builder object
 * @...: a list of additional values, followed by %NULL
 *
 * Adds a list of values into the builder
 */
void
wp_spa_pod_builder_add (WpSpaPodBuilder *self, ...)
{
  va_list args;
  va_start (args, self);
  wp_spa_pod_builder_add_valist (self, args);
  va_end (args);
}

/**
 * wp_spa_pod_builder_add_valist:
 * @self: the spa pod builder object
 * @args: the variable arguments passed to wp_spa_pod_builder_add()
 *
 * Adds a list of values into the builder
 */
void
wp_spa_pod_builder_add_valist (WpSpaPodBuilder *self, va_list args)
{
  do {
    const char *format;
    int n_values = 1;
    struct spa_pod_frame f;
    gboolean choice;

    switch (self->type) {
    case SPA_TYPE_Object:
    {
      guint32 key = 0;
      const char *key_name = va_arg(args, const char *);
      if (!key_name)
        return;
      if (!wp_spa_type_get_by_nick (self->prop_table, key_name, &key, NULL,
          NULL))
        g_return_if_reached ();
      if (key == 0)
        return;

      spa_pod_builder_prop (&self->builder, key, 0);
      break;
    }
    case SPA_TYPE_Sequence:
    {
      guint32 offset = va_arg(args, uint32_t);
      guint32 type = 0;
      if (offset == 0)
        return;
      const char *control_name = va_arg(args, const char *);
      if (!control_name)
        return;
      if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_CONTROL, control_name,
          &type, NULL, NULL))
        g_return_if_reached ();
      if (type == 0)
        return;

      spa_pod_builder_control (&self->builder, offset, type);
    }
    default:
      break;
    }

    if ((format = va_arg(args, const char *)) == NULL)
      break;

    choice = *format == '?';
    if (choice) {
      uint32_t type = spa_choice_from_id (*++format);
      if (*format != '\0')
        format++;
      spa_pod_builder_push_choice (&self->builder, &f, type, 0);
      n_values = va_arg(args, int);
    }
    while (n_values-- > 0) {
      switch (*format) {
      case 'P':  /* Pod */
      case 'V':  /* Choice */
      case 'O':  /* Object */
      case 'T':  /* Struct */
        spa_pod_builder_primitive (&self->builder, va_arg(args, WpSpaPod *)->pod);
        break;
      default:
        SPA_POD_BUILDER_COLLECT(&self->builder, *format, args);
        break;
      }
    }

    if (choice)
      spa_pod_builder_pop (&self->builder, &f);

  } while (TRUE);
}

/**
 * wp_spa_pod_builder_end:
 * @self: the spa pod builder object
 *
 * Ends the builder process and returns the constructed spa pod object
 *
 * Returns: (transfer full): the constructed spa pod object
 */
WpSpaPod *
wp_spa_pod_builder_end (WpSpaPodBuilder *self)
{
  WpSpaPod *ret = NULL;

  /* Construct the pod */
  ret = g_slice_new0 (WpSpaPod);
  g_ref_count_init (&ret->ref);
  ret->type = WP_SPA_POD_REGULAR;
  ret->pod = spa_pod_builder_pop (&self->builder, &self->frame);
  ret->builder = wp_spa_pod_builder_ref (self);

  /* Also copy the property table if it is an object type */
  if (ret->builder->type == SPA_TYPE_Object)
    ret->static_pod.data_property.table = ret->builder->prop_table;

  return ret;
}

/**
 * wp_spa_pod_parser_ref:
 * @self: a spa pod sparser object
 *
 * Returns: (transfer full): @self with an additional reference count on it
 */
WpSpaPodParser *
wp_spa_pod_parser_ref (WpSpaPodParser *self)
{
  return (WpSpaPodParser *) g_rc_box_acquire ((gpointer) self);
}

static void
wp_spa_pod_parser_free (WpSpaPodParser *self)
{
  self->pod = NULL;
}

/**
 * wp_spa_pod_parser_unref:
 * @self: (transfer full): a spa pod parser object
 *
 * Decreases the reference count on @self and frees it when the ref count
 * reaches zero.
 */
void
wp_spa_pod_parser_unref (WpSpaPodParser *self)
{
  g_rc_box_release_full (self, (GDestroyNotify) wp_spa_pod_parser_free);
}

static WpSpaPodParser *
wp_spa_pod_parser_new (WpSpaPod *pod, guint32 type)
{
  WpSpaPodParser *self = g_rc_box_new0 (WpSpaPodParser);
  self->type = type;
  self->pod = pod;
  spa_pod_parser_pod (&self->parser, self->pod->pod);
  return self;
}

/**
 * wp_spa_pod_parser_new:
 * @pod: the object spa pod to parse
 * @type_name: the type name of the object type
 * @id_name: the Id name of the object
 *
 * Creates an object spa pod parser. The @pod object must be valid for the
 * entire life-cycle of the returned parser.
 *
 * Returns: (transfer full): The new spa pod parser
 */
WpSpaPodParser *
wp_spa_pod_parser_new_object (WpSpaPod *pod, const char *type_name,
    const char **id_name)
{
  WpSpaPodParser *self = NULL;
  guint32 type = 0;
  guint32 id = 0;

  g_return_val_if_fail (wp_spa_pod_is_object (pod), NULL);

  self = wp_spa_pod_parser_new (pod, SPA_TYPE_Object);
  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_BASIC, type_name, &type,
      NULL, &self->prop_table))
    g_return_val_if_reached (NULL);
  spa_pod_parser_push_object (&self->parser, &self->frame, type, &id);
  if (!wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_PARAM, id, NULL, id_name, NULL))
    g_return_val_if_reached (NULL);
  return self;
}

/**
 * wp_spa_pod_parser_new_struct:
 * @pod: the struct spa pod to parse
 *
 * Creates an struct spa pod parser. The @pod object must be valid for the
 * entire life-cycle of the returned parser.
 *
 * Returns: (transfer full): The new spa pod parser
 */
WpSpaPodParser *
wp_spa_pod_parser_new_struct (WpSpaPod *pod)
{
  WpSpaPodParser *self = NULL;

  g_return_val_if_fail (wp_spa_pod_is_struct (pod), NULL);

  self = wp_spa_pod_parser_new (pod, SPA_TYPE_Struct);
  spa_pod_parser_push_struct (&self->parser, &self->frame);
  return self;
}

/**
 * wp_spa_pod_parser_get_boolean:
 * @self: the spa pod parser object
 * @value: (out): the boolean value
 *
 * Gets the boolean value from a spa pod parser
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_boolean (WpSpaPodParser *self, gboolean *value)
{
  g_return_val_if_fail (value, FALSE);
  bool v = FALSE;
  gboolean res = spa_pod_parser_get_bool (&self->parser, &v) >= 0;
  *value = v ? TRUE : FALSE;
  return res;
}

/**
 * wp_spa_pod_parser_get_id:
 * @self: the spa pod parser object
 * @value: (out): the Id value
 *
 * Gets the Id value from a spa pod parser object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_id (WpSpaPodParser *self, guint32 *value)
{
  g_return_val_if_fail (value, FALSE);
  return spa_pod_parser_get_id (&self->parser, value) >= 0;
}

/**
 * wp_spa_pod_parser_get_int:
 * @self: the spa pod parser object
 * @value: (out): the int value
 *
 * Gets the int value from a spa pod parser object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_int (WpSpaPodParser *self, gint *value)
{
  g_return_val_if_fail (value, FALSE);
  return spa_pod_parser_get_int (&self->parser, value) >= 0;
}

/**
 * wp_spa_pod_parser_get_long:
 * @self: the spa pod parser object
 * @value: (out): the long value
 *
 * Gets the long value from a spa pod parser object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_long (WpSpaPodParser *self, glong *value)
{
  g_return_val_if_fail (value, FALSE);
  return spa_pod_parser_get_long (&self->parser, value) >= 0;
}

/**
 * wp_spa_pod_parser_get_float:
 * @self: the spa pod parser object
 * @value: (out): the float value
 *
 * Gets the float value from a spa pod parser object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_float (WpSpaPodParser *self, float *value)
{
  g_return_val_if_fail (value, FALSE);
  return spa_pod_parser_get_float (&self->parser, value) >= 0;
}

/**
 * wp_spa_pod_parser_get_double:
 * @self: the spa pod parser object
 * @value: (out): the double value
 *
 * Gets the double value from a spa pod parser object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_double (WpSpaPodParser *self, double *value)
{
  g_return_val_if_fail (value, FALSE);
  return spa_pod_parser_get_double (&self->parser, value) >= 0;
}

/**
 * wp_spa_pod_parser_get_string:
 * @self: the spa pod parser object
 * @value: (out): the string value
 *
 * Gets the string value from a spa pod parser object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_string (WpSpaPodParser *self, const char **value)
{
  g_return_val_if_fail (value, FALSE);
  return spa_pod_parser_get_string (&self->parser, value) >= 0;
}

/**
 * wp_spa_pod_parser_get_bytes:
 * @self: the spa pod parser object
 * @value: (out): the bytes value
 * @len: (out): the length of the bytes value
 *
 * Gets the bytes value and its length from a spa pod parser object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_bytes (WpSpaPodParser *self, gconstpointer *value,
    guint32 *len)
{
  return spa_pod_parser_get_bytes (&self->parser, value, len) >= 0;
}

/**
 * wp_spa_pod_parser_get_pointer:
 * @self: the spa pod parser object
 * @type_name: (out): the type name of the pointer value
 * @value: (out): the pointer value
 *
 * Gets the pointer value and its type name from a spa pod parser object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_pointer (WpSpaPodParser *self, const char **type_name,
  gconstpointer *value)
{
  guint32 type = 0;
  gboolean res;

  g_return_val_if_fail (value, FALSE);

  res = spa_pod_parser_get_pointer (&self->parser, &type, value) >= 0;

  if (!wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_BASIC, type, NULL, type_name,
      NULL))
    g_return_val_if_reached (FALSE);
  return res;
}

/**
 * wp_spa_pod_parser_get_fd:
 * @self: the spa pod parser object
 * @value: (out): the Fd value
 *
 * Gets the Fd value from a spa pod parser object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_fd (WpSpaPodParser *self, gint64 *value)
{
  g_return_val_if_fail (value, FALSE);
  return spa_pod_parser_get_fd (&self->parser, value) >= 0;
}

/**
 * wp_spa_pod_parser_get_rectangle:
 * @self: the spa pod parser object
 * @width: (out): the rectangle's width value
 * @height: (out): the rectangle's height value
 *
 * Gets the rectangle's width and height value from a spa pod parser object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_rectangle (WpSpaPodParser *self, guint32 *width,
    guint32 *height)
{
  struct spa_rectangle r = { 0, };
  gboolean res = spa_pod_parser_get_rectangle (&self->parser, &r) >= 0;
  if (width)
    *width = r.width;
  if (height)
    *height = r.height;
  return res;
}

/**
 * wp_spa_pod_parser_get_fraction:
 * @self: the spa pod parser object
 * @num: (out): the fractions's numerator value
 * @denom: (out): the fractions's denominator value
 *
 * Gets the fractions's numerator and denominator value from a spa pod parser
 * object
 *
 * Returns: TRUE if the value was obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_fraction (WpSpaPodParser *self, guint32 *num,
    guint32 *denom)
{
  struct spa_fraction f = { 0, };
  gboolean res = spa_pod_parser_get_fraction (&self->parser, &f) >= 0;
  if (num)
    *num = f.num;
  if (denom)
    *denom = f.denom;
  return res;
}

/**
 * wp_spa_pod_parser_get_pod:
 * @self: the spa pod parser object
 *
 * Gets the spa pod value from a spa pod parser object
 *
 * Returns: (transfer full): The spa pod value or NULL if it could not be
 * obtained
 */
WpSpaPod *
wp_spa_pod_parser_get_pod (WpSpaPodParser *self)
{
  struct spa_pod *p = NULL;
  gboolean res = spa_pod_parser_get_pod (&self->parser, &p) >= 0;
  if (!res || !p)
    return NULL;

  return wp_spa_pod_new_wrap (p);
}

/**
 * wp_spa_pod_parser_get:
 * @self: the spa pod parser object
 * @...: (out): a list of values to get, followed by %NULL
 *
 * Gets a list of values from a spa pod parser object
 *
 * Returns: TRUE if the values were obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get (WpSpaPodParser *self, ...)
{
  gboolean res;
  va_list args;

  va_start (args, self);
  res = wp_spa_pod_parser_get_valist (self, args);
  va_end (args);

  return res;
}

/**
 * wp_spa_pod_parser_get_valist:
 * @self: the spa pod parser object
 * @args: the variable arguments passed to wp_spa_pod_parser_get()
 *
 * This is the `va_list` version of wp_spa_pod_parser_get()
 *
 * Returns: TRUE if the values were obtained, FALSE otherwise
 */
gboolean
wp_spa_pod_parser_get_valist (WpSpaPodParser *self, va_list args)
{
  const struct spa_pod_prop *prop = NULL;

  do {
    bool optional;
    const struct spa_pod *pod = NULL;
    const char *format;

    if (self->type == SPA_TYPE_Object) {
      guint32 key = 0;
      const struct spa_pod_object *object;
      const char *key_name = va_arg(args, const char *);
      if (!key_name)
        break;
      if (!wp_spa_type_get_by_nick (self->prop_table, key_name, &key, NULL, NULL))
        g_return_val_if_reached (FALSE);
      if (key == 0)
        break;

      object = (const struct spa_pod_object *)spa_pod_parser_frame (
          &self->parser, &self->frame);
      prop = spa_pod_object_find_prop (object, prop, key);
      pod = prop ? &prop->value : NULL;
    }

    if ((format = va_arg(args, char *)) == NULL)
      break;

    if (self->type == SPA_TYPE_Struct)
      pod = spa_pod_parser_next (&self->parser);

    if ((optional = (*format == '?')))
      format++;

    if (!spa_pod_parser_can_collect (pod, *format)) {
      if (!optional)
        return FALSE;

      SPA_POD_PARSER_SKIP (*format, args);
    } else {
      if (pod->type == SPA_TYPE_Choice && *format != 'V' &&
          SPA_POD_CHOICE_TYPE(pod) == SPA_CHOICE_None)
        pod = SPA_POD_CHOICE_CHILD(pod);

      switch (*format) {
      case 'P':  /* Pod */
      case 'V':  /* Choice */
      case 'O':  /* Object */
      case 'T':  /* Struct */
        *va_arg(args, WpSpaPod**) = wp_spa_pod_new_wrap_copy (pod);
        break;
      default:
        SPA_POD_PARSER_COLLECT (pod, *format, args);
        break;
      }
    }
  } while (TRUE);

  return TRUE;
}

/**
 * wp_spa_pod_parser_end:
 * @self: the spa pod parser object
 *
 * Ends the parser process
 */
void
wp_spa_pod_parser_end (WpSpaPodParser *self)
{
  spa_pod_parser_pop (&self->parser, &self->frame);
}


struct _WpSpaPodIterator
{
  WpSpaPod *pod;
  union {
    gpointer value;                   /* Array and Choice */
    struct spa_pod *pod;              /* Struct */
    struct spa_pod_prop *prop;        /* Object */
    struct spa_pod_control *control;  /* Sequence */
  } curr;
};
typedef struct _WpSpaPodIterator WpSpaPodIterator;

static gboolean
wp_spa_pod_iterator_next_choice (WpSpaPodIterator *self, GValue *item)
{
  const struct spa_pod_choice *pod_choice =
      (const struct spa_pod_choice *) self->pod->pod;

  if (!self->curr.value)
    self->curr.value = SPA_MEMBER (&pod_choice->body, sizeof(struct spa_pod_choice_body), void);
  else
    self->curr.value = SPA_MEMBER (self->curr.value, pod_choice->body.child.size, void);

  if (self->curr.value >= SPA_MEMBER(&pod_choice->body, SPA_POD_BODY_SIZE (pod_choice), void))
    return FALSE;

  if (item) {
    g_value_init (item, G_TYPE_POINTER);
    g_value_set_pointer (item, self->curr.value);
  }
  return TRUE;
}

static gboolean
wp_spa_pod_iterator_next_array (WpSpaPodIterator *self, GValue *item)
{
  const struct spa_pod_array *pod_arr =
      (const struct spa_pod_array *) self->pod->pod;

  if (!self->curr.value)
    self->curr.value = SPA_MEMBER (&pod_arr->body, sizeof(struct spa_pod_array_body), void);
  else
    self->curr.value = SPA_MEMBER (self->curr.value, pod_arr->body.child.size, void);

  if (self->curr.value >= SPA_MEMBER(&pod_arr->body, SPA_POD_BODY_SIZE (pod_arr), void))
    return FALSE;

  if (item) {
    g_value_init (item, G_TYPE_POINTER);
    g_value_set_pointer (item, self->curr.value);
  }
  return TRUE;
}

static gboolean
wp_spa_pod_iterator_next_object (WpSpaPodIterator *self, GValue *item)
{
  const struct spa_pod_object *pod_obj =
      (const struct spa_pod_object *) self->pod->pod;
  g_autoptr (WpSpaPod) pod = NULL;

  if (!self->curr.prop)
    self->curr.prop = spa_pod_prop_first (&pod_obj->body);
  else
    self->curr.prop = spa_pod_prop_next (self->curr.prop);

  if (!spa_pod_prop_is_inside (&pod_obj->body, SPA_POD_BODY_SIZE (pod_obj),
      self->curr.prop))
    return FALSE;

  if (item) {
    g_value_init (item, WP_TYPE_SPA_POD);
    g_value_take_boxed (item, wp_spa_pod_new_property_wrap (
        self->pod->static_pod.data_property.table, self->curr.prop->key,
        self->curr.prop->flags, &self->curr.prop->value));
  }
  return TRUE;
}

static gboolean
wp_spa_pod_iterator_next_struct (WpSpaPodIterator *self, GValue *item)
{
  if (!self->curr.pod)
    self->curr.pod = SPA_POD_BODY (self->pod->pod);
  else
    self->curr.pod = spa_pod_next (self->curr.pod);

  if (!spa_pod_is_inside (SPA_POD_BODY (self->pod->pod),
      SPA_POD_BODY_SIZE (self->pod->pod), self->curr.pod))
    return FALSE;

  if (item) {
    g_value_init (item, WP_TYPE_SPA_POD);
    g_value_take_boxed (item, wp_spa_pod_new_wrap (self->curr.pod));
  }
  return TRUE;
}

static gboolean
wp_spa_pod_iterator_next_sequence (WpSpaPodIterator *self, GValue *item)
{
  const struct spa_pod_sequence *pod_seq =
      (const struct spa_pod_sequence *) self->pod->pod;
  g_autoptr (WpSpaPod) pod = NULL;

  if (!self->curr.control)
    self->curr.control = spa_pod_control_first (&pod_seq->body);
  else
    self->curr.control = spa_pod_control_next (self->curr.control);

  if (!spa_pod_control_is_inside (&pod_seq->body, SPA_POD_BODY_SIZE (pod_seq),
      self->curr.control))
    return FALSE;

  if (item) {
    g_value_init (item, WP_TYPE_SPA_POD);
    g_value_take_boxed (item, wp_spa_pod_new_control_wrap (
        self->curr.control->offset, self->curr.control->type,
        &self->curr.control->value));
  }
  return TRUE;
}


static void
wp_spa_pod_iterator_reset (WpIterator *iterator)
{
  WpSpaPodIterator *self = wp_iterator_get_user_data (iterator);
  self->curr.value = NULL;
  self->curr.pod = NULL;
  self->curr.prop = NULL;
  self->curr.control = NULL;
}

static gboolean
wp_spa_pod_iterator_next (WpIterator *iterator, GValue *item)
{
  WpSpaPodIterator *self = wp_iterator_get_user_data (iterator);

  switch (self->pod->pod->type) {
  case SPA_TYPE_Choice:
    return wp_spa_pod_iterator_next_choice (self, item);
  case SPA_TYPE_Array:
    return wp_spa_pod_iterator_next_array (self, item);
  case SPA_TYPE_Object:
    return wp_spa_pod_iterator_next_object (self, item);
  case SPA_TYPE_Struct:
    return wp_spa_pod_iterator_next_struct (self, item);
  case SPA_TYPE_Sequence:
    return wp_spa_pod_iterator_next_sequence (self, item);
  default:
    break;
  }

  return FALSE;
}

static gboolean
wp_spa_pod_iterator_fold (WpIterator *iterator, WpIteratorFoldFunc func,
    GValue *ret, gpointer data)
{
  WpSpaPodIterator *self = wp_iterator_get_user_data (iterator);

  wp_iterator_reset (iterator);

  switch (self->pod->pod->type) {
  case SPA_TYPE_Choice:
  {
    const struct spa_pod_choice *pod_choice =
        (const struct spa_pod_choice *) self->pod->pod;
    gpointer p = NULL;
    SPA_POD_CHOICE_FOREACH (pod_choice, p) {
      GValue v = G_VALUE_INIT;
      g_value_init (&v, G_TYPE_POINTER);
      g_value_set_pointer (&v, p);
      const gboolean res = func (&v, ret, data);
      g_value_unset (&v);
      if (!res)
        return FALSE;
    }
    break;
  }
  case SPA_TYPE_Array:
  {
    const struct spa_pod_array *pod_arr =
        (const struct spa_pod_array *) self->pod->pod;
    gpointer p = NULL;
    SPA_POD_ARRAY_FOREACH (pod_arr, p) {
      GValue v = G_VALUE_INIT;
      g_value_init (&v, G_TYPE_POINTER);
      g_value_set_pointer (&v, p);
      const gboolean res = func (&v, ret, data);
      g_value_unset (&v);
      if (!res)
        return FALSE;
    }
    break;
  }
  case SPA_TYPE_Object:
  {
    const struct spa_pod_object *pod_obj =
        (const struct spa_pod_object *) self->pod->pod;
    struct spa_pod_prop *p = NULL;
    SPA_POD_OBJECT_FOREACH (pod_obj, p) {
      GValue v = G_VALUE_INIT;
      g_value_init (&v, WP_TYPE_SPA_POD);
      g_value_take_boxed (&v, wp_spa_pod_new_property_wrap (
          self->pod->static_pod.data_property.table, p->key, p->flags,
          &p->value));
      const gboolean res = func (&v, ret, data);
      g_value_unset (&v);
      if (!res)
        return FALSE;
    }
    break;
  }
  case SPA_TYPE_Struct:
  {
    struct spa_pod *p = NULL;
    SPA_POD_STRUCT_FOREACH (self->pod->pod, p) {
      GValue v = G_VALUE_INIT;
      g_value_init (&v, WP_TYPE_SPA_POD);
      g_value_take_boxed (&v, wp_spa_pod_new_wrap (p));
      const gboolean res = func (&v, ret, data);
      g_value_unset (&v);
      if (!res)
        return FALSE;
    }
    break;
  }
  case SPA_TYPE_Sequence:
  {
    const struct spa_pod_sequence *pod_seq =
        (const struct spa_pod_sequence *) self->pod->pod;
    struct spa_pod_control *p = NULL;
    SPA_POD_SEQUENCE_FOREACH (pod_seq, p) {
      GValue v = G_VALUE_INIT;
      g_value_init (&v, WP_TYPE_SPA_POD);
      g_value_take_boxed (&v, wp_spa_pod_new_control_wrap (p->offset, p->type,
          &p->value));
      const gboolean res = func (&v, ret, data);
      g_value_unset (&v);
      if (!res)
        return FALSE;
    }
    break;
  }
  default:
    return FALSE;
  }

  return TRUE;
}

static void
wp_spa_pod_iterator_finalize (WpIterator *iterator)
{
  WpSpaPodIterator *self = wp_iterator_get_user_data (iterator);
  g_clear_pointer (&self->pod, wp_spa_pod_unref);
}

/**
 * wp_spa_pod_iterate:
 * @pod: a spa pod object
 *
 * Creates a new iterator for a spa pod object.
 *
 * Returns: (transfer full): the new spa pod iterator
 */
WpIterator *
wp_spa_pod_iterate (WpSpaPod *pod)
{
  static const WpIteratorMethods methods = {
    .reset = wp_spa_pod_iterator_reset,
    .next = wp_spa_pod_iterator_next,
    .fold = wp_spa_pod_iterator_fold,
    .foreach = NULL,
    .finalize = wp_spa_pod_iterator_finalize
  };
  WpIterator *it = wp_iterator_new (&methods, sizeof (WpSpaPodIterator));
  WpSpaPodIterator *self = wp_iterator_get_user_data (it);

  self->pod = wp_spa_pod_ref (pod);

  return it;
}
