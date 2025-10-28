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
  const gchar *key = luaL_tolstring (L, 2, NULL);
  GType type = G_VALUE_TYPE (obj_v);
  GType boxed_type = type;
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

  /* If WpProperties type, just return the property value for that key */
  if (boxed_type == WP_TYPE_PROPERTIES) {
    WpProperties * props = g_value_get_boxed (obj_v);
    const gchar *val = wp_properties_get (props, key);
    lua_pushstring (L, val);
    return 1;
  }

  return 0;
}

static int
_wplua_gboxed___newindex (lua_State *L)
{
  GValue *obj_v = _wplua_togvalue_userdata_named (L, 1, G_TYPE_BOXED, "GBoxed");
  luaL_argcheck (L, obj_v != NULL, 1,
      "expected userdata storing GValue<GBoxed>");
  const gchar *key = luaL_tolstring (L, 2, NULL);
  GType type = G_VALUE_TYPE (obj_v);

  /* Set property value */
  if (type == WP_TYPE_PROPERTIES) {
    WpProperties * props = g_value_dup_boxed (obj_v);
    g_autofree gchar *val = NULL;
    luaL_checkany (L, 3);

    switch (lua_type (L, 3)) {
      case LUA_TNIL:
        break;
      case LUA_TUSERDATA: {
        if (wplua_gvalue_userdata_type (L, 3) != G_TYPE_INVALID) {
          GValue *v = lua_touserdata (L, 3);
          gpointer p = g_value_peek_pointer (v);
          val = g_strdup_printf ("%p", p);
          break;
        } else {
          val = g_strdup (luaL_tolstring (L, 3, NULL));
          break;
        }
      }
      default:
        val = g_strdup (luaL_tolstring (L, 3, NULL));
        break;
    }

    props = wp_properties_ensure_unique_owner (props);
    wp_properties_set (props, key, val);
    g_value_take_boxed (obj_v, props);
  } else {
    luaL_error (L, "cannot assign property '%s' to boxed type %s",
        key, g_type_name (type));
  }
  return 0;
}

static int
properties_iterator_next (lua_State *L)
{
  WpIterator *it = wplua_checkboxed (L, 1, WP_TYPE_ITERATOR);
  g_auto (GValue) item = G_VALUE_INIT;
  if (wp_iterator_next (it, &item)) {
    WpPropertiesItem *si = g_value_get_boxed (&item);
    const gchar *k = wp_properties_item_get_key (si);
    const gchar *v = wp_properties_item_get_value (si);
    lua_pushstring (L, k);
    lua_pushstring (L, v);
    return 2;
  } else {
    lua_pushnil (L);
    lua_pushnil (L);
    return 2;
  }
}

static int
push_properties_wpiterator (lua_State *L, WpIterator *it)
{
  lua_pushcfunction (L, properties_iterator_next);
  wplua_pushboxed (L, WP_TYPE_ITERATOR, it);
  return 2;
}

static int
_wplua_gboxed___pairs (lua_State *L)
{
  GValue *obj_v = _wplua_togvalue_userdata_named (L, 1, G_TYPE_BOXED, "GBoxed");
  luaL_argcheck (L, obj_v != NULL, 1,
      "expected userdata storing GValue<GBoxed>");
  GType type = G_VALUE_TYPE (obj_v);

  if (type == WP_TYPE_PROPERTIES) {
    WpProperties * props = g_value_get_boxed (obj_v);
    WpIterator *it = wp_properties_new_iterator (props);
    return push_properties_wpiterator (L, it);
  } else {
    luaL_error (L, "cannot do pairs of boxed type %s", g_type_name (type));
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
    { "__newindex", _wplua_gboxed___newindex },
    { "__pairs", _wplua_gboxed___pairs },
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
