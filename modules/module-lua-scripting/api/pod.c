/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <wplua/wplua.h>

#include <spa/utils/type.h>

#define WP_LOCAL_LOG_TOPIC log_topic_lua_scripting
WP_LOG_TOPIC_EXTERN (log_topic_lua_scripting)

#define MAX_LUA_TYPES 9

/* Builder */

typedef gboolean (*primitive_lua_add_func) (WpSpaPodBuilder *, WpSpaIdValue,
    lua_State *, int);

struct primitive_lua_type {
  WpSpaType primitive_type;
  primitive_lua_add_func primitive_lua_add_funcs[MAX_LUA_TYPES];
};

static inline gboolean
builder_add_boolean_lua_boolean (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  wp_spa_pod_builder_add_boolean (b, lua_toboolean (L, idx));
  return TRUE;
}

static inline gboolean
builder_add_boolean_lua_number (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  if (lua_isinteger (L, idx)) {
    wp_spa_pod_builder_add_boolean (b, lua_tointeger (L, idx) > 0);
    return TRUE;
  }
  return FALSE;
}

static inline gboolean
builder_add_boolean_lua_string (WpSpaPodBuilder *b, WpSpaIdValue key_id,
   lua_State *L, int idx)
{
  const gchar *value = lua_tostring (L, idx);
  wp_spa_pod_builder_add_boolean (b,
     g_strcmp0 (value, "true") == 0 || g_strcmp0 (value, "1") == 0);
  return TRUE;
}

static inline gboolean
builder_add_id_lua_number (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  if (lua_isinteger (L, idx)) {
    wp_spa_pod_builder_add_id (b, lua_tointeger (L, idx));
    return TRUE;
  }
  return FALSE;
}

static inline gboolean
builder_add_id_lua_string (WpSpaPodBuilder *b, WpSpaIdValue key_id,
   lua_State *L, int idx)
{
  const gchar *value = lua_tostring (L, idx);
  WpSpaIdTable id_table = NULL;
  WpSpaIdValue id_val = NULL;
  if (key_id) {
    wp_spa_id_value_get_value_type (key_id, &id_table);
    if (id_table) {
      id_val = wp_spa_id_table_find_value_from_short_name (id_table, value);
      wp_spa_pod_builder_add_id (b, wp_spa_id_value_number (id_val));
      return TRUE;
    }
  }
  return FALSE;
}

static inline gboolean
builder_add_int_lua_boolean (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  gboolean value = lua_toboolean (L, idx);
  wp_spa_pod_builder_add_int (b, value ? 1 : 0);
  return TRUE;
}

static inline gboolean
builder_add_int_lua_number (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  if (lua_isinteger (L, idx)) {
    wp_spa_pod_builder_add_int (b, lua_tointeger (L, idx));
    return TRUE;
  }
  return FALSE;
}

static inline gboolean
builder_add_int_lua_string (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  const gchar *value = lua_tostring (L, idx);
  wp_spa_pod_builder_add_int (b, strtol (value, NULL, 10));
  return TRUE;
}

static inline gboolean
builder_add_long_lua_boolean (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  gboolean value = lua_toboolean (L, idx);
  wp_spa_pod_builder_add_long (b, value ? 1 : 0);
  return TRUE;
}

static inline gboolean
builder_add_long_lua_number (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  if (lua_isinteger (L, idx)) {
    wp_spa_pod_builder_add_long (b, lua_tointeger (L, idx));
    return TRUE;
  }
  return FALSE;
}

static inline gboolean
builder_add_long_lua_string (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  const gchar *value = lua_tostring (L, idx);
  wp_spa_pod_builder_add_long (b, strtol (value, NULL, 10));
  return TRUE;
}

static inline gboolean
builder_add_float_lua_boolean (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  gboolean value = lua_toboolean (L, idx);
  wp_spa_pod_builder_add_float (b, value ? 1.0f : 0.0f);
  return TRUE;
}

static inline gboolean
builder_add_float_lua_number (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  if (lua_isnumber (L, idx) && !lua_isinteger (L, idx)) {
    wp_spa_pod_builder_add_float (b, lua_tonumber (L, idx));
    return TRUE;
  }
  return FALSE;
}

static inline gboolean
builder_add_double_lua_boolean (WpSpaPodBuilder *b, WpSpaIdValue key_id,
   lua_State *L, int idx)
{
  gboolean value = lua_toboolean (L, idx);
  wp_spa_pod_builder_add_double (b, value ? 1.0f : 0.0f);
  return TRUE;
}

static inline gboolean
builder_add_double_lua_number (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  if (lua_isnumber (L, idx) && !lua_isinteger (L, idx)) {
    wp_spa_pod_builder_add_double (b, lua_tonumber (L, idx));
    return TRUE;
  }
  return FALSE;
}

static inline gboolean
builder_add_string_lua_boolean (WpSpaPodBuilder *b, WpSpaIdValue key_id,
   lua_State *L, int idx)
{
  gboolean value = lua_toboolean (L, idx);
  wp_spa_pod_builder_add_string (b, value ? "true" : "false");
  return TRUE;
}

