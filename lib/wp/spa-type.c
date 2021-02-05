/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: spa-type
 * @title: Spa Type Information
 *
 * Spa has a type system that is represented by a set of arrays that contain
 * `spa_type_info` structures. This type system is simple, yet complex to
 * work with for a couple of reasons.
 *
 * WirePlumber uses this API to access the spa type system, which makes some
 * things easier to understand and work with. The main benefit of using this
 * API is that it makes it easy to work with string representations of the
 * types, allowing easier access from script bindings.
 *
 * ### Type hierarchy
 *
 * On the top level, there is a list of types like Int, Bool, String, Id, Object.
 * These are called fundamental types (terms borrowed from #GType).
 * Fundamental types can be derived and therefore we can have other types
 * that represent specific enums or objects, for instance.
 *
 * Enum and flag types are all derived directly from `SPA_TYPE_Id`. These types
 * may have a list of possible values that one can select from (enums)
 * or combine (flags). These values are accessed with the #WpSpaIdValue API.
 *
 * Object types can have fields. All objects always have a special "id" field,
 * whose type can be given by wp_spa_object_type_get_id_type() and optionally,
 * they can also have other object-specific fields, which are also accessed
 * with the #WpSpaIdValue API.
 */

#define G_LOG_DOMAIN "wp-spa-type"

#include "spa-type.h"

#include <spa/utils/type-info.h>
#include <spa/debug/types.h>
#include <pipewire/pipewire.h>

static const WpSpaType SPA_TYPE_VENDOR_WirePlumber = 0x03000000;
static GArray *extra_types = NULL;
static GArray *extra_id_tables = NULL;

typedef struct {
  const char *name;
  const struct spa_type_info *values;
} WpSpaIdTableInfo;

static const WpSpaIdTableInfo static_id_tables[] = {
  { SPA_TYPE_INFO_Choice, spa_type_choice },
  { SPA_TYPE_INFO_Direction, spa_type_direction },
  { SPA_TYPE_INFO_ParamId, spa_type_param },
  { SPA_TYPE_INFO_MediaType, spa_type_media_type },
  { SPA_TYPE_INFO_MediaSubtype, spa_type_media_subtype },
  { SPA_TYPE_INFO_ParamAvailability, spa_type_param_availability },
  { SPA_TYPE_INFO_ParamPortConfigMode, spa_type_param_port_config_mode },
  { SPA_TYPE_INFO_VideoFormat, spa_type_video_format },
  { SPA_TYPE_INFO_AudioFormat, spa_type_audio_format },
  { SPA_TYPE_INFO_AudioFlags, spa_type_audio_flags },
  { SPA_TYPE_INFO_AudioChannel, spa_type_audio_channel },
  { SPA_TYPE_INFO_IO, spa_type_io },
  { SPA_TYPE_INFO_Control, spa_type_control },
  { SPA_TYPE_INFO_Data, spa_type_data_type },
  { SPA_TYPE_INFO_Meta, spa_type_meta_type },
  { SPA_TYPE_INFO_DeviceEventId, spa_type_device_event_id },
  { SPA_TYPE_INFO_NodeEvent, spa_type_node_event_id },
  { SPA_TYPE_INFO_NodeCommand, spa_type_node_command_id },
  { NULL, NULL }
};

/**
 * WpSpaType:
 */
GType wp_spa_type_get_type (void)
{
  static volatile gsize id__volatile = 0;
  if (g_once_init_enter (&id__volatile)) {
    GType id = g_type_register_static_simple (
        G_TYPE_UINT, g_intern_static_string ("WpSpaType"),
        0, NULL, 0, NULL, 0);
    g_once_init_leave (&id__volatile, id);
  }
  return id__volatile;
}

/**
 * WpSpaIdTable:
 */
G_DEFINE_POINTER_TYPE (WpSpaIdTable, wp_spa_id_table)

/**
 * WpSpaIdValue:
 */
G_DEFINE_POINTER_TYPE (WpSpaIdValue, wp_spa_id_value)


static const struct spa_type_info *
wp_spa_type_info_find_by_type (WpSpaType type)
{
  const struct spa_type_info *info;

  g_return_val_if_fail (type != WP_SPA_TYPE_INVALID, NULL);
  g_return_val_if_fail (type != 0, NULL);

  if (extra_types)
    info = spa_debug_type_find (
        (const struct spa_type_info *) extra_types->data, type);
  else
    info = spa_debug_type_find (SPA_TYPE_ROOT, type);

  return info;
}

/* similar to spa_debug_type_find() and unlike spa_debug_type_find_type(),
   which steps into id values / object fields */
static const struct spa_type_info *
_spa_type_find_by_name (const struct spa_type_info * info, const char * name)
{
  const struct spa_type_info * res;

  while (info->name) {
    if (info->type == SPA_ID_INVALID) {
      if (info->values && (res = _spa_type_find_by_name (info->values, name)))
        return res;
    }
    if (strcmp (info->name, name) == 0)
      return info;
    info++;
  }
  return NULL;
}

static const struct spa_type_info *
wp_spa_type_info_find_by_name (const gchar *name)
{
  const struct spa_type_info *info = NULL;

  g_return_val_if_fail (name != NULL, NULL);

  if (extra_types)
    info = _spa_type_find_by_name (
        (const struct spa_type_info *) extra_types->data, name);
  else
    info = _spa_type_find_by_name (SPA_TYPE_ROOT, name);

  return info;
}

/**
 * wp_spa_type_from_name:
 * @name: the name to look up
 *
 * Looks up the type id from a given type name
 *
 * Returns: the corresponding type id or %WP_SPA_TYPE_INVALID if not found
 */
WpSpaType
wp_spa_type_from_name (const gchar *name)
{
  const struct spa_type_info *info = wp_spa_type_info_find_by_name (name);
  return info ? info->type : WP_SPA_TYPE_INVALID;
}

/**
 * wp_spa_type_parent:
 * @type: a type id
 *
 * Returns: the direct parent type of the given @type; if the type is
 *   fundamental (i.e. has no parent), the returned type is the same as @type
 */
WpSpaType
wp_spa_type_parent (WpSpaType type)
{
  const struct spa_type_info *info = wp_spa_type_info_find_by_type (type);
  return info ? info->parent : WP_SPA_TYPE_INVALID;
}

/**
 * wp_spa_type_name:
 * @type: a type id
 *
 * Returns: the complete name of the given @type or %NULL if @type is invalid
 */
const gchar *
wp_spa_type_name (WpSpaType type)
{
  const struct spa_type_info *info = wp_spa_type_info_find_by_type (type);
  return info ? info->name : NULL;
}

/**
 * wp_spa_type_is_fundamental:
 * @type: a type id
 *
 * Returns: %TRUE if the @type has no parent, %FALSE otherwise
 */
gboolean
wp_spa_type_is_fundamental (WpSpaType type)
{
  const struct spa_type_info *info = wp_spa_type_info_find_by_type (type);
  return info ? (info->type == info->parent) : FALSE;
}

/**
 * wp_spa_type_is_id:
 * @type: a type id
 *
 * Returns: %TRUE if the @type is a SPA_TYPE_Id, %FALSE otherwise
 */
gboolean
wp_spa_type_is_id (WpSpaType type)
{
  const struct spa_type_info *info = wp_spa_type_info_find_by_type (type);
  return info ? (info->parent == SPA_TYPE_Id) : FALSE;
}

/**
 * wp_spa_type_is_object:
 * @type: a type id
 *
 * Returns: %TRUE if the @type is a SPA_TYPE_Object, %FALSE otherwise
 */
gboolean
wp_spa_type_is_object (WpSpaType type)
{
  const struct spa_type_info *info = wp_spa_type_info_find_by_type (type);
  return info ? (info->parent == SPA_TYPE_Object) : FALSE;
}

/**
 * wp_spa_type_get_object_id_values_table:
 * @type: the type id of an object type
 *
 * Object pods (see #WpSpaPod) always have a special "id" field along with
 * other fields that can be defined. This "id" field can only store values
 * of a specific `SPA_TYPE_Id` type. This function returns the table that
 * contains the possible values for that field.
 *
 * Returns: the table with the values that can be stored in the special "id"
 *   field of an object of the given @type
 */
WpSpaIdTable
wp_spa_type_get_object_id_values_table (WpSpaType type)
{
  const struct spa_type_info *info = wp_spa_type_info_find_by_type (type);

  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->parent == SPA_TYPE_Object, NULL);
  g_return_val_if_fail (info->values != NULL, NULL);
  g_return_val_if_fail (info->values->name != NULL, NULL);
  g_return_val_if_fail (info->values->parent == SPA_TYPE_Id, NULL);

  return info->values->values;
}

