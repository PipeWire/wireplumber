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
  GValue *obj_v = _wplua_togvalue_userdata_named (L, 1, G_TYPE_BOXED, "GBoxed");
  luaL_argcheck (L, obj_v != NULL, 1,
      "expected userdata storing GValue<GBoxed>");
  const gchar *key = luaL_checkstring (L, 2);
  GType type = G_VALUE_TYPE (obj_v);
  lua_CFunction func = NULL;
  GHashTable *vtables;

  lua_pushliteral (L, "wplua_vtables");
  lua_gettable (L, LUA_REGISTRYINDEX);
  vtables = wplua_toboxed (L, -1);
  lua_pop (L, 1);

  /* search in registered vtables */
  while (!func && type) {
    luaL_Reg *reg = g_hash_table_lookup (vtables, GUINT_TO_POINTER (type));
    func = find_method_in_luaL_Reg (reg, key);
    type = g_type_parent (type);
  }

  wp_trace_boxed (type, g_value_get_boxed (obj_v),
      "indexing GBoxed, looking for '%s', found: %p", key, func);

  if (func) {
    lua_pushcfunction (L, func);
    return 1;
  }
  return 0;
}

static int
_wplua_gboxed___eq (lua_State *L)
{
  return _wplua_gvalue_userdata___eq_impl (L, "GBoxed");
}

void
_wplua_init_gboxed (lua_State *L)
{
  static const luaL_Reg gboxed_meta[] = {
    { "__gc", _wplua_gvalue_userdata___gc },
    { "__eq", _wplua_gboxed___eq },
    { "__index", _wplua_gboxed___index },
    { NULL, NULL }
  };

  if (!luaL_newmetatable (L, "GBoxed"))
    g_error ("Metatable with key GBoxed in the registry already exists?");
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
}

gpointer
wplua_toboxed (lua_State *L, int idx)
{
  GValue *v = _wplua_togvalue_userdata_named (L, idx, G_TYPE_BOXED, "GBoxed");

  g_return_val_if_fail (v, NULL);
  return g_value_get_boxed (v);
}

gpointer
wplua_checkboxed (lua_State *L, int idx, GType type)
{
  GValue *v = _wplua_togvalue_userdata_named (L, idx, type, "GBoxed");
  if (v == NULL) {
    wp_critical ("expected userdata storing GValue<%s>", g_type_name (type));
    luaL_argerror (L, idx, "expected userdata storing GValue<GBoxed>");
  }
  return g_value_get_boxed (v);
}

gboolean
wplua_isboxed (lua_State *L, int idx, GType type)
{
  return g_type_is_a (type, G_TYPE_BOXED) &&
         _wplua_togvalue_userdata_named (L, idx, type, "GBoxed") != NULL;
}
