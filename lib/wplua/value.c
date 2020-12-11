/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "wplua.h"
#include "private.h"

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
    break;
  case G_TYPE_OBJECT:
  case G_TYPE_INTERFACE:
    if (_wplua_isgvalue_userdata (L, idx, G_VALUE_TYPE (v)))
      g_value_set_object (v, wplua_toobject (L, idx));
    break;
  case G_TYPE_ENUM:
    if (lua_type (L, idx) == LUA_TSTRING) {
      GEnumClass *klass = g_type_class_peek (G_VALUE_TYPE (v));
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
    wplua_pushboxed (L, G_VALUE_TYPE (v), g_value_dup_boxed (v));
    break;
  case G_TYPE_OBJECT:
  case G_TYPE_INTERFACE:
    wplua_pushobject (L, g_value_dup_object (v));
    break;
  case G_TYPE_ENUM: {
    GEnumClass *klass = g_type_class_peek (G_VALUE_TYPE (v));
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
  case G_TYPE_VARIANT:
  default:
    /* FIXME implement */
    lua_pushnil (L);
    break;
  }
  return 1;
}
