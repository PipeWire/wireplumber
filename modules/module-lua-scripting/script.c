/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "script.h"
#include <pipewire/keys.h>

#define WP_LOCAL_LOG_TOPIC log_topic_lua_scripting
WP_LOG_TOPIC_EXTERN (log_topic_lua_scripting)

/*
 * This is a WpPlugin subclass that wraps a single lua script and acts like
 * a handle for that script. When enabled, through the WpObject activation
 * mechanism, the script is executed. It then provides an API for the script
 * to declare when it has finished its activation procedure, which can be
 * asynchronous (this is Script.finish_activation in Lua).
 * When disabled, this class destroys the global environment that was used
 * in the Lua engine for excecuting that script, effectively destroying all
 * objects that were held in Lua as global variables.
 */

struct _WpLuaScript
{
  WpPlugin parent;

  lua_State *L;
  gchar *filename;
  GVariant *args;
};

enum {
  PROP_0,
  PROP_LUA_ENGINE,
  PROP_FILENAME,
  PROP_ARGUMENTS,
};

G_DEFINE_TYPE (WpLuaScript, wp_lua_script, WP_TYPE_PLUGIN)

static void
wp_lua_script_init (WpLuaScript * self)
{
}

static void
wp_lua_script_cleanup (WpLuaScript * self)
{
  /* LUA_REGISTRYINDEX[self] = nil */
  if (self->L) {
    lua_pushnil (self->L);
    lua_rawsetp (self->L, LUA_REGISTRYINDEX, self);
  }
}

static void
wp_lua_script_finalize (GObject * object)
{
  WpLuaScript *self = WP_LUA_SCRIPT (object);

  wp_lua_script_cleanup (self);
  g_clear_pointer (&self->L, wplua_unref);
  g_clear_pointer (&self->filename, g_free);
  g_clear_pointer (&self->args, g_variant_unref);

  G_OBJECT_CLASS (wp_lua_script_parent_class)->finalize (object);
}

static void
wp_lua_script_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpLuaScript *self = WP_LUA_SCRIPT (object);

  switch (property_id) {
  case PROP_LUA_ENGINE:
    g_return_if_fail (self->L == NULL);
    self->L = g_value_get_pointer (value);
    if (self->L)
      self->L = wplua_ref (self->L);
    break;
  case PROP_FILENAME:
    self->filename = g_value_dup_string (value);
    break;
  case PROP_ARGUMENTS:
    self->args = g_value_dup_variant (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static gboolean
wp_lua_script_check_async_activation (WpLuaScript * self)
{
  gboolean ret;
  lua_rawgetp (self->L, LUA_REGISTRYINDEX, self);
  lua_pushliteral (self->L, "Script");
  lua_gettable (self->L, -2);
  lua_pushliteral (self->L, "async_activation");
  lua_gettable (self->L, -2);
  ret = lua_toboolean (self->L, -1);
  lua_pop (self->L, 3);
  return ret;
}

static void
wp_lua_script_detach_transition (WpLuaScript * self)
{
  lua_rawgetp (self->L, LUA_REGISTRYINDEX, self);
  lua_pushliteral (self->L, "Script");
  lua_gettable (self->L, -2);
  lua_pushliteral (self->L, "__transition");
  lua_pushnil (self->L);
  lua_settable (self->L, -3);
  lua_pop (self->L, 2);
}

static int
script_finish_activation (lua_State * L)
{
  WpLuaScript *self;

  luaL_checktype (L, 1, LUA_TTABLE);

  lua_pushliteral (L, "__self");
  lua_gettable (L, 1);
  luaL_checktype (L, -1, LUA_TLIGHTUSERDATA);
  self = WP_LUA_SCRIPT ((gpointer) lua_topointer (L, -1));
  lua_pop (L, 2);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
  return 0;
}

static int
script_finish_activation_with_error (lua_State * L)
{
  WpTransition *transition = NULL;
  const char *msg = NULL;

  luaL_checktype (L, 1, LUA_TTABLE);
  msg = luaL_checkstring (L, 2);

  lua_pushliteral (L, "__transition");
  lua_gettable (L, 1);
  if (lua_type (L, -1) == LUA_TLIGHTUSERDATA)
    transition = WP_TRANSITION ((gpointer) lua_topointer (L, -1));
  lua_pop (L, 2);

  if (transition)
    wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED, "%s", msg));
  return 0;
}