static inline gboolean
builder_add_string_lua_number (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  g_autofree gchar *value = NULL;
  value = lua_isinteger (L, idx) ?
      g_strdup_printf ("%lld", lua_tointeger (L, idx)) :
      g_strdup_printf ("%f", lua_tonumber (L, idx));
  wp_spa_pod_builder_add_string (b, value);
  return TRUE;
}

static inline gboolean
builder_add_string_lua_string (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  const gchar *value = lua_tostring (L, idx);
  wp_spa_pod_builder_add_string (b, value);
  return TRUE;
}

static inline gboolean
builder_add_bytes_lua_number (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  if (lua_isinteger (L, idx)) {
    gint64 value = lua_tointeger (L, idx);
    wp_spa_pod_builder_add_bytes (b, (gconstpointer)&value, sizeof (value));
  } else {
    double value = lua_tonumber (L, idx);
    wp_spa_pod_builder_add_bytes (b, (gconstpointer)&value, sizeof (value));
  }
  return TRUE;
}

static inline gboolean
builder_add_bytes_lua_string (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  const gchar *value = lua_tostring (L, idx);
  wp_spa_pod_builder_add_bytes (b, (gconstpointer)value, strlen (value));
  return TRUE;
}

static inline gboolean
builder_add_fd_lua_number (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  if (lua_isinteger (L, idx)) {
    wp_spa_pod_builder_add_fd (b, lua_tointeger (L, idx));
    return TRUE;
  }
  return FALSE;
}

static inline gboolean
builder_add_fd_lua_string (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  const gchar *value = lua_tostring (L, idx);
  wp_spa_pod_builder_add_fd (b, strtol (value, NULL, 10));
  return TRUE;
}

static inline gboolean
is_pod_type_compatible (WpSpaType type, WpSpaPod *pod)
{
  /* Check if the pod type matches */
  if (type == wp_spa_pod_get_spa_type (pod))
    return TRUE;

  /* Otherwise, check if the child type of Choice pod matches */
  if (wp_spa_pod_is_choice (pod)) {
    g_autoptr (WpSpaPod) child = wp_spa_pod_get_choice_child (pod);
    WpSpaType child_type = wp_spa_pod_get_spa_type (child);
    if (type == child_type)
      return TRUE;
  }

  return FALSE;
}

static inline gboolean
builder_add_lua_userdata (WpSpaPodBuilder *b, WpSpaIdValue key_id,
    lua_State *L, int idx)
{
  WpSpaPod *pod = wplua_checkboxed (L, idx, WP_TYPE_SPA_POD);
  if (pod) {
    if (key_id) {
      WpSpaType prop_type = wp_spa_id_value_get_value_type (key_id, NULL);
      if (is_pod_type_compatible (prop_type, pod)) {
        wp_spa_pod_builder_add_pod (b, pod);
        return TRUE;
      }
    } else {
      wp_spa_pod_builder_add_pod (b, pod);
      return TRUE;
    }
  }
  return FALSE;
}

static const struct primitive_lua_type primitive_lua_types[] = {
  {SPA_TYPE_Bool, {
    [LUA_TBOOLEAN] = builder_add_boolean_lua_boolean,
    [LUA_TNUMBER] = builder_add_boolean_lua_number,
    [LUA_TSTRING] = builder_add_boolean_lua_string,
  }},
  {SPA_TYPE_Id, {
    [LUA_TNUMBER] = builder_add_id_lua_number,
    [LUA_TSTRING] = builder_add_id_lua_string,
  }},
  {SPA_TYPE_Int, {
    [LUA_TBOOLEAN] = builder_add_int_lua_boolean,
    [LUA_TNUMBER] = builder_add_int_lua_number,
    [LUA_TSTRING] = builder_add_int_lua_string,
  }},
  {SPA_TYPE_Long, {
    [LUA_TBOOLEAN] = builder_add_long_lua_boolean,
    [LUA_TNUMBER] = builder_add_long_lua_number,
    [LUA_TSTRING] = builder_add_long_lua_string,
  }},
  {SPA_TYPE_Float, {
    [LUA_TBOOLEAN] = builder_add_float_lua_boolean,
    [LUA_TNUMBER] = builder_add_float_lua_number,
  }},
  {SPA_TYPE_Double, {
    [LUA_TBOOLEAN] = builder_add_double_lua_boolean,
    [LUA_TNUMBER] = builder_add_double_lua_number,
  }},
  {SPA_TYPE_String, {
    [LUA_TBOOLEAN] = builder_add_string_lua_boolean,
    [LUA_TNUMBER] = builder_add_string_lua_number,
    [LUA_TSTRING] = builder_add_string_lua_string,
  }},
  {SPA_TYPE_Bytes, {
    [LUA_TNUMBER] = builder_add_bytes_lua_number,
    [LUA_TSTRING] = builder_add_bytes_lua_string,
  }},
  {SPA_TYPE_Fd, {
    [LUA_TNUMBER] = builder_add_fd_lua_number,
    [LUA_TSTRING] = builder_add_fd_lua_string,
  }},
  {0, {}},
};