/**
 * wp_spa_type_get_values_table:
 * @type: a type id
 *
 * Returns: the associated #WpSpaIdTable that contains possible
 *   values or object fields for this type, or %NULL
 */
WpSpaIdTable
wp_spa_type_get_values_table (WpSpaType type)
{
  const struct spa_type_info *info = wp_spa_type_info_find_by_type (type);

  g_return_val_if_fail (info != NULL, NULL);
  return info->values;
}


struct spa_type_info_iterator_data
{
  const struct spa_type_info *base;
  const struct spa_type_info *cur;
};

static void
spa_type_info_iterator_reset (WpIterator *it)
{
  struct spa_type_info_iterator_data *it_data = wp_iterator_get_user_data (it);
  it_data->cur = it_data->base;
}

static gboolean
spa_type_info_iterator_next (WpIterator *it, GValue *item)
{
  struct spa_type_info_iterator_data *it_data = wp_iterator_get_user_data (it);

  if (it_data->cur->name) {
    g_value_init (item, WP_TYPE_SPA_ID_VALUE);
    g_value_set_pointer (item, (gpointer) it_data->cur);
    it_data->cur++;
    return TRUE;
  }
  return FALSE;
}

static gboolean
spa_type_info_iterator_fold (WpIterator *it, WpIteratorFoldFunc func,
    GValue *ret, gpointer data)
{
  struct spa_type_info_iterator_data *it_data = wp_iterator_get_user_data (it);
  const struct spa_type_info *cur, *base;

  cur = base = it_data->base;

  while (cur->name) {
    g_auto (GValue) item = G_VALUE_INIT;
    g_value_init (&item, WP_TYPE_SPA_ID_VALUE);
    g_value_set_pointer (&item, (gpointer) cur);
    if (!func (&item, ret, data))
      return FALSE;
    cur++;
  }
  return TRUE;
}

