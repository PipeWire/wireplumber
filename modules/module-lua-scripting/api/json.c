/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <wplua/wplua.h>

#define WP_LOCAL_LOG_TOPIC log_topic_lua_scripting
WP_LOG_TOPIC_EXTERN (log_topic_lua_scripting)

/* API */

static int
spa_json_get_data (lua_State *L)
{
  WpSpaJson *json = wplua_checkboxed (L, 1, WP_TYPE_SPA_JSON);
  lua_pushstring (L, wp_spa_json_get_data (json));
  return 1;
}

static int
spa_json_get_size (lua_State *L)
{
  WpSpaJson *json = wplua_checkboxed (L, 1, WP_TYPE_SPA_JSON);
  lua_pushinteger (L, wp_spa_json_get_size (json));
  return 1;
}

static int
spa_json_to_string (lua_State *L)
{
  WpSpaJson *json = wplua_checkboxed (L, 1, WP_TYPE_SPA_JSON);
  /* Instead of using wp_spa_json_to_string() and lua_pushstring, we can avoid
   * an extra allocation if we use lua_pushlstring with wp_spa_json_get_data()
   * and wp_spa_json_get_size () */
  lua_pushlstring (L, wp_spa_json_get_data (json), wp_spa_json_get_size (json));
  return 1;
}

static int
spa_json_is_null (lua_State *L)
{
  WpSpaJson *json = wplua_checkboxed (L, 1, WP_TYPE_SPA_JSON);
  lua_pushboolean (L, wp_spa_json_is_null (json));
  return 1;
}

static int
spa_json_is_boolean (lua_State *L)
{
  WpSpaJson *json = wplua_checkboxed (L, 1, WP_TYPE_SPA_JSON);
  lua_pushboolean (L, wp_spa_json_is_boolean (json));
  return 1;
}

static int
spa_json_is_int (lua_State *L)
{
  WpSpaJson *json = wplua_checkboxed (L, 1, WP_TYPE_SPA_JSON);
  lua_pushboolean (L, wp_spa_json_is_int (json));
  return 1;
}

static int
spa_json_is_float (lua_State *L)
{
  WpSpaJson *json = wplua_checkboxed (L, 1, WP_TYPE_SPA_JSON);
  lua_pushboolean (L, wp_spa_json_is_float (json));
  return 1;
}

static int
spa_json_is_string (lua_State *L)
{
  WpSpaJson *json = wplua_checkboxed (L, 1, WP_TYPE_SPA_JSON);
  lua_pushboolean (L, wp_spa_json_is_string (json));
  return 1;
}

static int
spa_json_is_array (lua_State *L)
{
  WpSpaJson *json = wplua_checkboxed (L, 1, WP_TYPE_SPA_JSON);
  lua_pushboolean (L, wp_spa_json_is_array (json));
  return 1;
}

static int
spa_json_is_object (lua_State *L)
{
  WpSpaJson *json = wplua_checkboxed (L, 1, WP_TYPE_SPA_JSON);
  lua_pushboolean (L, wp_spa_json_is_object (json));
  return 1;
}

void
push_luajson (lua_State *L, WpSpaJson *json)
{
  /* Null */
  if (wp_spa_json_is_null (json)) {
    lua_pushnil (L);
  }

  /* Boolean */
  else if (wp_spa_json_is_boolean (json)) {
    gboolean value = FALSE;
    g_warn_if_fail (wp_spa_json_parse_boolean (json, &value));
    lua_pushboolean (L, value);
  }

  /* Int */
  else if (wp_spa_json_is_int (json)) {
    gint value = 0;
    g_warn_if_fail (wp_spa_json_parse_int (json, &value));
    lua_pushinteger (L, value);
  }

  /* Float */
  else if (wp_spa_json_is_float (json)) {
    float value = 0;
    g_warn_if_fail (wp_spa_json_parse_float (json, &value));
    lua_pushnumber (L, value);
  }

  /* String */
  else if (wp_spa_json_is_string (json)) {
    g_autofree gchar *value = wp_spa_json_parse_string (json);
    g_warn_if_fail (value);
    lua_pushstring (L, value);
  }

  /* Array */
  else if (wp_spa_json_is_array (json)) {
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpIterator) it = wp_spa_json_new_iterator (json);
    guint i = 1;
    lua_newtable (L);
    for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
      WpSpaJson *j = g_value_get_boxed (&item);
      push_luajson (L, j);
      lua_rawseti (L, -2, i++);
    }
  }

  /* Object */
  else if (wp_spa_json_is_object (json)) {
    g_auto (GValue) item = G_VALUE_INIT;
    g_autoptr (WpIterator) it = wp_spa_json_new_iterator (json);
    lua_newtable (L);
    for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
      WpSpaJson *key = g_value_get_boxed (&item);
      g_autofree gchar *key_str = NULL;
      WpSpaJson *value = NULL;
      key_str = wp_spa_json_parse_string (key);
      g_warn_if_fail (key_str);
      g_value_unset (&item);
      if (!wp_iterator_next (it, &item))
        break;
      value = g_value_get_boxed (&item);
      push_luajson (L, value);
      lua_setfield (L, -2, key_str);
    }
  }
}

static int
spa_json_parse (lua_State *L)
{
  WpSpaJson *json = wplua_checkboxed (L, 1, WP_TYPE_SPA_JSON);
  push_luajson (L, json);
  return 1;
}

/* Raw */

static int
spa_json_raw_new (lua_State *L)
{
  const gchar *value = lua_tostring (L, 1);
  wplua_pushboxed (L, WP_TYPE_SPA_JSON, wp_spa_json_new_from_string (value));
  return 1;
}

