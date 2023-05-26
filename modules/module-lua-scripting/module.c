/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <wplua/wplua.h>
#include <pipewire/keys.h>

#include "script.h"

#define WP_LOCAL_LOG_TOPIC log_topic_lua_scripting
WP_LOG_TOPIC (log_topic_lua_scripting, "m-lua-scripting")

void wp_lua_scripting_api_init (lua_State *L);

struct _WpLuaScriptingPlugin
{
  WpPlugin parent;
  lua_State *L;
};

static int
wp_lua_scripting_package_loader (lua_State *L)
{
  luaL_checktype (L, 2, LUA_TFUNCTION);
  wplua_push_sandbox (L);
  lua_pushvalue (L, 2);
  lua_call (L, 1, 1);
  return 1;
}

static int
wp_lua_scripting_package_searcher (lua_State *L)
{
  const gchar *name = luaL_checkstring (L, 1);
  g_autoptr (GError) error = NULL;
  g_autofree gchar *filename = g_strdup_printf ("%s.lua", name);
  g_autofree gchar *script = wp_find_file (
      WP_LOOKUP_DIR_ENV_TEST_SRCDIR |
      WP_LOOKUP_DIR_ENV_DATA |
      WP_LOOKUP_DIR_XDG_CONFIG_HOME |
      WP_LOOKUP_DIR_ETC |
      WP_LOOKUP_DIR_PREFIX_SHARE,
      filename, "scripts/lib");

  if (!script)  {
    lua_pushliteral (L, "script not found");
    return 1;
  }

  /* 1. loader (function) */
  lua_pushcfunction (L, wp_lua_scripting_package_loader);

  /* 2. loader data (param to 1) */
  wp_debug ("Executing script %s", script);
  if (!wplua_load_path (L, script, &error)) {
    lua_pop (L, 1);
    lua_pushstring (L, error->message);
    return 1;
  }

  /* 3. script path */
  lua_pushstring (L, script);
  return 3;
}

static void
wp_lua_scripting_enable_package_searcher (lua_State *L)
{
  /* table.insert(package.searchers, 2, wp_lua_scripting_package_searcher) */
  lua_getglobal (L, "table");
  lua_getfield (L, -1, "insert");
  lua_remove (L, -2);
  lua_getglobal (L, "package");
  lua_getfield (L, -1, "searchers");
  lua_remove (L, -2);
  lua_pushinteger (L, 2);
  lua_pushcfunction (L, wp_lua_scripting_package_searcher);
  lua_call (L, 3, 0);
}

static void wp_lua_scripting_component_loader_init (WpComponentLoaderInterface * iface);

G_DECLARE_FINAL_TYPE (WpLuaScriptingPlugin, wp_lua_scripting_plugin,
                      WP, LUA_SCRIPTING_PLUGIN, WpPlugin)
G_DEFINE_TYPE_WITH_CODE (WpLuaScriptingPlugin, wp_lua_scripting_plugin,
                         WP_TYPE_PLUGIN, G_IMPLEMENT_INTERFACE (
                            WP_TYPE_COMPONENT_LOADER,
                            wp_lua_scripting_component_loader_init))

static void
wp_lua_scripting_plugin_init (WpLuaScriptingPlugin * self)
{
}

