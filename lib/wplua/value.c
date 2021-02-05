/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "wplua.h"
#include "private.h"
#include <wp/wp.h>

WpProperties *
wplua_table_to_properties (lua_State *L, int idx)
{
  WpProperties *p = wp_properties_new_empty ();
  const gchar *key, *value;
  int table = lua_absindex (L, idx);

  lua_pushnil(L);
  while (lua_next (L, table) != 0) {
    /* copy key & value to convert them to string */
    lua_pushvalue (L, -2);
    key = lua_tostring (L, -1);
    lua_pushvalue (L, -2);
    value = lua_tostring (L, -1);
    wp_properties_set (p, key, value);
    lua_pop (L, 3);
  }
  return p;
}

void
wplua_properties_to_table (lua_State *L, WpProperties *p)
{
  lua_newtable (L);
  if (p) {
    g_autoptr (WpIterator) it = wp_properties_new_iterator (p);
    GValue v = G_VALUE_INIT;
    const gchar *key, *value;

    while (wp_iterator_next (it, &v)) {
      key = wp_properties_iterator_item_get_key (&v);
      value = wp_properties_iterator_item_get_value (&v);
      lua_pushstring (L, key);
      lua_pushstring (L, value);
      lua_settable (L, -3);
      g_value_unset (&v);
    }
  }
}

GVariant *
wplua_table_to_asv (lua_State *L, int idx)
{
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
  int table = lua_absindex (L, idx);

  lua_pushnil (L);
  while (lua_next (L, table)) {
    /* each argument must have a string as key */
    if (lua_type (L, -2) != LUA_TSTRING) {
      wp_warning ("skipping non-string key");
      lua_pop (L, 1);
      continue; /* skip, it's probably harmless */
    }

    const char *key = lua_tostring (L, -2);

    switch (lua_type (L, -1)) {
    case LUA_TBOOLEAN:
      g_variant_builder_add (&b, "{sv}", key,
          g_variant_new_boolean (lua_toboolean (L, -1)));
      break;

    case LUA_TNUMBER:
      if (lua_isinteger (L, -1)) {
        g_variant_builder_add (&b, "{sv}", key,
            g_variant_new_int64 (lua_tointeger (L, -1)));
      } else {
        g_variant_builder_add (&b, "{sv}", key,
            g_variant_new_double (lua_tonumber (L, -1)));
      }
      break;

    case LUA_TSTRING:
      g_variant_builder_add (&b, "{sv}", key,
          g_variant_new_string (lua_tostring (L, -1)));
      break;

    case LUA_TTABLE:
      g_variant_builder_add (&b, "{sv}", key, wplua_table_to_asv (L, -1));
      break;

    default:
      wp_warning ("skipping bad value (its type cannot be represented in GVariant)");
      break;
    }

    lua_pop (L, 1);
  }

  return g_variant_builder_end (&b);
}

void
wplua_asv_to_table (lua_State *L, GVariant *asv)
{
  lua_newtable (L);
  if (asv) {
    GVariantIter iter;
    g_variant_iter_init (&iter, asv);
    const gchar *key;
    GVariant *value;

    while (g_variant_iter_loop (&iter, "{&sv}", &key, &value)) {
      lua_pushstring (L, key);

      if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN)) {
        lua_pushboolean (L, g_variant_get_boolean (value));
      }
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT16)) {
        lua_pushinteger (L, g_variant_get_int16 (value));
      }
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT32)) {
        lua_pushinteger (L, g_variant_get_int32 (value));
      }
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT64)) {
        lua_pushinteger (L, g_variant_get_int64 (value));
      }
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_DOUBLE)) {
        lua_pushnumber (L, g_variant_get_double (value));
      }
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING)) {
        lua_pushstring (L, g_variant_get_string (value, NULL));
      }
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_VARDICT)) {
        wplua_asv_to_table (L, value);
      }
      else {
        wp_warning ("skipping bad value (its type cannot be represented in lua)");
        lua_pop (L, 1);
        continue;
      }

      lua_settable (L, -3);
    }
  }
}

