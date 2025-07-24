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

WP_LOG_TOPIC (log_topic_wplua, "wplua")

#define URI_SANDBOX "resource:///org/freedesktop/pipewire/wireplumber/wplua/sandbox.lua"

extern void _wplua_register_resource (void);

typedef struct {
  guint64 refcount;
} WpLuaExtraData;

G_DEFINE_QUARK (wplua, wp_domain_lua);

static WpLuaExtraData *
wplua_getextradata (lua_State *L)
{
  WpLuaExtraData *data;

  G_STATIC_ASSERT(LUA_EXTRASPACE >= sizeof(data));
  memcpy (&data, lua_getextraspace (L), sizeof(data));
  return data;
}

static void
_wplua_openlibs (lua_State *L)
{
  /* http://www.lua.org/manual/5.3/manual.html#luaL_requiref
   * http://www.lua.org/source/5.3/linit.c.html */
  static const luaL_Reg loadedlibs[] = {
    {"_G", luaopen_base},
    {LUA_LOADLIBNAME, luaopen_package},
    {LUA_COLIBNAME, luaopen_coroutine},
    {LUA_TABLIBNAME, luaopen_table},
    /* {LUA_IOLIBNAME, luaopen_io}, */
    {LUA_OSLIBNAME, luaopen_os},
    {LUA_STRLIBNAME, luaopen_string},
    {LUA_MATHLIBNAME, luaopen_math},
    {LUA_UTF8LIBNAME, luaopen_utf8},
    {LUA_DBLIBNAME, luaopen_debug},
    {NULL, NULL}
  };
  const luaL_Reg *lib;

  for (lib = loadedlibs; lib->func; lib++) {
    luaL_requiref (L, lib->name, lib->func, 1);
    lua_pop (L, 1);
  }
}

static int
_wplua_errhandler (lua_State *L)
{
  luaL_traceback (L, L, NULL, 1);
  wp_warning ("%s\n%s", lua_tostring (L, -2), lua_tostring (L, -1));
  lua_pop (L, 2);
  return 0;
}

int
_wplua_pcall (lua_State *L, int nargs, int nret)
{
  int slots = lua_gettop (L);
  int ret = LUA_OK;
  /* 1 stack slot needed for error handler. */
  int hpos, stack_slots = 1;

  if (nargs < 0)
    g_error ("negative number of arguments");
  /* Need nargs + 1 stack slots for function and its arguments. */
  if (slots <= nargs)
    g_error ("not enough stack slots for arguments and function");
  if (nret != LUA_MULTRET) {
    if (nret - nargs > 1) {
      /* Need more stack slots: 1 for the error handler and (nret - (nargs + 1))
       * for the return values (after popping the function and its arguments). */
      stack_slots = nret - nargs;
    } else if (nret < 0)
      g_error ("negative number of return values");
  }
  if (!lua_checkstack (L, stack_slots)) {
    wp_critical ("_wplua_pcall: cannot grow Lua stack");
    return LUA_ERRMEM;
  }

  hpos = slots - nargs;
  lua_pushcfunction (L, _wplua_errhandler);
  lua_insert (L, hpos);

  ret = lua_pcall (L, nargs, nret, hpos);
  switch (ret) {
  case LUA_ERRMEM:
    wp_critical ("not enough memory");
    break;
  case LUA_ERRERR:
    wp_critical ("error running the message handler");
    break;
  default:
    break;
  }

  lua_remove (L, hpos);
  return ret;
}

lua_State *
wplua_new (void)
{
  static gboolean resource_registered = FALSE;
  lua_State *L = luaL_newstate ();

  if (L == NULL)
    g_error ("cannot create Lua state");
  wp_debug ("initializing lua_State %p", L);
  WpLuaExtraData *extradata = g_malloc(sizeof(*extradata));
  extradata->refcount = 1;
  memcpy (lua_getextraspace (L), &extradata, sizeof(extradata));

  if (!resource_registered) {
    _wplua_register_resource ();
    resource_registered = TRUE;
  }

  _wplua_openlibs (L);
  _wplua_init_gboxed (L);
  _wplua_init_gobject (L);
  _wplua_init_closure (L);

  {
    GHashTable *t = g_hash_table_new (g_direct_hash, g_direct_equal);
    lua_pushliteral (L, "wplua_vtables");
    wplua_pushboxed (L, G_TYPE_HASH_TABLE, t);
    lua_settable (L, LUA_REGISTRYINDEX);
  }

  return L;
}

lua_State *
wplua_ref (lua_State *L)
{
  WpLuaExtraData *data = wplua_getextradata(L);

  if (data->refcount < 1 || data->refcount == UINT64_MAX)
    g_error ("bad refcount");
  else {
    data->refcount++;
    return L;
  }
}

void
wplua_unref (lua_State * L)
{
  WpLuaExtraData *data = wplua_getextradata(L);

  if (data->refcount < 1)
    g_error ("bad refcount");
  else if (data->refcount > 1)
    data->refcount--;
  else {
    wp_debug ("closing lua_State %p", L);
    g_free (data);
    lua_close (L);
  }
}

