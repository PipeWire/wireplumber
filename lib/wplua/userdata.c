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

GValue *
_wplua_pushgvalue_userdata (lua_State * L, GType type)
{
  GValue *v = lua_newuserdata (L, sizeof (GValue));
  memset (v, 0, sizeof (GValue));
  g_value_init (v, type);
  return v;
}

gboolean
_wplua_isgvalue_userdata (lua_State *L, int idx, GType type)
{
  GValue *v;

  if (!lua_isuserdata (L, idx))
    return FALSE;
  if (lua_rawlen (L, idx) != sizeof (GValue))
    return FALSE;
  if (!(v = lua_touserdata (L, idx)))
    return FALSE;
  if (type != G_TYPE_NONE && !g_type_is_a (G_VALUE_TYPE (v), type))
    return FALSE;

  return TRUE;
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
_wplua_gvalue_userdata___eq (lua_State *L)
{
  if (_wplua_isgvalue_userdata (L, 1, G_TYPE_NONE) &&
      _wplua_isgvalue_userdata (L, 2, G_TYPE_NONE)) {
    GValue *v1 = lua_touserdata (L, 1);
    GValue *v2 = lua_touserdata (L, 2);
    gpointer p1 = g_value_peek_pointer (v1);
    gpointer p2 = g_value_peek_pointer (v2);
    lua_pushboolean (L, (p1 == p2));
  } else {
    lua_pushboolean (L, FALSE);
  }
  return 1;
}