static const luaL_Reg script_api_methods[] = {
  { "finish_activation", script_finish_activation },
  { "finish_activation_with_error", script_finish_activation_with_error },
  { NULL, NULL }
};

static int
wp_lua_script_sandbox (lua_State *L)
{
  luaL_checktype (L, 1, LUA_TLIGHTUSERDATA); // self
  luaL_checktype (L, 2, LUA_TLIGHTUSERDATA); // transition
  luaL_checktype (L, 3, LUA_TFUNCTION); // the script chunk

  /* create unique environment for this script */
  lua_getglobal (L, "create_sandbox_env");
  lua_call (L, 0, 1);

  /* create "Script" API */
  lua_pushliteral (L, "Script");
  luaL_newlib (L, script_api_methods);
  lua_pushliteral (L, "__self");
  lua_pushvalue (L, 1);
  lua_settable (L, -3);
  lua_pushliteral (L, "__transition");
  lua_pushvalue (L, 2);
  lua_settable (L, -3);
  lua_settable (L, -3);

  /* store the environment */
  /* LUA_REGISTRYINDEX[self] = env */
  lua_pushvalue (L, 1); // self
  lua_pushvalue (L, -2); // the table returned by create_sandbox_env
  lua_rawset (L, LUA_REGISTRYINDEX);

  /* set it as the 1st upvalue (_ENV) on the loaded script chunk (at index 3) */
  lua_setupvalue (L, 3, 1);

  /* anything remaining on the stack are function arguments */
  int nargs = lua_gettop (L) - 3;

  /* execute script */
  lua_call (L, nargs, 0);
  return 0;
}

static void
wp_lua_script_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpLuaScript *self = WP_LUA_SCRIPT (plugin);
  g_autoptr (GError) error = NULL;
  int top, nargs = 3;

  if (!self->L) {
    error = g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "No lua state open; lua-scripting plugin is not enabled");
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  top = lua_gettop (self->L);
  lua_pushcfunction (self->L, wp_lua_script_sandbox);
  lua_pushlightuserdata (self->L, self);
  lua_pushlightuserdata (self->L, transition);

  /* load script */
  if (!wplua_load_path (self->L, self->filename, &error)) {
    lua_settop (self->L, top);
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  /* push script arguments */
  if (self->args) {
    wplua_gvariant_to_lua (self->L, self->args);
    nargs++;
  }

  /* execute script */
  if (!wplua_pcall (self->L, nargs, 0, &error)) {
    lua_settop (self->L, top);
    wp_transition_return_error (transition, g_steal_pointer (&error));
    wp_lua_script_cleanup (self);
    return;
  }

  if (!wp_lua_script_check_async_activation (self)) {
    wp_lua_script_detach_transition (self);
    wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
  } else {
    g_signal_connect_object (transition, "notify::completed",
        (GCallback) wp_lua_script_detach_transition, self, G_CONNECT_SWAPPED);
  }

  lua_settop (self->L, top);
}

static void
wp_lua_script_disable (WpPlugin * plugin)
{
  WpLuaScript *self = WP_LUA_SCRIPT (plugin);
  wp_lua_script_cleanup (self);
}

static void
wp_lua_script_class_init (WpLuaScriptClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_lua_script_finalize;
  object_class->set_property = wp_lua_script_set_property;

  plugin_class->enable = wp_lua_script_enable;
  plugin_class->disable = wp_lua_script_disable;

  g_object_class_install_property (object_class, PROP_LUA_ENGINE,
      g_param_spec_pointer ("lua-engine", "lua-engine", "lua-engine",
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FILENAME,
      g_param_spec_string ("filename", "filename", "filename", NULL,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ARGUMENTS,
      g_param_spec_variant ("arguments", "arguments", "arguments",
          G_VARIANT_TYPE_VARDICT, NULL,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}