static gboolean
builder_add_key (WpSpaPodBuilder *b, WpSpaIdTable table, lua_State *L, int type)
{
  /* Number */
  if (type == LUA_TNUMBER) {
    wp_spa_pod_builder_add_id (b, lua_tonumber (L, -1));
    return TRUE;
  }

  /* String */
  else if (type == LUA_TSTRING) {
    const gchar *key = lua_tostring (L, -1);
    WpSpaIdValue val = wp_spa_id_table_find_value_from_short_name (table, key);
    if (val) {
      wp_spa_pod_builder_add_id (b, wp_spa_id_value_number (val));
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
builder_add_value (WpSpaPodBuilder *b, WpSpaType array_type, lua_State *L,
    int ltype)
{
  guint i;

  if (ltype < 0 || ltype >= MAX_LUA_TYPES)
    return FALSE;

  for (i = 0; primitive_lua_types[i].primitive_type; i++) {
    const struct primitive_lua_type *t = primitive_lua_types + i;
    if (t->primitive_type == array_type) {
      primitive_lua_add_func f = t->primitive_lua_add_funcs[ltype];
      if (f) {
        gboolean v = f (b, NULL, L, -1);
        if (ltype != lua_type (L, -1))
          g_error ("corrupted Lua stack");
        return v;
      }
    }
  }
  return FALSE;
}

static int
wp_spa_pod_lua_table_members (lua_State *L, int idx)
{
  size_t members = 0;

  lua_pushnil (L);
  while (lua_next (L, idx)) {
    lua_pop (L, 1);
    if (!lua_isinteger (L, -1))
      luaL_error (L, "Tables used to construct POD must have only integer keys");
    members++;
  }
  if ((lua_Integer)members < 0 ||
      (size_t)(lua_Integer)members != members ||
      (lua_Integer)members > INT_MAX)
    luaL_error (L, "too many table members");
  return (int)members;
}

static int
builder_add_table_inner (lua_State *L)
{
  WpSpaType type = WP_SPA_TYPE_INVALID;
  const gchar *type_name = NULL;
  WpSpaIdTable table = NULL;
  WpSpaPodBuilder *builder;
  lua_Integer table_key;
  int members;

  /* stack at entry: args from Lua, light userdata */
  /* Last argument is the pointer pushed by builder_add_table(). */
  builder = lua_touserdata (L, -1);
  lua_pop(L, 1);
  /* stack: args from Lua */

  /* Exactly one argument is expected, and it must be a table. */
  luaL_checktype (L, 1, LUA_TTABLE);
  luaL_checktype (L, 2, LUA_TNONE);
  /* stack: Lua table */
  members = wp_spa_pod_lua_table_members (L, 1);
  if (members == 0)
    return 0;
  if (lua_rawgeti (L, 1, 1) != LUA_TSTRING)
    luaL_error (L, "must have the item type or table on its first field");
  type_name = lua_tostring (L, -1);
  type = wp_spa_type_from_name (type_name);
  switch (type) {
  case WP_SPA_TYPE_INVALID:
    table = wp_spa_id_table_from_name (type_name);
    if (!table)
      luaL_error (L, "Unknown type '%s'", type_name);
    break;
  case SPA_TYPE_Bool:
  case SPA_TYPE_Id:
  case SPA_TYPE_Int:
  case SPA_TYPE_Long:
  case SPA_TYPE_Float:
  case SPA_TYPE_Double:
  case SPA_TYPE_Fd:
    /* no string or bytes */
    break;
  default:
    luaL_error (L, "Unsupported type %" PRIu32 " for array or choice", type);
  }
  lua_pop (L, 1);
  for (table_key = 2; table_key <= (lua_Integer)members; table_key++)  {
    int ltype = lua_rawgeti (L, 1, table_key);
    if (ltype == LUA_TNIL)
      luaL_error (L, "table has %d keys but is missing key %d",
                  members, (int)table_key);
    if (!(table ?
          builder_add_key (builder, table, L, ltype) :
          builder_add_value (builder, type, L, ltype)))
      luaL_error (L, "key could not be added");
    lua_pop (L, 1);
  }
  return 0;
}

static int
builder_add_table (lua_State *L, WpSpaPodBuilder *builder_)
{
  int call_args, status;
  {
    g_autoptr (WpSpaPodBuilder) builder = builder_;
    lua_pushlightuserdata (L, builder);
    call_args = lua_gettop (L);
    lua_pushcfunction (L, builder_add_table_inner);
    lua_insert (L, 1);
    status = lua_pcall (L, call_args, 1, 0);
    if (status == 0)
      wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_builder_end (builder));
  }
  if (status != 0)
    lua_error (L);
  return 1;
}

/* None */

static int
spa_pod_none_new (lua_State *L)
{
  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_new_none ());
  return 1;
}

/* Boolean */

static int
spa_pod_boolean_new (lua_State *L)
{
  gboolean value = lua_toboolean (L, 1);
  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_new_boolean (value));
  return 1;
}

