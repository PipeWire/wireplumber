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

#define WP_TYPE_LUA_SCRIPTING_ENGINE \
    (wp_lua_scripting_engine_get_type ())
GType wp_lua_scripting_engine_get_type ();
void wp_lua_scripting_api_init (lua_State *L);

struct _WpLuaScriptingPlugin
{
  WpPlugin parent;

  /* properties */
  gchar *profile;

  /* data */
  WpCore *export_core;
  gchar *config_ext;

  WpConfiguration *config;
};

enum {
  PROP_0,
  PROP_PROFILE,
};

G_DECLARE_FINAL_TYPE (WpLuaScriptingPlugin, wp_lua_scripting_plugin,
                      WP, LUA_SCRIPTING_PLUGIN, WpPlugin)
G_DEFINE_TYPE (WpLuaScriptingPlugin, wp_lua_scripting_plugin, WP_TYPE_PLUGIN)

static void
wp_lua_scripting_plugin_init (WpLuaScriptingPlugin * self)
{
}

static void
wp_lua_scripting_plugin_finalize (GObject * object)
{
  WpLuaScriptingPlugin * self = WP_LUA_SCRIPTING_PLUGIN (object);

  g_clear_pointer (&self->profile, g_free);

  G_OBJECT_CLASS (wp_lua_scripting_plugin_parent_class)->finalize (object);
}

static void
wp_lua_scripting_plugin_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpLuaScriptingPlugin * self = WP_LUA_SCRIPTING_PLUGIN (object);

  switch (property_id) {
  case PROP_PROFILE:
    g_clear_pointer (&self->profile, g_free);
    self->profile = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_lua_scripting_plugin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpLuaScriptingPlugin * self = WP_LUA_SCRIPTING_PLUGIN (object);

  switch (property_id) {
  case PROP_PROFILE:
    g_value_set_string (value, self->profile);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_lua_scripting_plugin_init_lua_ctx (WpConfigParser * engine, lua_State * L,
    WpLuaScriptingPlugin * self)
{
  g_autoptr (WpCore) core = wp_plugin_get_core (WP_PLUGIN (self));

  lua_pushliteral (L, "wireplumber_core");
  lua_pushlightuserdata (L, core);
  lua_settable (L, LUA_REGISTRYINDEX);

  lua_pushliteral (L, "wireplumber_export_core");
  lua_pushlightuserdata (L, self->export_core);
  lua_settable (L, LUA_REGISTRYINDEX);

  wp_lua_scripting_api_init (L);
}

static void
wp_lua_scripting_plugin_activate (WpPlugin * plugin)
{
  WpLuaScriptingPlugin * self = WP_LUA_SCRIPTING_PLUGIN (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
  g_autoptr (WpConfigParser) engine = NULL;

  self->config = wp_configuration_get_instance (core);

  /* initialize secondary connection to pipewire */
  self->export_core = wp_core_clone (core);
  wp_core_update_properties (self->export_core, wp_properties_new (
        PW_KEY_APP_NAME, "WirePlumber (export)",
        NULL));
  if (!wp_core_connect (self->export_core)) {
    wp_warning_object (self, "failed to connect export core");
    return;
  }

  /* load the lua scripts & execute them via the engine */
  self->config_ext = g_strdup_printf ("%s/lua", self->profile);
  wp_configuration_add_extension (self->config, self->config_ext,
      WP_TYPE_LUA_SCRIPTING_ENGINE);

  engine = wp_configuration_get_parser (self->config, self->config_ext);
  g_signal_connect_object (engine, "init-lua-context",
      G_CALLBACK (wp_lua_scripting_plugin_init_lua_ctx), self, 0);

  wp_configuration_reload (self->config, self->config_ext);
}

static void
wp_lua_scripting_plugin_deactivate (WpPlugin * plugin)
{
  WpLuaScriptingPlugin * self = WP_LUA_SCRIPTING_PLUGIN (plugin);

  if (self->config && self->config_ext)
    wp_configuration_remove_extension (self->config, self->config_ext);
  g_clear_object (&self->config);
  g_clear_pointer (&self->config_ext, g_free);
  g_clear_object (&self->export_core);
}

static void
wp_lua_scripting_plugin_class_init (WpLuaScriptingPluginClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_lua_scripting_plugin_finalize;
  object_class->set_property = wp_lua_scripting_plugin_set_property;
  object_class->get_property = wp_lua_scripting_plugin_get_property;

  plugin_class->activate = wp_lua_scripting_plugin_activate;
  plugin_class->deactivate = wp_lua_scripting_plugin_deactivate;

  g_object_class_install_property(object_class, PROP_PROFILE,
      g_param_spec_string ("profile", "profile",
          "The configuration profile", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  const gchar *profile;

  if (!g_variant_lookup (args, "profile", "&s", &profile)) {
    wp_warning_object (module, "module-lua-scripting requires a 'profile'");
    return;
  }

  wp_plugin_register (g_object_new (wp_lua_scripting_plugin_get_type (),
          "name", "lua-scripting",
          "module", module,
          "profile", profile,
          NULL));
}