static const WpIteratorMethods spa_type_info_iterator_methods = {
  .version = WP_ITERATOR_METHODS_VERSION,
  .reset = spa_type_info_iterator_reset,
  .next = spa_type_info_iterator_next,
  .fold = spa_type_info_iterator_fold,
};

WpSpaIdTable
wp_spa_id_table_from_name (const gchar *name)
{
  g_return_val_if_fail (name != NULL, NULL);
  const WpSpaIdTableInfo *info = NULL;

  /* first look in dynamic id tables */
  if (extra_id_tables) {
    info = (const WpSpaIdTableInfo *) extra_id_tables->data;
    while (info && info->name) {
      if (strcmp (info->name, name) == 0)
        return info->values;
      info++;
    }
  }

  /* then look at the well-known static ones */
  info = static_id_tables;
  while (info && info->name) {
    if (strcmp (info->name, name) == 0)
      return info->values;
    info++;
  }

  /* then look into types, hoping to find an object type */
  const struct spa_type_info *tinfo = wp_spa_type_info_find_by_name (name);
  return tinfo ? tinfo->values : NULL;
}

/**
 * wp_spa_id_table_new_iterator:
 * @type: the id table
 *
 * This function returns an iterator that allows you to iterate through the
 * values associated with this table.
 * The items in the iterator are of type #WpSpaIdValue.
 *
 * Returns: a #WpIterator that iterates over #WpSpaIdValue items
 */
WpIterator *
wp_spa_id_table_new_iterator (WpSpaIdTable table)
{
  g_return_val_if_fail (table != NULL, NULL);

  WpIterator *it = wp_iterator_new (&spa_type_info_iterator_methods,
      sizeof (struct spa_type_info_iterator_data));
  struct spa_type_info_iterator_data *it_data = wp_iterator_get_user_data (it);
  it_data->base = (const struct spa_type_info *) table;
  it_data->cur = it_data->base;
  return it;
}

WpSpaIdValue
wp_spa_id_table_find_value (WpSpaIdTable table, guint value)
{
  g_return_val_if_fail (table != NULL, NULL);

  const struct spa_type_info *info = table;
  while (info && info->name) {
    if (info->type == value)
      return info;
    info++;
  }
  return NULL;
}

WpSpaIdValue
wp_spa_id_table_find_value_from_name (WpSpaIdTable table, const gchar * name)
{
  g_return_val_if_fail (table != NULL, NULL);

  const struct spa_type_info *info = table;
  while (info && info->name) {
    if (!strcmp (info->name, name))
      return info;
    info++;
  }
  return NULL;
}

WpSpaIdValue
wp_spa_id_table_find_value_from_short_name (WpSpaIdTable table,
    const gchar * short_name)
{
  g_return_val_if_fail (table != NULL, NULL);

  const struct spa_type_info *info = table;
  while (info && info->name) {
    if (!strcmp (spa_debug_type_short_name (info->name), short_name))
      return info;
    info++;
  }
  return NULL;
}


