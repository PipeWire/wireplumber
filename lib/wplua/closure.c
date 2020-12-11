/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "wplua.h"
#include <wp/wp.h>

typedef struct _WpLuaClosure WpLuaClosure;
struct _WpLuaClosure
{
  GClosure closure;
  int func_ref;
};

static int
_wplua_closure_errhandler (lua_State *L)
{
  wp_warning ("%s", lua_tostring (L, -1));
  lua_pop (L, 1);

  luaL_traceback (L, L, "traceback:\n", 1);
  wp_warning ("%s", lua_tostring (L, -1));
  lua_pop (L, 1);

  return 0;
}

static int
_wplua_closure_pcall (lua_State *L, int nargs, int nret)
{
  int hpos = lua_gettop (L) - nargs;
  int ret = LUA_OK;

  lua_pushcfunction (L, _wplua_closure_errhandler);
  lua_insert (L, hpos);

  ret = lua_pcall (L, nargs, nret, hpos);
  switch (ret) {
  case LUA_ERRMEM:
    wp_critical ("not enough memory");
    break;
  case LUA_ERRERR:
    wp_critical ("error running the message handler");
    break;
  case LUA_ERRGCMM:
    wp_critical ("error running __gc");
    break;
  default:
    break;
  }

  lua_remove (L, hpos);
  return ret;
}

static void
_wplua_closure_marshal (GClosure *closure, GValue *return_value,
    guint n_param_values, const GValue *param_values,
    gpointer invocation_hint, gpointer marshal_data)
{
  lua_State *L = closure->data;
  int func_ref = ((WpLuaClosure *) closure)->func_ref;

  /* invalid closure, skip it */
  if (func_ref == LUA_NOREF || func_ref == LUA_REFNIL)
    return;

  /* clear the stack and stop the garbage collector for now */
  lua_settop (L, 0);
  lua_gc (L, LUA_GCSTOP, 0);

  /* push the function */
  lua_rawgeti (L, LUA_REGISTRYINDEX, func_ref);

  /* push arguments */
  for (guint i = 0; i < n_param_values; i++)
    wplua_gvalue_to_lua (L, &param_values[i]);

  /* call in protected mode */
  int res = _wplua_closure_pcall (L, n_param_values, return_value ? 1 : 0);

  /* handle the result */
  if (res == LUA_OK && return_value && lua_gettop (L) >= 1)
    wplua_lua_to_gvalue (L, 1, return_value);

  /* clear the stack and clean up */
  lua_settop (L, 0);
  lua_gc (L, LUA_GCCOLLECT, 0);
  lua_gc (L, LUA_GCRESTART, 0);
}

static void
_wplua_closure_invalidate (lua_State *L, WpLuaClosure *c)
{
  wp_trace_boxed (G_TYPE_CLOSURE, c, "invalidated");
  luaL_unref (L, LUA_REGISTRYINDEX, c->func_ref);
  c->func_ref = LUA_NOREF;
}

/**
 * wplua_function_to_closure:
 *
 * Make a GClosure out of a Lua function at index @idx
 *
 * Returns: (transfer floating): the new closure
 */
GClosure *
wplua_function_to_closure (lua_State *L, int idx)
{
  g_return_val_if_fail (lua_isfunction(L, idx), NULL);

  GClosure *c = g_closure_new_simple (sizeof (WpLuaClosure), L);
  WpLuaClosure *wlc = (WpLuaClosure *) c;
  GPtrArray *closures;

  lua_getglobal (L, "__wplua_closures");
  closures = wplua_toboxed (L, -1);
  lua_pop (L, 1);

  lua_pushvalue (L, idx);
  wlc->func_ref = luaL_ref (L, LUA_REGISTRYINDEX);

  wp_trace_boxed (G_TYPE_CLOSURE, c, "created, func_ref = %d", wlc->func_ref);

  g_closure_set_marshal (c, _wplua_closure_marshal);
  g_closure_add_invalidate_notifier (c, L,
      (GClosureNotify) _wplua_closure_invalidate);

  /* keep a ref in lua, so that we can invalidate
     the closure when lua_State closes */
  g_ptr_array_add (closures, g_closure_ref (c));

  return c;
}

static void
_wplua_closure_destroy (GClosure * c)
{
  g_closure_invalidate (c);
  g_closure_unref (c);
}

void
_wplua_init_closure (lua_State *L)
{
  GPtrArray *a = g_ptr_array_new_with_free_func (
      (GDestroyNotify) _wplua_closure_destroy);
  wplua_pushboxed (L, G_TYPE_PTR_ARRAY, a);
  lua_setglobal (L, "__wplua_closures");
}