void
wplua_lua_to_gvalue (lua_State *L, int idx, GValue *v)
{
  switch (g_type_fundamental (G_VALUE_TYPE (v))) {
  case G_TYPE_CHAR:
    if (lua_type (L, idx) == LUA_TSTRING)
      g_value_set_schar (v, *lua_tostring (L, idx));
    else
      g_value_set_schar (v, lua_tointeger (L, idx));
    break;
  case G_TYPE_UCHAR:
    g_value_set_uchar (v, lua_tointeger (L, idx));
    break;
  case G_TYPE_INT:
    g_value_set_int (v, lua_tointeger (L, idx));
    break;
  case G_TYPE_UINT:
    g_value_set_uint (v, lua_tointeger (L, idx));
    break;
  case G_TYPE_LONG:
    g_value_set_long (v, lua_tointeger (L, idx));
    break;
  case G_TYPE_ULONG:
    g_value_set_ulong (v, lua_tointeger (L, idx));
    break;
  case G_TYPE_INT64:
    g_value_set_int64 (v, lua_tointeger (L, idx));
    break;
  case G_TYPE_UINT64:
    g_value_set_uint64 (v, lua_tonumber (L, idx));
    break;
  case G_TYPE_FLOAT:
    g_value_set_float (v, lua_tonumber (L, idx));
    break;
  case G_TYPE_DOUBLE:
    g_value_set_double (v, lua_tonumber (L, idx));
    break;
  case G_TYPE_BOOLEAN:
    g_value_set_boolean (v, lua_toboolean (L, idx));
    break;
  case G_TYPE_STRING:
    g_value_set_string (v, lua_tostring (L, idx));
    break;
  case G_TYPE_POINTER:
    if (lua_type (L, idx) == LUA_TLIGHTUSERDATA)
      g_value_set_pointer (v, lua_touserdata (L, idx));
    break;
  case G_TYPE_BOXED:
    if (_wplua_isgvalue_userdata (L, idx, G_VALUE_TYPE (v)))
      g_value_set_boxed (v, wplua_toboxed (L, idx));
    /* table -> WpProperties */
    else if (lua_istable (L, idx) && G_VALUE_TYPE (v) == WP_TYPE_PROPERTIES)
      g_value_take_boxed (v, wplua_table_to_properties (L, idx));
    break;
  case G_TYPE_OBJECT:
  case G_TYPE_INTERFACE:
    if (_wplua_isgvalue_userdata (L, idx, G_VALUE_TYPE (v)))
      g_value_set_object (v, wplua_toobject (L, idx));
    break;
  case G_TYPE_ENUM:
    if (lua_type (L, idx) == LUA_TSTRING) {
      g_autoptr (GEnumClass) klass = g_type_class_ref (G_VALUE_TYPE (v));
      GEnumValue *value = g_enum_get_value_by_nick (klass, lua_tostring (L, idx));
      if (value)
        g_value_set_enum (v, value->value);
    } else {
      g_value_set_enum (v, lua_tointeger (L, idx));
    }
    break;
  case G_TYPE_FLAGS:
    g_value_set_flags (v, lua_tointeger (L, idx));
    break;
  case G_TYPE_VARIANT:
    if (lua_istable (L, idx))
      g_value_set_variant (v, wplua_table_to_asv (L, idx));
    break;
  default:
    break;
  }
}

int
wplua_gvalue_to_lua (lua_State *L, const GValue *v)
{
  switch (g_type_fundamental (G_VALUE_TYPE (v))) {
  case G_TYPE_CHAR:
    lua_pushinteger (L, g_value_get_schar (v));
    break;
  case G_TYPE_UCHAR:
    lua_pushinteger (L, g_value_get_uchar (v));
    break;
  case G_TYPE_INT:
    lua_pushinteger (L, g_value_get_int (v));
    break;
  case G_TYPE_UINT:
    lua_pushinteger (L, g_value_get_uint (v));
    break;
  case G_TYPE_LONG:
    lua_pushinteger (L, g_value_get_long (v));
    break;
  case G_TYPE_ULONG:
    lua_pushinteger (L, g_value_get_ulong (v));
    break;
  case G_TYPE_INT64:
    lua_pushinteger (L, g_value_get_int64 (v));
    break;
  case G_TYPE_UINT64:
    lua_pushnumber (L, g_value_get_uint64 (v));
    break;
  case G_TYPE_FLOAT:
    lua_pushnumber (L, g_value_get_float (v));
    break;
  case G_TYPE_DOUBLE:
    lua_pushnumber (L, g_value_get_double (v));
    break;
  case G_TYPE_BOOLEAN:
    lua_pushboolean (L, g_value_get_boolean (v));
    break;
  case G_TYPE_STRING:
    lua_pushstring (L, g_value_get_string (v));
    break;
  case G_TYPE_POINTER:
    lua_pushlightuserdata (L, g_value_get_pointer (v));
    break;
  case G_TYPE_BOXED:
    if (G_VALUE_TYPE (v) == WP_TYPE_PROPERTIES)
      wplua_properties_to_table (L, g_value_get_boxed (v));
    else
      wplua_pushboxed (L, G_VALUE_TYPE (v), g_value_dup_boxed (v));
    break;
  case G_TYPE_OBJECT:
  case G_TYPE_INTERFACE:
    wplua_pushobject (L, g_value_dup_object (v));
    break;
  case G_TYPE_ENUM: {
    g_autoptr (GEnumClass) klass = g_type_class_ref (G_VALUE_TYPE (v));
    GEnumValue *value = g_enum_get_value (klass, g_value_get_enum (v));
    if (value)
      lua_pushstring (L, value->value_nick);
    else
      lua_pushinteger (L, g_value_get_enum (v));
    break;
  }
  case G_TYPE_FLAGS:
    /* FIXME: push as userdata with methods */
    lua_pushinteger (L, g_value_get_flags (v));
    break;
  case G_TYPE_PARAM: {
    GParamSpec *pspec = g_value_get_param (v);
    lua_pushstring (L, pspec->name);
    break;
  }
  case G_TYPE_VARIANT: {
    GVariant *asv = g_value_get_variant (v);
    if (g_variant_is_of_type (asv, G_VARIANT_TYPE_VARDICT))
      wplua_asv_to_table (L, asv);
    else
      /* FIXME maybe implement if needed */
      lua_pushnil (L);
    break;
  }
  default:
    lua_pushnil (L);
    break;
  }
  return 1;
}