static WpSpaIdTable
wp_spa_id_name_find_id_table (const gchar * name)
{
  WpSpaIdTable table = NULL;
  g_autofree gchar *parent_name = g_strdup (name);
  gchar *h;

  if ((h = strrchr(parent_name, ':')) != NULL) {
    /* chop the enum name to get the type, ex:
       Spa:Enum:Direction:Input -> Spa:Enum:Direction */
    *h = '\0';
    table = wp_spa_id_table_from_name (parent_name);

    /* in some cases, the parent name is one layer further up, ex:
       Spa:Pod:Object:Param:Format:Audio:rate -> Spa:Pod:Object:Param:Format */
    if (!table && (h = strrchr(parent_name, ':')) != NULL) {
      *h = '\0';
      table = wp_spa_id_table_from_name (parent_name);
    }
  }
  return table;
}

/**
 * wp_spa_id_value_from_name:
 * @name: the full name of an id value
 *
 * Looks up an id value (enum, flag or object field) directly from its full
 * name. For instance, "Spa:Enum:Direction:Input" will resolve to the
 * id value that represents "Input" in the "Spa:Enum:Direction" enum.
 *
 * Returns: the id value for @name, or %NULL if no such id value was found
 */
WpSpaIdValue
wp_spa_id_value_from_name (const gchar * name)
{
  g_return_val_if_fail (name != NULL, NULL);

  WpSpaIdTable table = wp_spa_id_name_find_id_table (name);
  return wp_spa_id_table_find_value_from_name (table, name);
}

/**
 * wp_spa_id_value_from_short_name:
 * @table_name: the name of the #WpSpaIdTable to look up the value in
 * @short_name: the short name of the value to look up
 *
 * Looks up an id value given its container @table_name and its @short_name
 *
 * Returns: the id value or %NULL if it was not found
 */
WpSpaIdValue
wp_spa_id_value_from_short_name (const gchar * table_name,
    const gchar * short_name)
{
  g_return_val_if_fail (table_name != NULL, NULL);
  g_return_val_if_fail (short_name != NULL, NULL);

  WpSpaIdTable table = wp_spa_id_table_from_name (table_name);
  return wp_spa_id_table_find_value_from_short_name (table, short_name);
}

/**
 * wp_spa_id_value_from_number:
 * @table_name: the name of the #WpSpaIdTable to look up the value in
 * @id: the numeric representation of the value to look up
 *
 * Looks up an id value given its container @table_name and its numeric
 * representation, @id
 *
 * Returns: the id value or %NULL if it was not found
 */
WpSpaIdValue
wp_spa_id_value_from_number (const gchar * table_name, guint id)
{
  g_return_val_if_fail (table_name != NULL, NULL);

  WpSpaIdTable table = wp_spa_id_table_from_name (table_name);
  return wp_spa_id_table_find_value (table, id);
}

/**
 * wp_spa_id_value_number:
 * @id: an id value
 *
 * Returns: the numeric representation of this id value
 */
guint
wp_spa_id_value_number (WpSpaIdValue id)
{
  g_return_val_if_fail (id != NULL, -1);

  const struct spa_type_info *info = id;
  return info->type;
}

/**
 * wp_spa_id_value_name:
 * @id: an id value
 *
 * Returns: the full name of this id value
 */
const gchar *
wp_spa_id_value_name (WpSpaIdValue id)
{
  g_return_val_if_fail (id != NULL, NULL);

  const struct spa_type_info *info = id;
  return info->name;
}

/**
 * wp_spa_id_value_short_name:
 * @id: an id value
 *
 * Returns: the short name of this id value
 */
const gchar *
wp_spa_id_value_short_name (WpSpaIdValue id)
{
  g_return_val_if_fail (id != NULL, NULL);

  const struct spa_type_info *info = id;
  return spa_debug_type_short_name (info->name);
}

/**
 * wp_spa_id_value_get_value_type
 * @id: an id value
 * @table: (out) (optional): the associated #WpSpaIdTable
 *
 * Returns the value type associated with this #WpSpaIdValue. This information
 * is useful when @id represents an object field, which can take a value
 * of an arbitrary type.
 *
 * When the returned type is (or is derived from) `SPA_TYPE_Id` or
 * `SPA_TYPE_Object`, @table is set to point to the #WpSpaIdTable that contains
 * the possible Id values / object fields.
 *
 * Returns: the value type associated with @id
 */
