/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <wplua/wplua.h>

struct _WpLuaScriptingEngine
{
  GObject parent;
  lua_State *L;
};

enum {
  SIGNAL_INIT_LUA_CONTEXT,
  N_SIGNALS,
};

static guint signals[N_SIGNALS] = {0};

static void wp_lua_scripting_engine_parser_iface_init (WpConfigParserInterface * iface);

G_DECLARE_FINAL_TYPE (WpLuaScriptingEngine, wp_lua_scripting_engine,
                      WP, LUA_SCRIPTING_ENGINE, GObject)
G_DEFINE_TYPE_WITH_CODE (WpLuaScriptingEngine, wp_lua_scripting_engine,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (WP_TYPE_CONFIG_PARSER,
                           wp_lua_scripting_engine_parser_iface_init))

static void
wp_lua_scripting_engine_init (WpLuaScriptingEngine * self)
{
}

static void
wp_lua_scripting_engine_finalize (GObject * object)
{
  WpLuaScriptingEngine * self = WP_LUA_SCRIPTING_ENGINE (object);

  g_clear_pointer (&self->L, wplua_free);

  G_OBJECT_CLASS (wp_lua_scripting_engine_parent_class)->finalize (object);
}

static void
wp_lua_scripting_engine_class_init (WpLuaScriptingEngineClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;

  object_class->finalize = wp_lua_scripting_engine_finalize;

  signals[SIGNAL_INIT_LUA_CONTEXT] = g_signal_new ("init-lua-context",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static gboolean
wp_lua_scripting_engine_add_file (WpConfigParser * parser, const gchar * file)
{
  WpLuaScriptingEngine * self = WP_LUA_SCRIPTING_ENGINE (parser);
  g_autoptr (GError) error = NULL;

  if (!wplua_load_path (self->L, file, &error)) {
    wp_warning_object (self, "%s", error->message);
    if (error->domain != WP_DOMAIN_LUA || error->code != WP_LUA_ERROR_RUNTIME)
      return FALSE;
  }
  return TRUE;
}

static void
wp_lua_scripting_engine_reset (WpConfigParser * parser)
{
  WpLuaScriptingEngine * self = WP_LUA_SCRIPTING_ENGINE (parser);

  g_clear_pointer (&self->L, wplua_free);
  self->L = wplua_new ();
  g_signal_emit (self, signals[SIGNAL_INIT_LUA_CONTEXT], 0, self->L);
  wplua_enable_sandbox (self->L);
}

static void
wp_lua_scripting_engine_parser_iface_init (WpConfigParserInterface * iface)
{
  iface->add_file = wp_lua_scripting_engine_add_file;
  iface->reset = wp_lua_scripting_engine_reset;
}
