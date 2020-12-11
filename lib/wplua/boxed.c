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

static lua_CFunction
find_method_in_luaL_Reg (luaL_Reg *reg, const gchar *method)
{
  if (reg) {
    while (reg->name) {
      if (!g_strcmp0 (method, reg->name))
        return reg->func;
      reg++;
    }
  }
  return NULL;
}

static int
_wplua_gboxed___index (lua_State *L)
{
  luaL_argcheck (L, wplua_isboxed (L, 1), 1,
      "expected userdata storing GValue<GBoxed>");
  GValue *obj_v = lua_touserdata (L, 1);
  const gchar *key = luaL_checkstring (L, 2);
  lua_CFunction func = NULL;
  GHashTable *vtables;

  lua_getglobal (L, "__wplua_vtables");
  vtables = wplua_toboxed (L, -1);
  lua_pop (L, 1);

  /* search in registered vtables */
  if (!func) {
    GType type = G_VALUE_TYPE (obj_v);
    while (!func && type) {
      luaL_Reg *reg = g_hash_table_lookup (vtables, GUINT_TO_POINTER (type));
      func = find_method_in_luaL_Reg (reg, key);
      type = g_type_parent (type);
    }
  }

  if (func) {
    lua_pushcfunction (L, func);
    return 1;
  }
  return 0;
}

void
_wplua_init_gboxed (lua_State *L)
{
  static const luaL_Reg gboxed_meta[] = {
    { "__gc", _wplua_gvalue_userdata___gc },
    { "__eq", _wplua_gvalue_userdata___eq },
    { "__index", _wplua_gboxed___index },
    { NULL, NULL }
  };

  luaL_newmetatable (L, "GBoxed");
  luaL_setfuncs (L, gboxed_meta, 0);
  lua_pop (L, 1);
}

void
wplua_pushboxed (lua_State * L, GType type, gpointer object)
{
  g_return_if_fail (G_TYPE_FUNDAMENTAL (type) == G_TYPE_BOXED);

  GValue *v = _wplua_pushgvalue_userdata (L, type);
  wp_trace_boxed (type, object, "pushing to Lua, v=%p", v);
  g_value_take_boxed (v, object);

  luaL_getmetatable (L, "GBoxed");
  lua_setmetatable (L, -2);
}

gpointer
wplua_toboxed (lua_State *L, int idx)
{
  g_return_val_if_fail (_wplua_isgvalue_userdata (L, idx, G_TYPE_BOXED), NULL);
  return g_value_get_boxed ((GValue *) lua_touserdata (L, idx));
}

gpointer
wplua_checkboxed (lua_State *L, int idx, GType type)
{
  if (G_UNLIKELY (!_wplua_isgvalue_userdata (L, idx, type))) {
    wp_critical ("expected userdata storing GValue<%s>", g_type_name (type));
    luaL_argerror (L, idx, "expected userdata storing GValue<GBoxed>");
  }
  return g_value_get_boxed ((GValue *) lua_touserdata (L, idx));
}

gboolean
wplua_isboxed (lua_State *L, int idx)
{
  return _wplua_isgvalue_userdata (L, idx, G_TYPE_BOXED);
}
