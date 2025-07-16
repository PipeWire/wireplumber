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

static const char *
_wplua_get_metatable_name (GType type)
{
  if (g_type_is_a (type, G_TYPE_BOXED))
    return "GBoxed";
  else if (g_type_is_a (type, G_TYPE_OBJECT))
    return "GObject";
  else
    return NULL;
}

GValue *
_wplua_pushgvalue_userdata (lua_State * L, GType type)
{
  GValue *v;
  const char *table_name = _wplua_get_metatable_name (type);

  if (table_name == NULL)
    g_error ("type passed to %s not boxed or object", __func__);

  /* auxillary library can use 4 stack slots, plus 1 for userdata */
  if (!lua_checkstack (L, 5))
    g_error ("cannot grow Lua stack in %s", __func__);
  v = lua_newuserdata (L, sizeof (GValue));
  memset (v, 0, sizeof (GValue));
  g_value_init (v, type);
  luaL_getmetatable (L, table_name);
  lua_setmetatable (L, -2);
  return v;
}

GValue *
_wplua_togvalue_userdata_named (lua_State *L, int idx, GType type,
                                const char *table_name)
{
  GValue *v;

  /* auxillary library can use 4 stack slots */
  if (!lua_checkstack (L, 4))
    g_error ("cannot grow Lua stack in %s", __func__);
  if (!(v = luaL_testudata (L, idx, table_name)))
    return NULL;
  /* if this triggers someone misused the debug library */
  if (lua_rawlen (L, idx) != sizeof (GValue))
    g_error ("Wrong length for userdata of type %s", table_name);
  if (type != G_TYPE_NONE && !g_type_is_a (G_VALUE_TYPE (v), type))
    return NULL;

  return v;
}

GValue *
_wplua_togvalue_userdata (lua_State *L, int idx, GType type)
{
  const char *table_name = _wplua_get_metatable_name (type);
  return table_name == NULL ? NULL :
         _wplua_togvalue_userdata_named (L, idx, type, table_name);
}

gboolean
_wplua_isgvalue_userdata (lua_State *L, int idx, GType type)
{
  return _wplua_togvalue_userdata (L, idx, type) != NULL;
}

GType
wplua_gvalue_userdata_type (lua_State *L, int idx)
{
  GValue *v;

  if (!lua_isuserdata (L, idx))
    return G_TYPE_INVALID;
  if (lua_rawlen (L, idx) != sizeof (GValue))
    return G_TYPE_INVALID;
  if (!(v = lua_touserdata (L, idx)))
    return G_TYPE_INVALID;

  return G_VALUE_TYPE (v);
}

int
_wplua_gvalue_userdata___gc (lua_State *L)
{
  GValue *v = lua_touserdata (L, 1);
  wp_trace_boxed (G_VALUE_TYPE (v), g_value_peek_pointer (v),
      "collected, v=%p", v);
  g_value_unset (v);
  return 0;
}

int
_wplua_gvalue_userdata___eq_impl (lua_State *L, const char *type)
{
  /*
   * First argument should always be a userdata.
   * Second argument can be anything.
   */
  GValue *v1 = luaL_checkudata (L, 1, type);
  GValue *v2 = luaL_testudata (L, 2, type);
  if (v2 != NULL) {
    gpointer p1 = g_value_peek_pointer (v1);
    gpointer p2 = g_value_peek_pointer (v2);
    lua_pushboolean (L, (p1 == p2));
  } else {
    lua_pushboolean (L, FALSE);
  }
  return 1;
}