WpSpaType
wp_spa_id_value_get_value_type (WpSpaIdValue id, WpSpaIdTable * table)
{
  g_return_val_if_fail (id != NULL, WP_SPA_TYPE_INVALID);

  const struct spa_type_info *info = id;

  if (table) {
    /* info->values has different semantics on Array types */
    if (info->values && info->parent != SPA_TYPE_Array) {
      *table = info->values;
    }
    /* derived object types normally don't have info->values directly set,
       so we need to look them up */
    else if (wp_spa_type_is_object (info->parent)) {
      WpSpaIdTable t = wp_spa_type_get_values_table (info->parent);
      if (t) *table = t;
    }
  }

  return info->parent;
}

/**
 * wp_spa_id_value_array_get_item_type:
 * @id: an id value
 * @table: (out) (optional): the associated #WpSpaIdTable
 *
 * If the value type of @id is `SPA_TYPE_Array`, this function returns the
 * type that is allowed to be contained inside the array.
 *
 * When the returned type is (or is derived from) `SPA_TYPE_Id` or
 * `SPA_TYPE_Object`, @table is set to point to the #WpSpaIdTable that contains
 * the possible Id values / object fields.
 *
 * Returns: the type that is allowed in the array, if @id represents
 *   an object field that takes an array as value
 */
WpSpaType
wp_spa_id_value_array_get_item_type (WpSpaIdValue id, WpSpaIdTable * table)
{
  g_return_val_if_fail (id != NULL, WP_SPA_TYPE_INVALID);

  const struct spa_type_info *info = id;
  g_return_val_if_fail (info->parent == SPA_TYPE_Array, WP_SPA_TYPE_INVALID);

  return info->values ?
      wp_spa_id_value_get_value_type (info->values, table) :
      WP_SPA_TYPE_INVALID;
}


/**
 * wp_spa_dynamic_type_init:
 *
 * Initializes the spa dynamic type registry.
 * This allows registering new spa types at runtime. The spa type system
 * still works if this function is not called.
 *
 * Normally called by wp_init() when %WP_INIT_SPA_TYPES is passed in its flags.
 */
void
wp_spa_dynamic_type_init (void)
{
  extra_types = g_array_new (TRUE, FALSE, sizeof (struct spa_type_info));
  extra_id_tables = g_array_new (TRUE, FALSE, sizeof (WpSpaIdTableInfo));

  /* init to chain up to spa types */
  struct spa_type_info info = {
      SPA_ID_INVALID, SPA_ID_INVALID, "spa_types", SPA_TYPE_ROOT
  };
  g_array_append_val (extra_types, info);
}

/**
 * wp_spa_dynamic_type_deinit:
 *
 * Deinitializes the spa type registry.
 * You do not need to ever call this, unless you want to free memory at the
 * end of the execution of a test, so that it doesn't show as leaked in
 * the memory profiler.
 */
void
wp_spa_dynamic_type_deinit (void)
{
  g_clear_pointer (&extra_types, g_array_unref);
  g_clear_pointer (&extra_id_tables, g_array_unref);
}

/**
 * wp_spa_dynamic_type_register:
 * @name: the name of the type
 * @parent: the parent type
 * @values: an array of `spa_type_info` that contains the values of the type,
 *   used only for Object types
 *
 * Registers an additional type in the spa type system.
 * This is useful to add a custom pod object type.
 *
 * Note that both @name and @values must be statically allocated, or
 * otherwise guaranteed to be kept in memory until wp_spa_dynamic_type_deinit()
 * is called. No memory copy is done by this function.
 *
 * Returns: the new type
 */
WpSpaType
wp_spa_dynamic_type_register (const gchar *name, WpSpaType parent,
    const struct spa_type_info * values)
{
  struct spa_type_info info;
  info.type = SPA_TYPE_VENDOR_WirePlumber + extra_types->len;
  info.name = name;
  info.parent = parent;
  info.values = values;
  g_array_append_val (extra_types, info);
  return info.type;
}

/**
 * wp_spa_dynamic_id_table_register:
 * @name: the name of the id table
 * @values: an array of `spa_type_info` that contains the values of the table
 *
 * Registers an additional #WpSpaIdTable in the spa type system.
 * This is useful to add custom enumeration types.
 *
 * Note that both @name and @values must be statically allocated, or
 * otherwise guaranteed to be kept in memory until wp_spa_dynamic_type_deinit()
 * is called. No memory copy is done by this function.
 *
 * Returns: the new table
 */
WpSpaIdTable
wp_spa_dynamic_id_table_register (const gchar *name,
    const struct spa_type_info * values)
{
  WpSpaIdTableInfo info;
  info.name = name;
  info.values = values;
  g_array_append_val (extra_id_tables, info);
  return values;
}