/* Id */

static int
spa_pod_id_new (lua_State *L)
{
  /* Id number */
  if (lua_type (L, 1) == LUA_TNUMBER) {
    gint64 value = lua_tointeger (L, 1);
    wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_new_id (value));
    return 1;
  }

  /* Table name and key name */
  else if (lua_type (L, 1) == LUA_TSTRING) {
    const gchar *table_name = lua_tostring (L, 1);
    const gchar *key_name = lua_tostring (L, 2);
    WpSpaIdTable table = NULL;
    WpSpaIdValue value = NULL;
    table = wp_spa_id_table_from_name (table_name);
    if (!table)
      luaL_error (L, "table '%s' does not exist", table_name);
    value = wp_spa_id_table_find_value_from_short_name (table, key_name);
    if (!value)
      luaL_error (L, "key '%s' does not exist in '%s'", key_name, table_name);
    wplua_pushboxed (L, WP_TYPE_SPA_POD,
        wp_spa_pod_new_id (wp_spa_id_value_number (value)));
    return 1;
  }

  /* Error */
  luaL_error (L, "Invalid parameters");
  return 0;
}

/* Int */

static int
spa_pod_int_new (lua_State *L)
{
  gint64 value = lua_tointeger (L, 1);
  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_new_int (value));
  return 1;
}

/* Long */

static int
spa_pod_long_new (lua_State *L)
{
  gint64 value = lua_tointeger (L, 1);
  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_new_long (value));
  return 1;
}

/* Float */

static int
spa_pod_float_new (lua_State *L)
{
  float value = lua_tonumber (L, 1);
  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_new_float (value));
  return 1;
}

/* Double */

static int
spa_pod_double_new (lua_State *L)
{
  double value = lua_tonumber (L, 1);
  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_new_double (value));
  return 1;
}

/* String */

static int
spa_pod_string_new (lua_State *L)
{
  const gchar *value = lua_tostring (L, 1);
  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_new_string (value));
  return 1;
}

/* Bytes */

static int
spa_pod_bytes_new (lua_State *L)
{
  switch (lua_type (L, 1)) {
    case LUA_TNUMBER: {
      if (lua_isinteger (L, 1)) {
        guint64 value = lua_tointeger (L, 1);
        wplua_pushboxed (L, WP_TYPE_SPA_POD,
            wp_spa_pod_new_bytes (&value, sizeof (guint64)));
      } else {
        double value = lua_tonumber (L, 1);
        wplua_pushboxed (L, WP_TYPE_SPA_POD,
            wp_spa_pod_new_bytes (&value, sizeof (double)));
      }
      return 1;
    }
    case LUA_TSTRING: {
      const gchar *str = lua_tostring (L, 1);
      wplua_pushboxed (L, WP_TYPE_SPA_POD,
          wp_spa_pod_new_bytes (str, strlen (str)));
      return 1;
    }
    default:
      luaL_error (L, "Only number and strings are valid for bytes pod");
      break;
  }
  return 0;
}

/* Pointer */

static int
spa_pod_pointer_new (lua_State *L)
{
  const gchar *type = lua_tostring (L, 1);
  gconstpointer value = lua_touserdata (L, 2);
  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_new_pointer (type, value));
  return 1;
}

/* Fd */

static int
spa_pod_fd_new (lua_State *L)
{
  gint64 value = lua_tointeger (L, 1);
  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_new_fd (value));
  return 1;
}

/* Rectangle */

static int
spa_pod_rectangle_new (lua_State *L)
{
  gint64 width = lua_tointeger (L, 1);
  gint64 height = lua_tointeger (L, 2);
  wplua_pushboxed (L, WP_TYPE_SPA_POD,
      wp_spa_pod_new_rectangle (width, height));
  return 1;
}

/* Fraction */

static int
spa_pod_fraction_new (lua_State *L)
{
  gint64 num = lua_tointeger (L, 1);
  gint64 denom = lua_tointeger (L, 2);
  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_new_fraction (num, denom));
  return 1;
}


/* Object */

static gboolean
object_add_property (WpSpaPodBuilder *b, WpSpaIdTable table,
    const gchar *key, lua_State *L, int idx)
{
  guint i;
  WpSpaIdValue prop_id = NULL;
  int ltype = lua_type (L, idx);

  if (ltype < 0 || ltype >= MAX_LUA_TYPES)
    return FALSE;

  /* Check if we can add primitive property directly from LUA type */
  prop_id = wp_spa_id_table_find_value_from_short_name (table, key);
  if (prop_id) {
    WpSpaType prop_type = wp_spa_id_value_get_value_type (prop_id, NULL);
    if (prop_type != WP_SPA_TYPE_INVALID) {
      for (i = 0; primitive_lua_types[i].primitive_type; i++) {
        const struct primitive_lua_type *t = primitive_lua_types + i;
        if (t->primitive_type == prop_type) {
          primitive_lua_add_func f = t->primitive_lua_add_funcs[ltype];
          if (f) {
            wp_spa_pod_builder_add_property (b, key);
            return f (b, prop_id, L, idx);
          }
        }
      }
    }
  }

  /* Otherwise just add pod property */
  if (lua_type (L, idx) == LUA_TUSERDATA) {
    wp_spa_pod_builder_add_property (b, key);
    return builder_add_lua_userdata (b, prop_id, L, idx);
  }

  return FALSE;
}