/* None */

static int
spa_json_null_new (lua_State *L)
{
  wplua_pushboxed (L, WP_TYPE_SPA_JSON, wp_spa_json_new_null ());
  return 1;
}

/* Boolean */

static int
spa_json_boolean_new (lua_State *L)
{
  gboolean value = lua_toboolean (L, 1);
  wplua_pushboxed (L, WP_TYPE_SPA_JSON, wp_spa_json_new_boolean (value));
  return 1;
}

/* Int */

static int
spa_json_int_new (lua_State *L)
{
  gint64 value = lua_tointeger (L, 1);
  wplua_pushboxed (L, WP_TYPE_SPA_JSON, wp_spa_json_new_int (value));
  return 1;
}

/* Float */

static int
spa_json_float_new (lua_State *L)
{
  float value = lua_tonumber (L, 1);
  wplua_pushboxed (L, WP_TYPE_SPA_JSON, wp_spa_json_new_float (value));
  return 1;
}

/* String */

static int
spa_json_string_new (lua_State *L)
{
  const gchar *value = lua_tostring (L, 1);
  wplua_pushboxed (L, WP_TYPE_SPA_JSON, wp_spa_json_new_string (value));
  return 1;
}

/* Array */

static int
spa_json_array_new (lua_State *L)
{
  g_autoptr (WpSpaJsonBuilder) builder = wp_spa_json_builder_new_array ();

  luaL_checktype (L, 1, LUA_TTABLE);

  lua_pushnil (L);
  while (lua_next (L, -2)) {
    /* We only add table values with integer keys */
    if (lua_isinteger (L, -2)) {
      switch (lua_type (L, -1)) {
        case LUA_TBOOLEAN:
          wp_spa_json_builder_add_boolean (builder, lua_toboolean (L, -1));
          break;
        case LUA_TNUMBER:
          if (lua_isinteger (L, -1))
            wp_spa_json_builder_add_int (builder, lua_tointeger (L, -1));
          else
            wp_spa_json_builder_add_float (builder, lua_tonumber (L, -1));
          break;
        case LUA_TSTRING:
          wp_spa_json_builder_add_string (builder, lua_tostring (L, -1));
          break;
        case LUA_TUSERDATA: {
          WpSpaJson *json = wplua_checkboxed (L, -1, WP_TYPE_SPA_JSON);
          wp_spa_json_builder_add_json (builder, json);
          break;
        }
        default:
          luaL_error (L, "Json does not support lua type ",
              lua_typename(L, lua_type(L, -1)));
          break;
      }
    }
    lua_pop (L, 1);
  }

  wplua_pushboxed (L, WP_TYPE_SPA_JSON, wp_spa_json_builder_end (builder));
  return 1;
}

/* Object */

static int
spa_json_object_new (lua_State *L)
{
  g_autoptr (WpSpaJsonBuilder) builder = wp_spa_json_builder_new_object ();

  luaL_checktype (L, 1, LUA_TTABLE);

  lua_pushnil (L);
  while (lua_next (L, -2)) {
    /* We only add table values with string keys */
    if (lua_type (L, -2) == LUA_TSTRING) {
      wp_spa_json_builder_add_property (builder, lua_tostring (L, -2));

      switch (lua_type (L, -1)) {
        case LUA_TBOOLEAN:
          wp_spa_json_builder_add_boolean (builder, lua_toboolean (L, -1));
          break;
        case LUA_TNUMBER:
          if (lua_isinteger (L, -1))
            wp_spa_json_builder_add_int (builder, lua_tointeger (L, -1));
          else
            wp_spa_json_builder_add_float (builder, lua_tonumber (L, -1));
          break;
        case LUA_TSTRING:
          wp_spa_json_builder_add_string (builder, lua_tostring (L, -1));
          break;
        case LUA_TUSERDATA: {
          WpSpaJson *json = wplua_checkboxed (L, -1, WP_TYPE_SPA_JSON);
          wp_spa_json_builder_add_json (builder, json);
          break;
        }
        default:
          luaL_error (L, "Json does not support lua type ",
              lua_typename(L, lua_type(L, -1)));
          break;
      }
    }

    lua_pop (L, 1);
  }

  wplua_pushboxed (L, WP_TYPE_SPA_JSON, wp_spa_json_builder_end (builder));
  return 1;
}

/* Init */

static const luaL_Reg spa_json_methods[] = {
  { "get_data", spa_json_get_data },
  { "get_size", spa_json_get_size },
  { "to_string", spa_json_to_string },
  { "is_null", spa_json_is_null },
  { "is_boolean", spa_json_is_boolean },
  { "is_int", spa_json_is_int },
  { "is_float", spa_json_is_float },
  { "is_string", spa_json_is_string },
  { "is_array", spa_json_is_array },
  { "is_object", spa_json_is_object },
  { "parse", spa_json_parse },
  { NULL, NULL }
};

static const luaL_Reg spa_json_constructors[] = {
  { "Raw", spa_json_raw_new },
  { "Null", spa_json_null_new },
  { "Boolean", spa_json_boolean_new },
  { "Int", spa_json_int_new },
  { "Float", spa_json_float_new },
  { "String", spa_json_string_new },
  { "Array", spa_json_array_new },
  { "Object", spa_json_object_new },
  { NULL, NULL }
};

void
wp_lua_scripting_json_init (lua_State *L)
{
  luaL_newlib (L, spa_json_constructors);
  lua_setglobal (L, "WpSpaJson");

  wplua_register_type_methods (L, WP_TYPE_SPA_JSON, NULL, spa_json_methods);
}
