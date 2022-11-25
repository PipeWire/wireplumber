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

void wp_lua_scripting_api_init (lua_State *L);
gboolean wp_lua_scripting_load_configuration (const gchar * conf_file,
    WpCore * core, GError ** error);

struct _WpLuaScriptingPlugin
{
  WpComponentLoader parent;

  GPtrArray *scripts; /* element-type: WpPlugin* */
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

G_DECLARE_FINAL_TYPE (WpLuaScriptingPlugin, wp_lua_scripting_plugin,
                      WP, LUA_SCRIPTING_PLUGIN, WpComponentLoader)
G_DEFINE_TYPE (WpLuaScriptingPlugin, wp_lua_scripting_plugin,
               WP_TYPE_COMPONENT_LOADER)

static void
wp_lua_scripting_plugin_init (WpLuaScriptingPlugin * self)
{
  self->scripts = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
wp_lua_scripting_plugin_finalize (GObject * object)
{
  WpLuaScriptingPlugin * self = WP_LUA_SCRIPTING_PLUGIN (object);

  g_clear_pointer (&self->scripts, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_lua_scripting_plugin_parent_class)->finalize (object);
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
    wplua_pushobject (self->L, export_core);
    lua_settable (self->L, LUA_REGISTRYINDEX);
  }

  wp_lua_scripting_api_init (self->L);
  wp_lua_scripting_enable_package_searcher (self->L);
  wplua_enable_sandbox (self->L, WP_LUA_SANDBOX_ISOLATE_ENV);

  /* register scripts that were queued in for loading */
  for (guint i = 0; i < self->scripts->len; i++) {
    WpPlugin *script = g_ptr_array_index (self->scripts, i);
    g_object_set (script, "lua-engine", self->L, NULL);
    wp_plugin_register (g_object_ref (script));
  }
  g_ptr_array_set_size (self->scripts, 0);

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
  return (!g_strcmp0 (type, "script/lua") || !g_strcmp0 (type, "config/lua"));
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

static gboolean
wp_lua_scripting_plugin_load (WpComponentLoader * cl, const gchar * component,
    const gchar * type, GVariant * args, GError ** error)
{
  WpLuaScriptingPlugin * self = WP_LUA_SCRIPTING_PLUGIN (cl);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (cl));

  /* interpret component as a script */
  if (!g_strcmp0 (type, "script/lua")) {
    g_autofree gchar *filepath = NULL;
    g_autofree gchar *pluginname = NULL;
    g_autoptr (WpPlugin) script = NULL;

    if (g_file_test (component, G_FILE_TEST_EXISTS)) {
      /* dangling components come with full path */
      g_autofree gchar *filename = g_path_get_basename (component);
      filepath = g_strdup (component);
      pluginname = g_strdup_printf ("script:%s", filename);
    }
    else {
      filepath = find_script (component, core);
      pluginname = g_strdup_printf ("script:%s", component);
    }

    if (!filepath) {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
          "Could not locate script '%s'", component);
      return FALSE;
    }

    script = g_object_new (WP_TYPE_LUA_SCRIPT,
        "core", core,
        "name", pluginname,
        "filename", filepath,
        "arguments", args,
        NULL);

    if (self->L) {
      wp_debug_object (core, "loading script(%s) plugin name(%s)",
          filepath, pluginname);
      g_object_set (script, "lua-engine", self->L, NULL);
      wp_plugin_register (g_steal_pointer (&script));
    } else {
      /* keep in a list and delay registering until the plugin is enabled */
      wp_debug ("queuing script %s", filepath);
      g_ptr_array_add (self->scripts, g_steal_pointer (&script));
    }
    return TRUE;
  }
  /* interpret component as a configuration file */
  else if (!g_strcmp0 (type, "config/lua")) {
    return wp_lua_scripting_load_configuration (component, core, error);
  }

  g_return_val_if_reached (FALSE);
}

static void
wp_lua_scripting_plugin_class_init (WpLuaScriptingPluginClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;
  WpComponentLoaderClass *cl_class = (WpComponentLoaderClass *) klass;

  object_class->finalize = wp_lua_scripting_plugin_finalize;

  plugin_class->enable = wp_lua_scripting_plugin_enable;
  plugin_class->disable = wp_lua_scripting_plugin_disable;

  cl_class->supports_type = wp_lua_scripting_plugin_supports_type;
  cl_class->load = wp_lua_scripting_plugin_load;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_lua_scripting_plugin_get_type (),
          "name", "lua-scripting",
          "core", core,
          NULL));
  return TRUE;
}