static int
spa_pod_object_new (lua_State *L)
{
  g_autoptr (WpSpaPodBuilder) builder = NULL;
  const gchar *fields[2] = { NULL, NULL };  // type_name, name_id
  WpSpaType object_type = WP_SPA_TYPE_INVALID;
  WpSpaIdTable table = NULL;

  luaL_checktype (L, 1, LUA_TTABLE);

  lua_geti (L, 1, 1);
  fields[0] = lua_tostring (L, -1);
  lua_geti (L, 1, 2);
  fields[1] = lua_tostring (L, -1);

  object_type = wp_spa_type_from_name (fields[0]);
  if (object_type == WP_SPA_TYPE_INVALID)
    luaL_error (L, "Invalid object type '%s'", fields[0]);

  table = wp_spa_type_get_values_table (object_type);
  if (!table)
    luaL_error (L, "Object type '%s' has incomplete type information",
        fields[0]);

  builder = wp_spa_pod_builder_new_object (fields[0], fields[1]);
  if (!builder)
    luaL_error (L, "Could not create pod object");

  lua_pop (L, 2);

  lua_pushnil(L);
  while (lua_next (L, -2)) {
    /* Remaining fields with string keys are the object properties */
    if (lua_type (L, -2) == LUA_TSTRING) {
      const gchar *key = lua_tostring (L, -2);
      if (!object_add_property (builder, table, key, L, -1))
        luaL_error (L, "Property '%s' could not be added", key);
    }

    lua_pop (L, 1);
  }

  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_builder_end (builder));
  return 1;
}

/* Struct */

static int
spa_pod_struct_new (lua_State *L)
{
  g_autoptr (WpSpaPodBuilder) builder = NULL;

  luaL_checktype (L, 1, LUA_TTABLE);

  builder = wp_spa_pod_builder_new_struct ();

  lua_pushnil (L);
  while (lua_next (L, 1)) {
    switch (lua_type (L, -1)) {
      case LUA_TBOOLEAN:
        wp_spa_pod_builder_add_boolean (builder, lua_toboolean (L, -1));
        break;
      case LUA_TNUMBER:
        if (lua_isinteger (L, -1))
          wp_spa_pod_builder_add_long (builder, lua_tointeger (L, -1));
        else
          wp_spa_pod_builder_add_double (builder, lua_tonumber (L, -1));
        break;
      case LUA_TSTRING:
        wp_spa_pod_builder_add_string (builder, lua_tostring (L, -1));
        break;
      case LUA_TUSERDATA: {
        WpSpaPod *pod = wplua_checkboxed (L, -1, WP_TYPE_SPA_POD);
        wp_spa_pod_builder_add_pod (builder, pod);
        break;
      }
      default:
        luaL_error (L, "Struct does not support lua type ",
            lua_typename(L, lua_type(L, -1)));
        break;
    }
    lua_pop (L, 1);
  }

  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_builder_end (builder));
  return 1;
}

/* Sequence */

static int
spa_pod_sequence_new (lua_State *L)
{
  g_autoptr (WpSpaPodBuilder) builder = NULL;

  luaL_checktype (L, 1, LUA_TTABLE);

  builder = wp_spa_pod_builder_new_sequence (0);

  lua_pushnil (L);
  while (lua_next (L, -2)) {
    guint32 offset = 0;
    const gchar *type_name = NULL;
    WpSpaPod *value = NULL;

    /* Read Control */
    if (lua_istable(L, -1)) {
      lua_pushnil (L);
      while (lua_next (L, -2)) {
        const gchar *key = lua_tostring (L, -2);
        if (g_strcmp0 (key, "offset") == 0) {
          offset = lua_tointeger (L, -1);
        } else if (!type_name && g_strcmp0 (key, "typename") == 0) {
          type_name = lua_tostring (L, -1);
        } else if (!value && g_strcmp0 (key, "value") == 0) {
          switch (lua_type (L, -1)) {
	    case LUA_TBOOLEAN:
	      value = wp_spa_pod_new_boolean (lua_toboolean (L, -1));
              break;
	    case LUA_TNUMBER:
              if (lua_isinteger (L, -1))
	        value = wp_spa_pod_new_long (lua_tointeger (L, -1));
	      else
                value = wp_spa_pod_new_double (lua_tonumber (L, -1));
	      break;
	    case LUA_TSTRING:
	      value = wp_spa_pod_new_string (lua_tostring (L, -1));
	      break;
	    case LUA_TUSERDATA: {
              value = wplua_checkboxed (L, -1, WP_TYPE_SPA_POD);
	      break;
	    }
	    default: {
              luaL_error (L, "Control value does not support lua type ",
	          lua_typename(L, lua_type(L, -1)));
	      value = NULL;
	      break;
	    }
	  }
        }
        lua_pop(L, 1);
      }
    }

    /* Add control */
    if (type_name && value) {
      wp_spa_pod_builder_add_control (builder, offset, type_name);
      wp_spa_pod_builder_add_pod (builder, value);
      wp_spa_pod_unref (value);
    }

    lua_pop(L, 1);
  }

  wplua_pushboxed (L, WP_TYPE_SPA_POD, wp_spa_pod_builder_end (builder));
  return 1;
}

