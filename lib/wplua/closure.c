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

/* This structure is added to a lua global and it's only referenced from there;
   When the lua_State closes, it is unrefed and its finalize function below
   invalidates all the closures so that nothing attempts to call into a lua
   function after the state is closed */
typedef struct _WpLuaClosureStore WpLuaClosureStore;
struct _WpLuaClosureStore
{
  GPtrArray *closures;
};

static WpLuaClosureStore *
_wplua_closure_store_new (void)
{
  WpLuaClosureStore *self = g_rc_box_new (WpLuaClosureStore);
  self->closures = g_ptr_array_new ();
  return self;
}

static void
_wplua_closure_store_finalize (WpLuaClosureStore * self)
{
  for (guint i = self->closures->len; i > 0; i--) {
    GClosure *c = g_ptr_array_index (self->closures, i-1);
    g_closure_ref (c);
    g_closure_invalidate (c);
    g_ptr_array_remove_index_fast (self->closures, i-1);
    g_closure_unref (c);
  }
  g_ptr_array_unref (self->closures);
}

static WpLuaClosureStore *
_wplua_closure_store_ref (WpLuaClosureStore * self)
{
  return g_rc_box_acquire (self);
}

static void
_wplua_closure_store_unref (WpLuaClosureStore * self)
{
  g_rc_box_release_full (self, (GDestroyNotify) _wplua_closure_store_finalize);
}

G_DEFINE_BOXED_TYPE(WpLuaClosureStore, _wplua_closure_store,
    _wplua_closure_store_ref, _wplua_closure_store_unref)


typedef struct _WpLuaClosure WpLuaClosure;
struct _WpLuaClosure
{
  GClosure closure;
  int func_ref;
  GPtrArray *closures;
};

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
  int res = _wplua_pcall (L, n_param_values, return_value ? 1 : 0);

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

static void
_wplua_closure_finalize (lua_State *L, WpLuaClosure *c)
{
  g_ptr_array_remove_fast (c->closures, c);
  g_ptr_array_unref (c->closures);
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
  WpLuaClosureStore *store;

  lua_pushvalue (L, idx);
  wlc->func_ref = luaL_ref (L, LUA_REGISTRYINDEX);

  wp_trace_boxed (G_TYPE_CLOSURE, c, "created, func_ref = %d", wlc->func_ref);

  g_closure_set_marshal (c, _wplua_closure_marshal);
  g_closure_add_invalidate_notifier (c, L,
      (GClosureNotify) _wplua_closure_invalidate);
  g_closure_add_finalize_notifier (c, L,
      (GClosureNotify) _wplua_closure_finalize);

  /* keep a weak ref of the closure in the store's array,
     so that we can invalidate the closure when lua_State closes;
     keep a strong ref of the array in the closure so that
     _wplua_closure_finalize() works even after the state is closed */
  lua_pushliteral (L, "wplua_closures");
  lua_gettable (L, LUA_REGISTRYINDEX);
  store = wplua_toboxed (L, -1);
  lua_pop (L, 1);

  g_ptr_array_add (store->closures, c);
  wlc->closures = g_ptr_array_ref (store->closures);

  return c;
}

void
_wplua_init_closure (lua_State *L)
{
  lua_pushliteral (L, "wplua_closures");
  wplua_pushboxed (L,
      _wplua_closure_store_get_type (),
      _wplua_closure_store_new ());
  lua_settable (L, LUA_REGISTRYINDEX);
}
