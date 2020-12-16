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

static int
_wplua_gobject_call (lua_State *L)
{
  GObject *obj = wplua_checkobject (L, 1, G_TYPE_OBJECT);
  const char *sig_name = lua_tostring (L, 2);
  guint n_params = lua_gettop (L) - 2;
  GSignalQuery query;
  guint sig_id = 0;
  GQuark detail = 0;

  if (G_UNLIKELY (!g_signal_parse_name (sig_name, G_TYPE_FROM_INSTANCE (obj),
                                        &sig_id, &detail, FALSE)))
    luaL_error (L, "unknown signal '%s::%s'", G_OBJECT_TYPE_NAME (obj),
        sig_name);

  g_signal_query (sig_id, &query);

  if (G_UNLIKELY (!(query.signal_flags & G_SIGNAL_ACTION)))
    luaL_error (L, "lua code is not allowed to emit non-action signal '%s::%s'",
        G_OBJECT_TYPE_NAME (obj), sig_name);

  if (G_UNLIKELY (query.n_params > n_params))
    luaL_error (L, "not enough arguments for '%s::%s': expected %d, got %d",
        G_OBJECT_TYPE_NAME (obj), sig_name, query.n_params, n_params);

  GValue ret = G_VALUE_INIT;
  GValue *vals = g_newa (GValue, n_params + 1);
  memset (vals, 0, sizeof (GValue) * (n_params + 1));

  if (query.return_type != G_TYPE_NONE)
    g_value_init (&ret, query.return_type);

  g_value_init_from_instance (&vals[0], obj);
  for (guint i = 0; i < n_params; i++) {
    g_value_init (&vals[i+1], query.param_types[i]);
    wplua_lua_to_gvalue (L, i+3, &vals[i+1]);
  }

  g_signal_emitv (vals, sig_id, detail, &ret);

  if (query.return_type != G_TYPE_NONE)
    return wplua_gvalue_to_lua (L, &ret);
  else
    return 0;
}

static int
_wplua_gobject_connect (lua_State *L)
{
  GObject *obj = wplua_checkobject (L, 1, G_TYPE_OBJECT);
  const char *sig_name = luaL_checkstring (L, 2);
  luaL_checktype (L, 3, LUA_TFUNCTION);

  guint sig_id = 0;
  GQuark detail = 0;

  if (G_UNLIKELY (!g_signal_parse_name (sig_name, G_TYPE_FROM_INSTANCE (obj),
                                        &sig_id, &detail, FALSE)))
    luaL_error (L, "unknown signal '%s::%s'", G_OBJECT_TYPE_NAME (obj),
        sig_name);

  GClosure *closure = wplua_function_to_closure (L, 3);
  gulong handler =
      g_signal_connect_closure_by_id (obj, sig_id, detail, closure, FALSE);

  lua_pushinteger (L, handler);
  return 1;
}

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
_wplua_gobject___index (lua_State *L)
{
  GObject *obj = wplua_checkobject (L, 1, G_TYPE_OBJECT);
  const gchar *key = luaL_checkstring (L, 2);
  lua_CFunction func = NULL;
  GHashTable *vtables;

  lua_pushliteral (L, "wplua_vtables");
  lua_gettable (L, LUA_REGISTRYINDEX);
  vtables = wplua_toboxed (L, -1);
  lua_pop (L, 1);

  if (!g_strcmp0 (key, "call"))
    func = _wplua_gobject_call;
  else if (!g_strcmp0 (key, "connect"))
    func = _wplua_gobject_connect;

  /* search in registered vtables */
  if (!func) {
    GType type = G_TYPE_FROM_INSTANCE (obj);
    while (!func && type) {
      luaL_Reg *reg = g_hash_table_lookup (vtables, GUINT_TO_POINTER (type));
      func = find_method_in_luaL_Reg (reg, key);
      type = g_type_parent (type);
    }
  }

  /* search in registered vtables of interfaces */
  if (!func) {
    g_autofree GType *interfaces =
        g_type_interfaces (G_TYPE_FROM_INSTANCE (obj), NULL);
    GType *type = interfaces;
    while (!func && *type) {
      luaL_Reg *reg = g_hash_table_lookup (vtables, GUINT_TO_POINTER (*type));
      func = find_method_in_luaL_Reg (reg, key);
      type++;
    }
  }

  if (func) {
    lua_pushcfunction (L, func);
    return 1;
  }
  else {
    /* search in properties */
    GObjectClass *klass = G_OBJECT_GET_CLASS (obj);
    GParamSpec *pspec = g_object_class_find_property (klass, key);
    if (pspec && (pspec->flags & G_PARAM_READABLE)) {
      g_auto (GValue) v = G_VALUE_INIT;
      g_value_init (&v, pspec->value_type);
      g_object_get_property (obj, key, &v);
      return wplua_gvalue_to_lua (L, &v);
    }
  }

  return 0;
}

static int
_wplua_gobject___newindex (lua_State *L)
{
  GObject *obj = wplua_checkobject (L, 1, G_TYPE_OBJECT);
  const gchar *key = luaL_checkstring (L, 2);

  /* search in properties */
  GObjectClass *klass = G_OBJECT_GET_CLASS (obj);
  GParamSpec *pspec = g_object_class_find_property (klass, key);
  if (pspec && (pspec->flags & G_PARAM_WRITABLE)) {
    g_auto (GValue) v = G_VALUE_INIT;
    g_value_init (&v, pspec->value_type);
    wplua_lua_to_gvalue (L, 3, &v);
    g_object_set_property (obj, key, &v);
  } else {
    luaL_error (L, "attempted to assign unknown or non-writable property '%s'",
        key);
  }
  return 0;
}

void
_wplua_init_gobject (lua_State *L)
{
  static const luaL_Reg gobject_meta[] = {
    { "__gc", _wplua_gvalue_userdata___gc },
    { "__eq", _wplua_gvalue_userdata___eq },
    { "__index", _wplua_gobject___index },
    { "__newindex", _wplua_gobject___newindex },
    { NULL, NULL }
  };

  luaL_newmetatable (L, "GObject");
  luaL_setfuncs (L, gobject_meta, 0);
  lua_pop (L, 1);
}

void
wplua_pushobject (lua_State * L, gpointer object)
{
  g_return_if_fail (G_IS_OBJECT (object));

  GValue *v = _wplua_pushgvalue_userdata (L, G_TYPE_FROM_INSTANCE (object));
  wp_trace_object (object, "pushing to Lua, v=%p", v);
  g_value_take_object (v, object);

  luaL_getmetatable (L, "GObject");
  lua_setmetatable (L, -2);
}

gpointer
wplua_toobject (lua_State *L, int idx)
{
  g_return_val_if_fail (_wplua_isgvalue_userdata (L, idx, G_TYPE_OBJECT), NULL);
  return g_value_get_object ((GValue *) lua_touserdata (L, idx));
}

gpointer
wplua_checkobject (lua_State *L, int idx, GType type)
{
  if (G_UNLIKELY (!_wplua_isgvalue_userdata (L, idx, type))) {
    wp_critical ("expected userdata storing GValue<%s>", g_type_name (type));
    luaL_argerror (L, idx, "expected userdata storing GValue<GObject>");
  }
  return g_value_get_object ((GValue *) lua_touserdata (L, idx));
}

gboolean
wplua_isobject (lua_State *L, int idx, GType type)
{
  if (!g_type_is_a (type, G_TYPE_OBJECT)) return FALSE;
  return _wplua_isgvalue_userdata (L, idx, type);
}