/* Array */

static int
spa_pod_array_new (lua_State *L)
{
  WpSpaPodBuilder *builder = wp_spa_pod_builder_new_array ();
  return builder_add_table (L, builder);
}

/* Choice */

static int
spa_pod_choice_none_new (lua_State *L)
{
  WpSpaPodBuilder *builder = wp_spa_pod_builder_new_choice ("None");
  return builder_add_table (L, builder);
}

static int
spa_pod_choice_range_new (lua_State *L)
{
  WpSpaPodBuilder *builder = wp_spa_pod_builder_new_choice ("Range");
  return builder_add_table (L, builder);
}

static int
spa_pod_choice_step_new (lua_State *L)
{
  WpSpaPodBuilder *builder = wp_spa_pod_builder_new_choice ("Step");
  return builder_add_table (L, builder);
}

static int
spa_pod_choice_enum_new (lua_State *L)
{
  WpSpaPodBuilder *builder = wp_spa_pod_builder_new_choice ("Enum");
  /* TODO: check bool enums */
  return builder_add_table (L, builder);
}

static int
spa_pod_choice_flags_new (lua_State *L)
{
  WpSpaPodBuilder *builder = wp_spa_pod_builder_new_choice ("Flags");
  return builder_add_table (L, builder);
}

/* API */

static int
spa_pod_get_type_name (lua_State *L)
{
  WpSpaPod *pod = wplua_checkboxed (L, 1, WP_TYPE_SPA_POD);
  lua_pushstring (L, wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
  return 1;
}

static void
push_primitive_values (lua_State *L, WpSpaPod *pod, WpSpaType type,
    guint start_index, WpSpaIdTable idtable)
{
  g_auto (GValue) item = G_VALUE_INIT;
  g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (pod);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    gpointer p = g_value_get_pointer (&item);
    if (!p)
      continue;
    switch (type) {
    case SPA_TYPE_Bool:
      lua_pushboolean (L, *(gboolean *)p);
      break;
    case SPA_TYPE_Id: {
      WpSpaIdValue idval = NULL;
      if (idtable)
        idval = wp_spa_id_table_find_value (idtable, *(guint32 *)p);
      if (idval)
        lua_pushstring (L, wp_spa_id_value_short_name (idval));
      else
        lua_pushinteger (L, *(guint32 *)p);
      break;
    }
    case SPA_TYPE_Int:
      lua_pushinteger (L, *(gint *)p);
      break;
    case SPA_TYPE_Long:
      lua_pushinteger (L, *(long *)p);
      break;
    case SPA_TYPE_Float:
      lua_pushnumber (L, *(float *)p);
      break;
    case SPA_TYPE_Double:
      lua_pushnumber (L, *(double *)p);
      break;
    case SPA_TYPE_Fd:
      lua_pushnumber (L, *(gint64 *)p);
      break;
    default:
      continue;
    }
    lua_rawseti (L, -2, start_index++);
  }
}