void
wplua_enable_sandbox (lua_State * L, WpLuaSandboxFlags flags)
{
  g_autoptr (GError) error = NULL;
  wp_debug ("enabling Lua sandbox");

  if (!wplua_load_uri (L, URI_SANDBOX, &error)) {
    wp_critical ("Failed to load sandbox: %s", error->message);
    return;
  }

  lua_newtable (L);
  lua_pushliteral (L, "isolate_env");
  lua_pushboolean (L, (flags & WP_LUA_SANDBOX_ISOLATE_ENV));
  lua_settable (L, -3);

  if (!wplua_pcall (L, 1, 0, &error)) {
    wp_critical ("Failed to load sandbox: %s", error->message);
  }
}

int
wplua_push_sandbox (lua_State * L)
{
  return (lua_getglobal (L, "sandbox") == LUA_TFUNCTION) ? 1 : 0;
}

void
wplua_register_type_methods (lua_State * L, GType type,
    lua_CFunction constructor, const luaL_Reg * methods)
{
  g_return_if_fail (L != NULL);
  g_return_if_fail (G_TYPE_FUNDAMENTAL (type) == G_TYPE_OBJECT ||
                    G_TYPE_FUNDAMENTAL (type) == G_TYPE_BOXED ||
                    G_TYPE_FUNDAMENTAL (type) == G_TYPE_INTERFACE);

  /* register methods */
  if (methods) {
    GHashTable *vtables;

    lua_pushliteral (L, "wplua_vtables");
    lua_gettable (L, LUA_REGISTRYINDEX);
    vtables = wplua_toboxed (L, -1);
    lua_pop (L, 1);

    wp_debug ("Registering methods for '%s'", g_type_name (type));

    if (G_UNLIKELY (g_hash_table_contains (vtables, GUINT_TO_POINTER (type)))) {
      wp_critical ("type '%s' was already registered", g_type_name (type));
      return;
    }

    g_hash_table_insert (vtables, GUINT_TO_POINTER (type), (gpointer) methods);
  }

  /* register constructor */
  if (constructor) {
    luaL_Buffer b;

    wp_debug ("Registering class for '%s'", g_type_name (type));

    luaL_buffinit (L, &b);
    luaL_addstring (&b, g_type_name (type));
    luaL_addchar (&b, '_');
    luaL_addstring (&b, "new");
    luaL_pushresult (&b);
    lua_pushcfunction (L, constructor);
    lua_setglobal (L, lua_tostring (L, -2));
    lua_pop (L, 1);
  }
}

static gboolean
_wplua_load_buffer (lua_State * L, const gchar *buf, gsize size,
    const gchar * name, GError **error)
{
  int ret;

  /* skip shebang, if present */
  if (g_str_has_prefix (buf, "#!/")) {
    const char *tmp = strchr (buf, '\n');
    size -= (tmp - buf);
    buf = tmp;
  }

  ret = luaL_loadbuffer (L, buf, size, name);
  if (ret != LUA_OK) {
    g_set_error (error, WP_DOMAIN_LUA, WP_LUA_ERROR_COMPILATION,
        "Failed to compile: %s", lua_tostring (L, -1));
    lua_pop (L, 1);
    return FALSE;
  }
  return TRUE;
}

gboolean
wplua_load_buffer (lua_State * L, const gchar *buf, gsize size, GError **error)
{
  g_return_val_if_fail (L != NULL, FALSE);
  g_return_val_if_fail (buf != NULL, FALSE);
  g_return_val_if_fail (size != 0, FALSE);

  g_autofree gchar *name =
      g_strdup_printf ("buffer@%p;size=%" G_GSIZE_FORMAT, buf, size);
  return _wplua_load_buffer (L, buf, size, name, error);
}

gboolean
wplua_load_uri (lua_State * L, const gchar *uri, GError **error)
{
  g_autoptr (GFile) file = NULL;
  g_autoptr (GBytes) bytes = NULL;
  g_autoptr (GError) err = NULL;
  g_autofree gchar *name = NULL;
  gconstpointer data;
  gsize size;

  g_return_val_if_fail (L != NULL, FALSE);
  g_return_val_if_fail (uri != NULL, FALSE);

  file = g_file_new_for_uri (uri);
  if (!(bytes = g_file_load_bytes (file, NULL, NULL, &err))) {
    g_propagate_prefixed_error (error, err, "Failed to load '%s':", uri);
    err = NULL;
    return FALSE;
  }

  name = g_path_get_basename (uri);
  data = g_bytes_get_data (bytes, &size);
  return _wplua_load_buffer (L, data, size, name, error);
}

gboolean
wplua_load_path (lua_State * L, const gchar *path, GError **error)
{
  g_autofree gchar *abs_path = NULL;
  g_autofree gchar *uri = NULL;

  g_return_val_if_fail (L != NULL, FALSE);
  g_return_val_if_fail (path != NULL, FALSE);

  if (!g_path_is_absolute (path)) {
    g_autofree gchar *cwd = g_get_current_dir ();
    abs_path = g_build_filename (cwd, path, NULL);
  }

  if (!(uri = g_filename_to_uri (abs_path ? abs_path : path, NULL, error)))
    return FALSE;

  return wplua_load_uri (L, uri, error);
}

gboolean
wplua_pcall (lua_State * L, int nargs, int nres, GError **error)
{
  int ret = _wplua_pcall (L, nargs, nres);
  if (ret != LUA_OK) {
    g_set_error (error, WP_DOMAIN_LUA, WP_LUA_ERROR_RUNTIME,
        "Lua runtime error");
    return FALSE;
  }
  return TRUE;
}