static void
wp_lua_scripting_plugin_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpLuaScriptingPlugin * self = WP_LUA_SCRIPTING_PLUGIN (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  WpCore *export_core;

  /* init lua engine */
  self->L = wplua_new ();

  lua_pushliteral (self->L, "wireplumber_core");
  lua_pushlightuserdata (self->L, core);
  lua_settable (self->L, LUA_REGISTRYINDEX);

  /* initialize secondary connection to pipewire */
  export_core = g_object_get_data (G_OBJECT (core), "wireplumber.export-core");
  if (export_core) {
    lua_pushliteral (self->L, "wireplumber_export_core");
    wplua_pushobject (self->L, g_object_ref (export_core));
    lua_settable (self->L, LUA_REGISTRYINDEX);
  }

  wp_lua_scripting_api_init (self->L);
  wp_lua_scripting_enable_package_searcher (self->L);
  wplua_enable_sandbox (self->L, WP_LUA_SANDBOX_ISOLATE_ENV);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_lua_scripting_plugin_disable (WpPlugin * plugin)
{
  WpLuaScriptingPlugin * self = WP_LUA_SCRIPTING_PLUGIN (plugin);

  g_clear_pointer (&self->L, wplua_unref);
}

static gboolean
wp_lua_scripting_plugin_supports_type (WpComponentLoader * cl,
    const gchar * type)
{
  return g_str_equal (type, "script/lua");
}

static gchar *
find_script (const gchar * script, WpCore *core)
{
  g_autoptr (WpProperties) p = wp_core_get_properties (core);
  const gchar *str = wp_properties_get (p, "wireplumber.daemon");
  gboolean daemon = !g_strcmp0 (str, "true");

  if ((!daemon || g_path_is_absolute (script)) &&
      g_file_test (script, G_FILE_TEST_IS_REGULAR))
    return g_strdup (script);

  return wp_find_file (WP_LOOKUP_DIR_ENV_DATA |
                       WP_LOOKUP_DIR_ENV_TEST_SRCDIR |
                       WP_LOOKUP_DIR_XDG_CONFIG_HOME |
                       WP_LOOKUP_DIR_ETC |
                       WP_LOOKUP_DIR_PREFIX_SHARE,
                       script, "scripts");
}

static void
on_script_loaded (WpObject *object, GAsyncResult *res, gpointer data)
{
  g_autoptr (GTask) task = G_TASK (data);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (object, res, &error)) {
    g_task_return_error (task, g_steal_pointer (&error));
    return;
  }

  g_task_return_pointer (task, g_object_ref (object), g_object_unref);
}

static void
wp_lua_scripting_plugin_load (WpComponentLoader * cl, WpCore * core,
    const gchar * component, const gchar * type, WpSpaJson * args,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpLuaScriptingPlugin * self = WP_LUA_SCRIPTING_PLUGIN (cl);
  g_autoptr (GTask) task = task = g_task_new (core, cancellable, callback, data);
  g_autofree gchar *filepath = NULL;
  g_autofree gchar *pluginname = NULL;
  g_autoptr (WpPlugin) script = NULL;

  /* make sure the component loader is activated */
  if (!self->L) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
        "Lua script component loader cannot load Lua scripts if not enabled");
    return;
  }

  /* make sure the type is supported */
  if (!g_str_equal (type, "script/lua")) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
        "Could not load script '%s' as its type is not 'script/lua'",
        component);
    return;
  }

  /* find the script */
  filepath = find_script (component, core);
  if (!filepath) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
        "Could not locate script '%s'", component);
    return;
  }

  pluginname = g_strdup_printf ("script:%s", component);

  script = g_object_new (WP_TYPE_LUA_SCRIPT,
      "core", core,
      "name", pluginname,
      "lua-engine", self->L,
      "filename", filepath,
      "arguments", args,
      NULL);

  /* register the script */
  wp_plugin_register (g_object_ref (script));

  /* enable script */
  wp_object_activate (WP_OBJECT (script), WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_script_loaded, g_object_ref (task));
}

static void
wp_lua_scripting_plugin_class_init (WpLuaScriptingPluginClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_lua_scripting_plugin_enable;
  plugin_class->disable = wp_lua_scripting_plugin_disable;
}

static void
wp_lua_scripting_component_loader_init (WpComponentLoaderInterface * iface)
{
  iface->supports_type = wp_lua_scripting_plugin_supports_type;
  iface->load = wp_lua_scripting_plugin_load;
}

WP_PLUGIN_EXPORT GObject *
wireplumber__module_init (WpCore * core, WpSpaJson * args, GError ** error)
{
  return G_OBJECT (g_object_new (wp_lua_scripting_plugin_get_type (),
      "name", "lua-scripting",
      "core", core,
      NULL));
}