static void
push_luapod (lua_State *L, WpSpaPod *pod, WpSpaIdValue field_idval)
{
  /* None */
  if (wp_spa_pod_is_none (pod)) {
    lua_pushnil (L);
  }

  /* Boolean */
  else if (wp_spa_pod_is_boolean (pod)) {
    gboolean value = FALSE;
    g_warn_if_fail (wp_spa_pod_get_boolean (pod, &value));
    lua_pushboolean (L, value);
  }

  /* Id */
  else if (wp_spa_pod_is_id (pod)) {
    guint32 value = 0;
    WpSpaIdTable idtable = NULL;
    WpSpaIdValue idval = NULL;
    g_warn_if_fail (wp_spa_pod_get_id (pod, &value));
    if (field_idval && SPA_TYPE_Id ==
            wp_spa_id_value_get_value_type (field_idval, &idtable)) {
      idval = wp_spa_id_table_find_value (idtable, value);
    }
    if (idval)
      lua_pushstring (L, wp_spa_id_value_short_name (idval));
    else
      lua_pushinteger (L, value);
  }

  /* Int */
  else if (wp_spa_pod_is_int (pod)) {
    gint value = 0;
    g_warn_if_fail (wp_spa_pod_get_int (pod, &value));
    lua_pushinteger (L, value);
  }

  /* Long */
  else if (wp_spa_pod_is_long (pod)) {
    gint64 value = 0;
    wp_spa_pod_get_long (pod, &value);
    lua_pushinteger (L, value);
  }

  /* Float */
  else if (wp_spa_pod_is_float (pod)) {
    float value = 0;
    g_warn_if_fail (wp_spa_pod_get_float (pod, &value));
    lua_pushnumber (L, value);
  }

  /* Double */
  else if (wp_spa_pod_is_double (pod)) {
    double value = 0;
    g_warn_if_fail (wp_spa_pod_get_double (pod, &value));
    lua_pushnumber (L, value);
  }

  /* String */
  else if (wp_spa_pod_is_string (pod)) {
    const gchar *value = NULL;
    g_warn_if_fail (wp_spa_pod_get_string (pod, &value));
    lua_pushstring (L, value);
  }

  /* Bytes */
  else if (wp_spa_pod_is_bytes (pod)) {
    gconstpointer value = NULL;
    guint32 size = 0;
    g_warn_if_fail (wp_spa_pod_get_bytes (pod, &value, &size));
    char str[size + 1];
    for (guint i = 0; i < size; i++)
      str[i] = ((const gchar *)value)[i];
    str[size] = '\0';
    lua_pushstring (L, str);
  }

  /* Pointer */
  else if (wp_spa_pod_is_pointer (pod)) {
    gconstpointer value = NULL;
    g_warn_if_fail (wp_spa_pod_get_pointer (pod, &value));
    if (!value)
      lua_pushnil (L);
    else
      lua_pushlightuserdata (L, (gpointer)value);
  }

  /* Fd */
  else if (wp_spa_pod_is_fd (pod)) {
    gint64 value = 0;
    g_warn_if_fail (wp_spa_pod_get_fd (pod, &value));
    lua_pushinteger (L, value);
  }

  /* Rectangle */
  else if (wp_spa_pod_is_rectangle (pod)) {
    guint32 width = 0, height = 0;
    g_warn_if_fail (wp_spa_pod_get_rectangle (pod, &width, &height));
    lua_newtable (L);
    lua_pushstring (L, "Rectangle");
    lua_setfield (L, -2, "pod_type");
    lua_pushinteger (L, width);
    lua_setfield (L, -2, "width");
    lua_pushinteger (L, height);
    lua_setfield (L, -2, "height");
  }

  /* Fraction */
  else if (wp_spa_pod_is_fraction (pod)) {
    guint32 num = 0, denom = 0;
    g_warn_if_fail (wp_spa_pod_get_fraction (pod, &num, &denom));
    lua_newtable (L);
    lua_pushstring (L, "Fraction");
    lua_setfield (L, -2, "pod_type");
    lua_pushinteger (L, num);
    lua_setfield (L, -2, "num");
    lua_pushinteger (L, denom);
    lua_setfield (L, -2, "denom");
  }

  /* Object */
  else if (wp_spa_pod_is_object (pod)) {
    WpSpaType type = wp_spa_pod_get_spa_type (pod);
    WpSpaIdTable values_table = wp_spa_type_get_values_table (type);
    const gchar *id_name = NULL;
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpIterator) it = NULL;
    g_warn_if_fail (wp_spa_pod_get_object (pod, &id_name, NULL));
    lua_newtable (L);
    lua_pushstring (L, "Object");
    lua_setfield (L, -2, "pod_type");
    lua_pushstring (L, id_name);
    lua_setfield (L, -2, "object_id");
    it = wp_spa_pod_new_iterator (pod);
    lua_newtable (L);
    for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
      WpSpaPod *prop = g_value_get_boxed (&item);
      const gchar *key = NULL;
      g_autoptr (WpSpaPod) val = NULL;
      //FIXME: this is suboptimal because _get_property() converts
      // the key to a short name and we convert it back
      g_warn_if_fail (wp_spa_pod_get_property (prop, &key, &val));
      if (key) {
        push_luapod (L, val,
            wp_spa_id_table_find_value_from_short_name (values_table, key));
        lua_setfield (L, -2, key);
      }
    }
    lua_setfield (L, -2, "properties");
  }

  /* Struct */
  else if (wp_spa_pod_is_struct (pod)) {
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (pod);
    guint i = 1;
    lua_newtable (L);
    lua_pushstring (L, "Struct");
    lua_setfield (L, -2, "pod_type");
    for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
      WpSpaPod *val = g_value_get_boxed (&item);
      push_luapod (L, val, NULL);
      lua_rawseti (L, -2, i++);
    }
  }

  /* Sequence */
  else if (wp_spa_pod_is_sequence (pod)) {
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (pod);
    guint i = 1;
    lua_newtable (L);
    lua_pushstring (L, "Sequence");
    lua_setfield (L, -2, "pod_type");
    for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
      WpSpaPod *control = g_value_get_boxed (&item);
      guint32 offset = 0;
      const char *type_name = NULL;
      g_autoptr (WpSpaPod) val = NULL;
      g_warn_if_fail (wp_spa_pod_get_control (control, &offset, &type_name,
          &val));
      lua_newtable (L);
      lua_pushinteger (L, offset);
      lua_setfield (L, -2, "offset");
      lua_pushstring (L, type_name);
      lua_setfield (L, -2, "typename");
      push_luapod (L, val, NULL);
      lua_setfield (L, -2, "value");
      lua_rawseti (L, -2, i++);
    }
  }

  /* Array */
  else if (wp_spa_pod_is_array (pod)) {
    g_autoptr (WpSpaPod) child = wp_spa_pod_get_array_child (pod);
    WpSpaType type = wp_spa_pod_get_spa_type (child);
    WpSpaIdTable idtable = NULL;
    if (field_idval && type == SPA_TYPE_Id && SPA_TYPE_Array ==
            wp_spa_id_value_get_value_type (field_idval, &idtable))
        wp_spa_id_value_array_get_item_type (field_idval, &idtable);
    lua_newtable (L);
    lua_pushstring (L, "Array");
    lua_setfield (L, -2, "pod_type");
    lua_pushstring (L, wp_spa_type_name (type));
    lua_setfield (L, -2, "value_type");
    push_primitive_values (L, pod, type, 1, idtable);
  }

  /* Choice */
  else if (wp_spa_pod_is_choice (pod)) {
    g_autoptr (WpSpaPod) child = wp_spa_pod_get_choice_child (pod);
    WpSpaType type = wp_spa_pod_get_spa_type (child);
    g_autofree const gchar *choice_type = g_strdup_printf ("Choice.%s",
        wp_spa_id_value_short_name (wp_spa_pod_get_choice_type (pod)));
    WpSpaIdTable idtable = NULL;
    if (field_idval && type == SPA_TYPE_Id)
      wp_spa_id_value_get_value_type (field_idval, &idtable);
    lua_newtable (L);
    lua_pushstring (L, choice_type);
    lua_setfield (L, -2, "pod_type");
    lua_pushstring (L, wp_spa_type_name (type));
    lua_setfield (L, -2, "value_type");
    push_primitive_values (L, pod, type, 1, idtable);
  }

  /* Error */
  else {
    luaL_error (L, "Unsupported pod type ",
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
  }
}

static int
spa_pod_parse (lua_State *L)
{
  WpSpaPod *pod = wplua_checkboxed (L, 1, WP_TYPE_SPA_POD);
  push_luapod (L, pod, NULL);
  return 1;
}

static int
spa_pod_fixate (lua_State *L)
{
  WpSpaPod *pod = wplua_checkboxed (L, 1, WP_TYPE_SPA_POD);
  gboolean res = wp_spa_pod_fixate (pod);
  lua_pushboolean (L, res);
  return 1;
}

static inline WpSpaPod *
_checkpod (lua_State *L, int n)
{
  return wplua_checkboxed (L, n, WP_TYPE_SPA_POD);
}

static int
spa_pod_filter (lua_State *L)
{
  WpSpaPod *pod = wplua_checkboxed (L, 1, WP_TYPE_SPA_POD);
  WpSpaPod *filter = luaL_opt (L, _checkpod, 2, NULL);
  WpSpaPod *result = wp_spa_pod_filter (pod, filter);
  if (result) {
    wplua_pushboxed (L, WP_TYPE_SPA_POD, result);
    return 1;
  }
  return 0;
}

static const luaL_Reg spa_pod_methods[] = {
  { "get_type_name", spa_pod_get_type_name },
  { "parse", spa_pod_parse },
  { "fixate", spa_pod_fixate },
  { "filter", spa_pod_filter },
  { NULL, NULL }
};

static const luaL_Reg spa_pod_constructors[] = {
  { "None", spa_pod_none_new },
  { "Boolean", spa_pod_boolean_new },
  { "Id", spa_pod_id_new },
  { "Int", spa_pod_int_new },
  { "Long", spa_pod_long_new },
  { "Float", spa_pod_float_new },
  { "Double", spa_pod_double_new },
  { "String", spa_pod_string_new },
  { "Bytes", spa_pod_bytes_new },
  { "Pointer", spa_pod_pointer_new },
  { "Fd", spa_pod_fd_new },
  { "Rectangle", spa_pod_rectangle_new },
  { "Fraction", spa_pod_fraction_new },
  { "Object", spa_pod_object_new },
  { "Struct", spa_pod_struct_new },
  { "Sequence", spa_pod_sequence_new },
  { "Array", spa_pod_array_new },
  { NULL, NULL }
};

static const luaL_Reg spa_pod_choice_constructors[] = {
  { "None", spa_pod_choice_none_new },
  { "Range", spa_pod_choice_range_new },
  { "Step", spa_pod_choice_step_new },
  { "Enum", spa_pod_choice_enum_new },
  { "Flags", spa_pod_choice_flags_new },
  { NULL, NULL }
};

/* Init */

void
wp_lua_scripting_pod_init (lua_State *L)
{
  luaL_newlib (L, spa_pod_constructors);
  luaL_newlib (L, spa_pod_choice_constructors);
  lua_setfield (L, -2, "Choice");
  lua_setglobal (L, "WpSpaPod");

  wplua_register_type_methods (L, WP_TYPE_SPA_POD, NULL, spa_pod_methods);
}
