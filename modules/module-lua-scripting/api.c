/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <glib/gstdio.h>
#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <wplua/wplua.h>

#define URI_API "resource:///org/freedesktop/pipewire/wireplumber/m-lua-scripting/api.lua"

void wp_lua_scripting_pod_init (lua_State *L);

/* helpers */

static WpCore *
get_wp_core (lua_State *L)
{
  WpCore *core = NULL;
  lua_pushliteral (L, "wireplumber_core");
  lua_gettable (L, LUA_REGISTRYINDEX);
  core = lua_touserdata (L, -1);
  lua_pop (L, 1);
  return core;
}

static WpCore *
get_wp_export_core (lua_State *L)
{
  WpCore *core = NULL;
  lua_pushliteral (L, "wireplumber_export_core");
  lua_gettable (L, LUA_REGISTRYINDEX);
  core = lua_touserdata (L, -1);
  lua_pop (L, 1);
  return core ? core : get_wp_core(L);
}

/* GLib */

static int
glib_get_monotonic_time (lua_State *L)
{
  lua_pushinteger (L, g_get_monotonic_time ());
  return 1;
}

static int
glib_get_real_time (lua_State *L)
{
  lua_pushinteger (L, g_get_real_time ());
  return 1;
}

static gboolean
access_parse_mode (const gchar * mode_str, gint *mode)
{
  *mode = 0;

  if (!mode_str)
    return FALSE;
  else {
    for (guint i = 0; i < strlen (mode_str); i++) {
      switch (mode_str[i]) {
        case 'r': *mode |= R_OK; break;
        case 'w': *mode |= W_OK; break;
        case 'x': *mode |= X_OK; break;
        case 'f': *mode |= F_OK; break;
        case '-': break;
        default:
          return FALSE;
      }
    }
  }
  return TRUE;
}

static int
glib_access (lua_State *L)
{
  const gchar *filename = luaL_checkstring (L, 1);
  int mode = 0;
  if (!access_parse_mode (luaL_checkstring (L, 2), &mode))
      luaL_error (L, "invalid mode string: '%s'", lua_tostring (L, 2));
  lua_pushboolean (L, g_access (filename, mode) >= 0);
  return 1;
}

static const luaL_Reg glib_methods[] = {
  { "get_monotonic_time", glib_get_monotonic_time },
  { "get_real_time", glib_get_real_time },
  { "access", glib_access },
  { NULL, NULL }
};

/* GSource */

static int
source_destroy (lua_State *L)
{
  GSource *source = wplua_checkboxed (L, 1, G_TYPE_SOURCE);
  g_source_destroy (source);
  return 0;
}

static const luaL_Reg source_methods[] = {
  { "destroy", source_destroy },
  { NULL, NULL }
};

/* WpCore */

static int
core_get_info (lua_State *L)
{
  WpCore * core = get_wp_core (L);
  g_autoptr (WpProperties) p = wp_core_get_remote_properties (core);

  lua_newtable (L);
  lua_pushinteger (L, wp_core_get_remote_cookie (core));
  lua_setfield (L, -2, "cookie");
  lua_pushstring (L, wp_core_get_remote_name (core));
  lua_setfield (L, -2, "name");
  lua_pushstring (L, wp_core_get_remote_user_name (core));
  lua_setfield (L, -2, "user_name");
  lua_pushstring (L, wp_core_get_remote_host_name (core));
  lua_setfield (L, -2, "host_name");
  lua_pushstring (L, wp_core_get_remote_version (core));
  lua_setfield (L, -2, "version");
  wplua_properties_to_table (L, p);
  lua_setfield (L, -2, "properties");
  return 1;
}

static int
core_idle_add (lua_State *L)
{
  GSource *source = NULL;
  luaL_checktype (L, 1, LUA_TFUNCTION);
  wp_core_idle_add_closure (get_wp_core (L), &source,
      wplua_function_to_closure (L, 1));
  wplua_pushboxed (L, G_TYPE_SOURCE, source);
  return 1;
}

static int
core_timeout_add (lua_State *L)
{
  GSource *source = NULL;
  lua_Integer timeout_ms = luaL_checkinteger (L, 1);
  luaL_checktype (L, 2, LUA_TFUNCTION);
  wp_core_timeout_add_closure (get_wp_core (L), &source, timeout_ms,
      wplua_function_to_closure (L, 2));
  wplua_pushboxed (L, G_TYPE_SOURCE, source);
  return 1;
}

static void
on_core_done (WpCore * core, GAsyncResult * res, GClosure * closure)
{
  g_autoptr (GError) error = NULL;
  GValue val = G_VALUE_INIT;
  int n_vals = 0;

  if (!wp_core_sync_finish (core, res, &error)) {
    g_value_init (&val, G_TYPE_STRING);
    g_value_set_string (&val, error->message);
    n_vals = 1;
  }
  g_closure_invoke (closure, NULL, n_vals, &val, NULL);
  g_value_unset (&val);
  g_closure_invalidate (closure);
  g_closure_unref (closure);
}

static int
core_sync (lua_State *L)
{
  luaL_checktype (L, 1, LUA_TFUNCTION);
  GClosure * closure = wplua_function_to_closure (L, 1);
  g_closure_sink (g_closure_ref (closure));
  wp_core_sync (get_wp_core (L), NULL, (GAsyncReadyCallback) on_core_done,
      closure);
  return 0;
}

static gboolean
core_disconnect (WpCore * core)
{
  wp_core_disconnect (core);
  return G_SOURCE_REMOVE;
}

static int
core_quit (lua_State *L)
{
  WpCore * core = get_wp_core (L);
  g_autoptr (WpProperties) p = wp_core_get_properties (core);
  const gchar *daemon = wp_properties_get (p, "wireplumber.daemon");
  if (!g_strcmp0 (daemon, "true")) {
    wp_warning ("script attempted to quit, but the engine is "
        "running in the wireplumber daemon; ignoring");
    return 0;
  }

  /* wp_core_disconnect() will immediately destroy the lua plugin
     and the lua engine, so we cannot call it directly */
  wp_core_idle_add (core, NULL, G_SOURCE_FUNC (core_disconnect), core, NULL);
  return 0;
}

#include "require.c"

static int
core_require_api (lua_State *L)
{
  WpCore * core = get_wp_core (L);
  g_autoptr (WpProperties) p = wp_core_get_properties (core);
  const gchar *daemon = wp_properties_get (p, "wireplumber.daemon");
  if (!g_strcmp0 (daemon, "true")) {
    wp_warning ("script attempted to load an API module, but the engine is "
        "running in the wireplumber daemon; ignoring");
    return 0;
  }
  return wp_require_api_transition_new_from_lua (L, core);
}

static const luaL_Reg core_funcs[] = {
  { "get_info", core_get_info },
  { "idle_add", core_idle_add },
  { "timeout_add", core_timeout_add },
  { "sync", core_sync },
  { "quit", core_quit },
  { "require_api", core_require_api },
  { NULL, NULL }
};

/* WpLog */

static int
log_log (lua_State *L, GLogLevelFlags lvl)
{
  lua_Debug ar = {0};
  const gchar *message, *tmp;
  gchar domain[25];
  gchar line_str[11];
  gconstpointer instance = NULL;
  GType type = G_TYPE_INVALID;
  int index = 1;

  if (!wp_log_level_is_enabled (lvl))
    return 0;

  g_warn_if_fail (lua_getstack (L, 1, &ar) == 1);
  g_warn_if_fail (lua_getinfo (L, "nSl", &ar) == 1);

  if (wplua_isobject (L, 1, G_TYPE_OBJECT)) {
    instance = wplua_toobject (L, 1);
    type = G_TYPE_FROM_INSTANCE (instance);
    index++;
  }
  else if (wplua_isboxed (L, 1, G_TYPE_BOXED)) {
    instance = wplua_toboxed (L, 1);
    type = wplua_gvalue_userdata_type (L, 1);
    index++;
  }

  message = luaL_checkstring (L, index);
  tmp = ar.source ? g_strrstr (ar.source, ".lua") : NULL;
  snprintf (domain, 25, "script/%.*s",
      tmp ? MIN((gint)(tmp - ar.source), 17) : 17,
      ar.source);
  snprintf (line_str, 11, "%d", ar.currentline);
  ar.name = ar.name ? ar.name : "chunk";

  wp_log_structured_standard (domain, lvl,
      ar.source, line_str, ar.name, type, instance, "%s", message);
  return 0;
}

static int
log_warning (lua_State *L) { return log_log (L, G_LOG_LEVEL_WARNING); }

static int
log_message (lua_State *L) { return log_log (L, G_LOG_LEVEL_MESSAGE); }

static int
log_info (lua_State *L) { return log_log (L, G_LOG_LEVEL_INFO); }

static int
log_debug (lua_State *L) { return log_log (L, G_LOG_LEVEL_DEBUG); }

static int
log_trace (lua_State *L) { return log_log (L, WP_LOG_LEVEL_TRACE); }

static const luaL_Reg log_funcs[] = {
  { "warning", log_warning },
  { "message", log_message },
  { "info", log_info },
  { "debug", log_debug },
  { "trace", log_trace },
  { NULL, NULL }
};

/* WpPlugin */

static int
plugin_find (lua_State *L)
{
  const char *name = luaL_checkstring (L, 1);
  WpPlugin *plugin = wp_plugin_find (get_wp_core (L), name);
  if (plugin)
    wplua_pushobject (L, plugin);
  else
    lua_pushnil (L);
  return 1;
}

static const luaL_Reg plugin_funcs[] = {
  { "find", plugin_find },
  { NULL, NULL }
};

/* WpObject */

static void
object_activate_done (WpObject *o, GAsyncResult * res, GClosure * closure)
{
  g_autoptr (GError) error = NULL;
  GValue val[2] = { G_VALUE_INIT, G_VALUE_INIT };
  int n_vals = 1;

  if (!wp_object_activate_finish (o, res, &error)) {
    wp_message_object (o, "%s", error->message);
    if (closure) {
      g_value_init (&val[1], G_TYPE_STRING);
      g_value_set_string (&val[1], error->message);
      n_vals = 2;
    }
  }
  if (closure) {
    g_value_init_from_instance (&val[0], o);
    g_closure_invoke (closure, NULL, n_vals, val, NULL);
    g_value_unset (&val[0]);
    g_value_unset (&val[1]);
    g_closure_invalidate (closure);
    g_closure_unref (closure);
  }
}

static int
object_activate (lua_State *L)
{
  WpObject *o = wplua_checkobject (L, 1, WP_TYPE_OBJECT);
  WpObjectFeatures features = luaL_checkinteger (L, 2);
  GClosure * closure = luaL_opt (L, wplua_checkclosure, 3, NULL);
  if (closure)
    g_closure_sink (g_closure_ref (closure));
  wp_object_activate (o, features, NULL,
      (GAsyncReadyCallback) object_activate_done, closure);
  return 0;
}

static int
object_deactivate (lua_State *L)
{
  WpObject *o = wplua_checkobject (L, 1, WP_TYPE_OBJECT);
  WpObjectFeatures features = luaL_checkinteger (L, 2);
  wp_object_deactivate (o, features);
  return 0;
}

static int
object_get_active_features (lua_State *L)
{
  WpObject *o = wplua_checkobject (L, 1, WP_TYPE_OBJECT);
  WpObjectFeatures features = wp_object_get_active_features (o);
  lua_pushinteger (L, features);
  return 1;
}

static int
object_get_supported_features (lua_State *L)
{
  WpObject *o = wplua_checkobject (L, 1, WP_TYPE_OBJECT);
  WpObjectFeatures features = wp_object_get_supported_features (o);
  lua_pushinteger (L, features);
  return 1;
}

static const luaL_Reg object_methods[] = {
  { "activate", object_activate },
  { "deactivate", object_deactivate },
  { "get_active_features", object_get_active_features },
  { "get_supported_features", object_get_supported_features },
  { NULL, NULL }
};

/* WpProxy */

static int
proxy_get_interface_type (lua_State *L)
{
  WpProxy * p = wplua_checkobject (L, 1, WP_TYPE_PROXY);
  guint32 version = 0;
  const gchar *type = wp_proxy_get_interface_type (p, &version);
  lua_pushstring (L, type);
  lua_pushinteger (L, version);
  return 2;
}

static const luaL_Reg proxy_methods[] = {
  { "get_interface_type", proxy_get_interface_type },
  { NULL, NULL }
};

/* WpGlobalProxy */

static int
global_proxy_request_destroy (lua_State *L)
{
  WpGlobalProxy * p = wplua_checkobject (L, 1, WP_TYPE_GLOBAL_PROXY);
  wp_global_proxy_request_destroy (p);
  return 0;
}

static const luaL_Reg global_proxy_methods[] = {
  { "request_destroy", global_proxy_request_destroy },
  { NULL, NULL }
};

/* WpIterator */

static int
iterator_next (lua_State *L)
{
  WpIterator *it = wplua_checkboxed (L, 1, WP_TYPE_ITERATOR);
  g_auto (GValue) v = G_VALUE_INIT;
  if (it && wp_iterator_next (it, &v)) {
    return wplua_gvalue_to_lua (L, &v);
  } else {
    lua_pushnil (L);
    return 1;
  }
}

static int
push_wpiterator (lua_State *L, WpIterator *it)
{
  lua_pushcfunction (L, iterator_next);
  wplua_pushboxed (L, WP_TYPE_ITERATOR, it);
  return 2;
}

/* Metadata WpIterator */

static int
metadata_iterator_next (lua_State *L)
{
  WpIterator *it = wplua_checkboxed (L, 1, WP_TYPE_ITERATOR);
  g_auto (GValue) item = G_VALUE_INIT;
  if (wp_iterator_next (it, &item)) {
    guint32 s = 0;
    const gchar *k = NULL, *t = NULL, *v = NULL;
    wp_metadata_iterator_item_extract (&item, &s, &k, &t, &v);
    lua_pushinteger (L, s);
    lua_pushstring (L, k);
    lua_pushstring (L, t);
    lua_pushstring (L, v);
    return 4;
  } else {
    lua_pushnil (L);
    return 1;
  }
}

static int
push_metadata_wpiterator (lua_State *L, WpIterator *it)
{
  lua_pushcfunction (L, metadata_iterator_next);
  wplua_pushboxed (L, WP_TYPE_ITERATOR, it);
  return 2;
}

/* WpObjectInterest */

static GVariant *
constraint_value_to_variant (lua_State *L, int idx)
{
  switch (lua_type (L, idx)) {
  case LUA_TBOOLEAN:
    return g_variant_new_boolean (lua_toboolean (L, idx));
  case LUA_TSTRING:
    return g_variant_new_string (lua_tostring (L, idx));
  case LUA_TNUMBER:
    if (lua_isinteger (L, idx))
      return g_variant_new_int64 (lua_tointeger (L, idx));
    else
      return g_variant_new_double (lua_tonumber (L, idx));
  default:
    return NULL;
  }
}

static void
object_interest_new_add_constraint (lua_State *L, GType type,
    WpObjectInterest *interest)
{
  int constraint_idx;
  WpConstraintType ctype;
  const gchar *subject;
  WpConstraintVerb verb;
  GVariant *value = NULL;

  constraint_idx = lua_absindex (L, -1);

  /* verify this is a Constraint{} */
  if (lua_type (L, constraint_idx) != LUA_TTABLE) {
    luaL_error (L, "Interest: expected Constraint at index %d",
        lua_tointeger (L, -2));
  }

  if (luaL_getmetafield (L, constraint_idx, "__name") == LUA_TNIL ||
      g_strcmp0 (lua_tostring (L, -1), "Constraint") != 0) {
    luaL_error (L, "Interest: expected Constraint at index %d",
        lua_tointeger (L, -2));
  }
  lua_pop (L, 1);

  /* get the constraint type */
  lua_pushliteral (L, "type");
  if (lua_gettable (L, constraint_idx) == LUA_TNUMBER)
    ctype = lua_tointeger (L, -1);
  else
    ctype = WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY;
  lua_pop (L, 1);

  /* get t[1] (the subject) and t[2] (the verb) */
  lua_geti (L, constraint_idx, 1);
  subject = lua_tostring (L, -1);

  lua_geti (L, constraint_idx, 2);
  verb = lua_tostring (L, -1)[0];

  switch (verb) {
  case WP_CONSTRAINT_VERB_EQUALS:
  case WP_CONSTRAINT_VERB_NOT_EQUALS:
  case WP_CONSTRAINT_VERB_MATCHES: {
    lua_geti (L, constraint_idx, 3);
    value = constraint_value_to_variant (L, -1);
    if (G_UNLIKELY (!value))
      luaL_error (L, "Constraint: bad value type");
    break;
  }
  case WP_CONSTRAINT_VERB_IN_RANGE: {
    GVariant *values[2];
    lua_geti (L, constraint_idx, 3);
    lua_geti (L, constraint_idx, 4);
    values[0] = constraint_value_to_variant (L, -2);
    values[1] = constraint_value_to_variant (L, -1);
    if (G_UNLIKELY (!values[0] || !values[1])) {
      g_clear_pointer (&values[0], g_variant_unref);
      g_clear_pointer (&values[1], g_variant_unref);
      luaL_error (L, "Constraint: bad value type");
    }
    value = g_variant_new_tuple (values, 2);
    break;
  }
  case WP_CONSTRAINT_VERB_IN_LIST: {
    GPtrArray *values =
        g_ptr_array_new_with_free_func ((GDestroyNotify) g_variant_unref);
    int i = 3;
    while (lua_geti (L, constraint_idx, i++) != LUA_TNIL) {
      GVariant *tmp = constraint_value_to_variant (L, -1);
      if (G_UNLIKELY (!tmp)) {
        g_ptr_array_unref (values);
        luaL_error (L, "Constraint: bad value type");
      }
      g_ptr_array_add (values, g_variant_ref_sink (tmp));
      lua_pop (L, 1);
    }
    value = g_variant_new_tuple ((GVariant **) values->pdata, values->len);
    g_ptr_array_unref (values);
    break;
  }
  default:
    break;
  }

  wp_object_interest_add_constraint (interest, ctype, subject, verb, value);
  lua_settop (L, constraint_idx);
}

static GType
parse_gtype (const gchar *str)
{
  g_autofree gchar *typestr = NULL;
  GType res = G_TYPE_INVALID;

  g_return_val_if_fail (str, res);

  /* "device" -> "WpDevice" */
  typestr = g_strdup_printf ("Wp%s", str);
  if (typestr[2] != 0) {
    typestr[2] = g_ascii_toupper (typestr[2]);
    res = g_type_from_name (typestr);
  }

  return res;
}

static int
object_interest_new_index (lua_State *L, int idx, GType def_type)
{
  WpObjectInterest *interest = NULL;
  GType type = def_type;

  luaL_checktype (L, idx, LUA_TTABLE);

  /* type = "string" */
  lua_pushliteral (L, "type");
  if (lua_gettable (L, idx) == LUA_TSTRING) {
    type = parse_gtype (lua_tostring (L, -1));
    if (type == G_TYPE_INVALID)
      luaL_error (L, "Interest: unknown type '%s'", lua_tostring (L, -1));
  }
  else if (def_type == G_TYPE_INVALID)
    luaL_error (L, "Interest: expected 'type' as string");
  lua_pop (L, 1);

  interest = wp_object_interest_new_type (type);
  wplua_pushboxed (L, WP_TYPE_OBJECT_INTEREST, interest);

  /* add constraints */
  lua_pushnil (L);
  while (lua_next (L, idx)) {
    /* if the key isn't "type" */
    if (!(lua_type (L, -2) == LUA_TSTRING &&
          !g_strcmp0 ("type", lua_tostring (L, -2))))
      object_interest_new_add_constraint (L, type, interest);
    lua_pop (L, 1);
  }

  return 1;
}

static int
object_interest_new (lua_State *L)
{
  return object_interest_new_index (L, 1, G_TYPE_INVALID);
}

static int
object_interest_matches (lua_State *L)
{
  WpObjectInterest *interest = wplua_checkboxed (L, 1, WP_TYPE_OBJECT_INTEREST);
  gboolean matches = FALSE;

  if (wplua_isobject (L, 2, G_TYPE_OBJECT)) {
    matches = wp_object_interest_matches (interest, wplua_toobject (L, 2));
  }
  else if (lua_istable (L, 2)) {
    g_autoptr (WpProperties) props = wplua_table_to_properties (L, 2);
    matches = wp_object_interest_matches (interest, props);
  } else
    luaL_argerror (L, 2, "expected GObject or table");

  lua_pushboolean (L, matches);
  return 1;
}

static const luaL_Reg object_interest_methods[] = {
  { "matches", object_interest_matches },
  { NULL, NULL }
};

static WpObjectInterest *
get_optional_object_interest (lua_State *L, int idx, GType def_type)
{
  if (lua_isnoneornil (L, idx))
    return NULL;
  else if (lua_isuserdata (L, idx))
    return wplua_checkboxed (L, idx, WP_TYPE_OBJECT_INTEREST);
  else if (lua_istable (L, idx)) {
    object_interest_new_index (L, idx, def_type);
    return wplua_toboxed (L, -1);
  } else {
    luaL_error (L, "expected Interest or none/nil");
    return NULL;
  }
}

/* WpObjectManager */

static int
object_manager_new (lua_State *L)
{
  WpObjectManager *om;

  /* validate arguments */
  luaL_checktype (L, 1, LUA_TTABLE);

  /* push to Lua asap to have a way to unref in case of error */
  om = wp_object_manager_new ();
  wplua_pushobject (L, om);

  lua_pushnil (L);
  while (lua_next (L, 1)) {
    WpObjectInterest *interest =
        wplua_checkboxed (L, -1, WP_TYPE_OBJECT_INTEREST);
    wp_object_manager_add_interest_full (om, wp_object_interest_ref (interest));
    lua_pop (L, 1);
  }

  /* request all the features for Lua scripts to make their job easier */
  wp_object_manager_request_object_features (om,
      WP_TYPE_OBJECT, WP_OBJECT_FEATURES_ALL);

  return 1;
}

static int
object_manager_activate (lua_State *L)
{
  WpObjectManager *om = wplua_checkobject (L, 1, WP_TYPE_OBJECT_MANAGER);
  wp_core_install_object_manager (get_wp_core (L), om);
  return 0;
}

static int
object_manager_get_n_objects (lua_State *L)
{
  WpObjectManager *om = wplua_checkobject (L, 1, WP_TYPE_OBJECT_MANAGER);
  lua_pushinteger (L, wp_object_manager_get_n_objects (om));
  return 1;
}

static int
object_manager_iterate (lua_State *L)
{
  WpObjectManager *om = wplua_checkobject (L, 1, WP_TYPE_OBJECT_MANAGER);
  WpObjectInterest *oi = get_optional_object_interest (L, 2, G_TYPE_OBJECT);
  WpIterator *it = oi ?
      wp_object_manager_new_filtered_iterator_full (om,
          wp_object_interest_ref (oi)) :
      wp_object_manager_new_iterator (om);
  return push_wpiterator (L, it);
}

static int
object_manager_lookup (lua_State *L)
{
  WpObjectManager *om = wplua_checkobject (L, 1, WP_TYPE_OBJECT_MANAGER);
  WpObjectInterest *oi = get_optional_object_interest (L, 2, G_TYPE_OBJECT);
  WpObject *o = oi ?
      wp_object_manager_lookup_full (om, wp_object_interest_ref (oi)) :
      wp_object_manager_lookup (om, G_TYPE_OBJECT, NULL);
  if (o) {
    wplua_pushobject (L, o);
    return 1;
  }
  return 0;
}

static const luaL_Reg object_manager_methods[] = {
  { "activate", object_manager_activate },
  { "get_n_objects", object_manager_get_n_objects },
  { "iterate", object_manager_iterate },
  { "lookup", object_manager_lookup },
  { NULL, NULL }
};

/* WpMetadata */

static int
metadata_iterate (lua_State *L)
{
  WpMetadata *metadata = wplua_checkobject (L, 1, WP_TYPE_METADATA);
  lua_Integer subject = luaL_checkinteger (L, 2);
  WpIterator *it = wp_metadata_new_iterator (metadata, subject);
  return push_metadata_wpiterator (L, it);
}

static int
metadata_find (lua_State *L)
{
  WpMetadata *metadata = wplua_checkobject (L, 1, WP_TYPE_METADATA);
  lua_Integer subject = luaL_checkinteger (L, 2);
  const char *key = luaL_checkstring (L, 3), *v = NULL, *t = NULL;
  v = wp_metadata_find (metadata, subject, key, &t);
  lua_pushstring (L, v);
  lua_pushstring (L, t);
  return 2;
}

static int
metadata_set (lua_State *L)
{
  WpMetadata *metadata = wplua_checkobject (L, 1, WP_TYPE_METADATA);
  lua_Integer subject = luaL_checkinteger (L, 2);
  const char *key = luaL_opt (L, luaL_checkstring, 3, NULL);
  const char *type = luaL_opt (L, luaL_checkstring, 4, NULL);
  const char *value = luaL_opt (L, luaL_checkstring, 5, NULL);
  wp_metadata_set (metadata, subject, key, type, value);
  return 0;
}

static const luaL_Reg metadata_methods[] = {
  { "iterate", metadata_iterate },
  { "find", metadata_find },
  { "set", metadata_set },
  { NULL, NULL }
};

/* WpEndpoint */

static const luaL_Reg endpoint_methods[] = {
  { NULL, NULL }
};

/* Device */

static int
device_new (lua_State *L)
{
  const char *factory = luaL_checkstring (L, 1);
  WpProperties *properties = NULL;

  if (lua_type (L, 2) != LUA_TNONE) {
    luaL_checktype (L, 2, LUA_TTABLE);
    properties = wplua_table_to_properties (L, 2);
  }

  WpDevice *d = wp_device_new_from_factory (get_wp_export_core (L),
      factory, properties);
  if (d)
    wplua_pushobject (L, d);
  return d ? 1 : 0;
}

/* WpSpaDevice */

static int
spa_device_new (lua_State *L)
{
  const char *factory = luaL_checkstring (L, 1);
  WpProperties *properties = NULL;

  if (lua_type (L, 2) != LUA_TNONE) {
    luaL_checktype (L, 2, LUA_TTABLE);
    properties = wplua_table_to_properties (L, 2);
  }

  WpSpaDevice *d = wp_spa_device_new_from_spa_factory (get_wp_export_core (L),
      factory, properties);
  if (d)
    wplua_pushobject (L, d);
  return d ? 1 : 0;
}

static int
spa_device_get_managed_object (lua_State *L)
{
  WpSpaDevice *device = wplua_checkobject (L, 1, WP_TYPE_SPA_DEVICE);
  guint id = luaL_checkinteger (L, 2);
  GObject *obj = wp_spa_device_get_managed_object (device, id);
  if (obj)
    wplua_pushobject (L, obj);
  return obj ? 1 : 0;
}

static int
spa_device_store_managed_object (lua_State *L)
{
  WpSpaDevice *device = wplua_checkobject (L, 1, WP_TYPE_SPA_DEVICE);
  guint id = luaL_checkinteger (L, 2);
  GObject *obj = (lua_type (L, 3) != LUA_TNIL) ?
      g_object_ref (wplua_checkobject (L, 3, G_TYPE_OBJECT)) : NULL;

  wp_spa_device_store_managed_object (device, id, obj);
  return 0;
}

static const luaL_Reg spa_device_methods[] = {
  { "get_managed_object", spa_device_get_managed_object },
  { "store_managed_object", spa_device_store_managed_object },
  { NULL, NULL }
};

/* Node */

static int
node_new (lua_State *L)
{
  const char *factory = luaL_checkstring (L, 1);
  WpProperties *properties = NULL;

  if (lua_type (L, 2) != LUA_TNONE) {
    luaL_checktype (L, 2, LUA_TTABLE);
    properties = wplua_table_to_properties (L, 2);
  }

  WpNode *d = wp_node_new_from_factory (get_wp_export_core (L),
      factory, properties);
  if (d)
    wplua_pushobject (L, d);
  return d ? 1 : 0;
}

static int
node_get_state (lua_State *L)
{
  WpNode *node = wplua_checkobject (L, 1, WP_TYPE_NODE);
  const gchar *error = NULL;
  WpNodeState state = wp_node_get_state (node, &error);
  wplua_enum_to_lua (L, state, WP_TYPE_NODE_STATE);
  lua_pushstring (L, error ? error : "");
  return 2;
}

static int
node_get_n_input_ports (lua_State *L)
{
  WpNode *node = wplua_checkobject (L, 1, WP_TYPE_NODE);
  guint max = 0;
  guint ports = wp_node_get_n_input_ports (node, &max);
  lua_pushinteger (L, ports);
  lua_pushinteger (L, max);
  return 2;
}

static int
node_get_n_output_ports (lua_State *L)
{
  WpNode *node = wplua_checkobject (L, 1, WP_TYPE_NODE);
  guint max = 0;
  guint ports = wp_node_get_n_output_ports (node, &max);
  lua_pushinteger (L, ports);
  lua_pushinteger (L, max);
  return 2;
}

static int
node_get_n_ports (lua_State *L)
{
  WpNode *node = wplua_checkobject (L, 1, WP_TYPE_NODE);
  guint ports = wp_node_get_n_ports (node);
  lua_pushinteger (L, ports);
  return 1;
}

static int
node_iterate_ports (lua_State *L)
{
  WpNode *node = wplua_checkobject (L, 1, WP_TYPE_NODE);
  WpObjectInterest *oi = get_optional_object_interest (L, 2, WP_TYPE_PORT);
  WpIterator *it = oi ?
      wp_node_new_ports_filtered_iterator_full (node,
          wp_object_interest_ref (oi)) :
      wp_node_new_ports_iterator (node);
  return push_wpiterator (L, it);
}

static int
node_lookup_port (lua_State *L)
{
  WpNode *node = wplua_checkobject (L, 1, WP_TYPE_NODE);
  WpObjectInterest *oi = get_optional_object_interest (L, 2, WP_TYPE_PORT);
  WpPort *port = oi ?
      wp_node_lookup_port_full (node, wp_object_interest_ref (oi)) :
      wp_node_lookup_port (node, G_TYPE_OBJECT, NULL);
  if (port) {
    wplua_pushobject (L, port);
    return 1;
  }
  return 0;
}

static int
node_send_command (lua_State *L)
{
  WpNode *node = wplua_checkobject (L, 1, WP_TYPE_NODE);
  const char *command = luaL_checkstring (L, 2);
  wp_node_send_command (node, command);
  return 0;
}

static const luaL_Reg node_methods[] = {
  { "get_state", node_get_state },
  { "get_n_input_ports", node_get_n_input_ports },
  { "get_n_output_ports", node_get_n_output_ports },
  { "get_n_ports", node_get_n_ports },
  { "iterate_ports", node_iterate_ports },
  { "lookup_port", node_lookup_port },
  { "send_command", node_send_command },
  { NULL, NULL }
};

/* ImplNode */

static int
impl_node_new (lua_State *L)
{
  const char *factory = luaL_checkstring (L, 1);
  WpProperties *properties = NULL;

  if (lua_type (L, 2) != LUA_TNONE) {
    luaL_checktype (L, 2, LUA_TTABLE);
    properties = wplua_table_to_properties (L, 2);
  }

  WpImplNode *d = wp_impl_node_new_from_pw_factory (get_wp_export_core (L),
     factory, properties);
  if (d)
    wplua_pushobject (L, d);
  return d ? 1 : 0;
}

/* Port */

static int
port_get_direction (lua_State *L)
{
  WpPort *port = wplua_checkobject (L, 1, WP_TYPE_PORT);
  WpDirection direction = wp_port_get_direction (port);
  wplua_enum_to_lua (L, direction, WP_TYPE_DIRECTION);
  return 1;
}

static const luaL_Reg port_methods[] = {
  { "get_direction", port_get_direction },
  { NULL, NULL }
};

/* Link */

static int
link_new (lua_State *L)
{
  const char *factory = luaL_checkstring (L, 1);
  WpProperties *properties = NULL;

  if (lua_type (L, 2) != LUA_TNONE) {
    luaL_checktype (L, 2, LUA_TTABLE);
    properties = wplua_table_to_properties (L, 2);
  }

  WpLink *l = wp_link_new_from_factory (get_wp_core (L), factory, properties);
  if (l)
    wplua_pushobject (L, l);
  return l ? 1 : 0;
}

/* Client */

static gboolean
client_parse_permissions (const gchar * perms_str, guint32 *perms)
{
  *perms = 0;

  if (!perms_str)
    return FALSE;
  else if (g_strcmp0 (perms_str, "all") == 0)
    *perms = PW_PERM_ALL;
  else {
    for (guint i = 0; i < strlen (perms_str); i++) {
      switch (perms_str[i]) {
        case 'r': *perms |= PW_PERM_R; break;
        case 'w': *perms |= PW_PERM_W; break;
        case 'x': *perms |= PW_PERM_X; break;
        case 'm': *perms |= PW_PERM_M; break;
        case '-': break;
        default:
          return FALSE;
      }
    }
  }
  return TRUE;
}

static int
client_update_permissions (lua_State *L)
{
  WpClient *client = wplua_checkobject (L, 1, WP_TYPE_CLIENT);
  g_autoptr (GArray) arr = NULL;

  luaL_checktype (L, 2, LUA_TTABLE);

  lua_pushnil(L);
  while (lua_next (L, -2)) {
    struct pw_permission perm = {0};

    if (lua_type (L, -2) == LUA_TSTRING &&
        (!g_ascii_strcasecmp (lua_tostring(L, -2), "any") ||
         !g_ascii_strcasecmp (lua_tostring(L, -2), "all")))
      perm.id = PW_ID_ANY;
    else if (lua_isinteger (L, -2))
      perm.id = lua_tointeger (L, -2);
    else
      luaL_error (L, "invalid key for permissions array");

    if (!client_parse_permissions (lua_tostring (L, -1), &perm.permissions))
      luaL_error (L, "invalid permission string: '%s'", lua_tostring (L, -1));

    if (!arr)
      arr = g_array_new (FALSE, FALSE, sizeof (struct pw_permission));

    g_array_append_val (arr, perm);
    lua_pop (L, 1);
  }

  wp_client_update_permissions_array (client, arr->len,
      (const struct pw_permission *) arr->data);
  return 0;
}

static const luaL_Reg client_methods[] = {
  { "update_permissions", client_update_permissions },
  { NULL, NULL }
};

/* WpSessionItem */

static int
session_item_new (lua_State *L)
{
  const char *type = luaL_checkstring (L, 1);
  WpSessionItem *si = wp_session_item_make (get_wp_core (L), type);
  if (si)
    wplua_pushobject (L, si);
  return si ? 1 : 0;
}

static int
session_item_get_associated_proxy (lua_State *L)
{
  WpSessionItem *si = wplua_checkobject (L, 1, WP_TYPE_SESSION_ITEM);
  const char *typestr = luaL_checkstring (L, 2);
  WpProxy *proxy = wp_session_item_get_associated_proxy (si,
      parse_gtype (typestr));
  if (proxy)
    wplua_pushobject (L, proxy);
  return proxy ? 1 : 0;
}

static int
session_item_reset (lua_State *L)
{
  WpSessionItem *si = wplua_checkobject (L, 1, WP_TYPE_SESSION_ITEM);
  wp_session_item_reset (si);
  return 0;
}

static int
session_item_configure (lua_State *L)
{
  WpSessionItem *si = wplua_checkobject (L, 1, WP_TYPE_SESSION_ITEM);
  WpProperties *props = wp_properties_new_empty ();

  /* validate arguments */
  luaL_checktype (L, 2, LUA_TTABLE);

  /* build the configuration properties */
  lua_pushnil (L);
  while (lua_next (L, 2)) {
    const gchar *key = NULL;
    g_autofree gchar *var = NULL;

    switch (lua_type (L, -1)) {
      case LUA_TBOOLEAN:
        var = g_strdup_printf ("%u", lua_toboolean (L, -1));
        break;
      case LUA_TNUMBER:
        if (lua_isinteger (L, -1))
          var = g_strdup_printf ("%lld", lua_tointeger (L, -1));
        else
          var = g_strdup_printf ("%f", lua_tonumber (L, -1));
        break;
      case LUA_TSTRING:
        var = g_strdup (lua_tostring (L, -1));
        break;
      case LUA_TUSERDATA: {
        GValue *v = lua_touserdata (L, -1);
        gpointer p = g_value_peek_pointer (v);
        var = g_strdup_printf ("%p", p);
        break;
      }
      default:
        luaL_error (L, "configure does not support lua type ",
            lua_typename(L, lua_type(L, -1)));
        break;
    }

    key = luaL_tolstring (L, -2, NULL);
    wp_properties_set (props, key, var);
    lua_pop (L, 2);
  }

  lua_pushboolean (L, wp_session_item_configure (si, props));
  return 1;
}

static int
session_item_register (lua_State *L)
{
  WpSessionItem *si = wplua_checkobject (L, 1, WP_TYPE_SESSION_ITEM);
  wp_session_item_register (g_object_ref (si));
  return 0;
}

static int
session_item_remove (lua_State *L)
{
  WpSessionItem *si = wplua_checkobject (L, 1, WP_TYPE_SESSION_ITEM);
  wp_session_item_remove (si);
  return 0;
}

static const luaL_Reg session_item_methods[] = {
  { "get_associated_proxy", session_item_get_associated_proxy },
  { "reset", session_item_reset },
  { "configure", session_item_configure },
  { "register", session_item_register },
  { "remove", session_item_remove },
  { NULL, NULL }
};

/* WpPipewireObject */

static int
pipewire_object_iterate_params (lua_State *L)
{
  WpPipewireObject *pwobj = wplua_checkobject (L, 1, WP_TYPE_PIPEWIRE_OBJECT);
  const gchar *id = luaL_checkstring (L, 2);
  WpIterator *it = wp_pipewire_object_enum_params_sync (pwobj, id, NULL);
  return push_wpiterator (L, it);
}

static int
pipewire_object_set_param (lua_State *L)
{
  WpPipewireObject *pwobj = wplua_checkobject (L, 1, WP_TYPE_PIPEWIRE_OBJECT);
  const gchar *id = luaL_checkstring (L, 2);
  WpSpaPod *pod = wplua_checkboxed (L, 3, WP_TYPE_SPA_POD);
  wp_pipewire_object_set_param (pwobj, id, 0, wp_spa_pod_ref (pod));
  return 0;
}

static const luaL_Reg pipewire_object_methods[] = {
  { "iterate_params", pipewire_object_iterate_params },
  { "set_param" , pipewire_object_set_param },
  { "set_params" , pipewire_object_set_param }, /* deprecated, compat only */
  { NULL, NULL }
};

/* WpState */

static int
state_new (lua_State *L)
{
  const gchar *name = luaL_checkstring (L, 1);
  WpState *state = wp_state_new (name);
  wplua_pushobject (L, state);
  return 1;
}

static int
state_clear (lua_State *L)
{
  WpState *state = wplua_checkobject (L, 1, WP_TYPE_STATE);
  wp_state_clear (state);
  return 0;
}

static int
state_save (lua_State *L)
{
  WpState *state = wplua_checkobject (L, 1, WP_TYPE_STATE);
  luaL_checktype (L, 2, LUA_TTABLE);
  g_autoptr (WpProperties) props = wplua_table_to_properties (L, 2);
  g_autoptr (GError) error = NULL;
  gboolean saved = wp_state_save (state, props, &error);
  lua_pushboolean (L, saved);
  lua_pushstring (L, error ? error->message : "");
  return 2;
}

static int
state_load (lua_State *L)
{
  WpState *state = wplua_checkobject (L, 1, WP_TYPE_STATE);
  g_autoptr (WpProperties) props = wp_state_load (state);
  wplua_properties_to_table (L, props);
  return 1;
}

static const luaL_Reg state_methods[] = {
  { "clear", state_clear },
  { "save" , state_save },
  { "load" , state_load },
  { NULL, NULL }
};

/* ImplModule */

static int
impl_module_new (lua_State *L)
{
  const char *name, *args = NULL;
  WpProperties *properties = NULL;

  name = luaL_checkstring (L, 1);

  if (lua_type (L, 2) != LUA_TNONE)
    args = luaL_checkstring (L, 2);

  if (lua_type (L, 3) != LUA_TNONE) {
    luaL_checktype (L, 3, LUA_TTABLE);
    properties = wplua_table_to_properties (L, 2);
  }

  WpImplModule *m = wp_impl_module_load (get_wp_export_core (L),
     name, args, properties);

  if (m) {
    wplua_pushobject (L, m);
    return 1;
  } else {
    return 0;
  }
}

void
wp_lua_scripting_api_init (lua_State *L)
{
  g_autoptr (GError) error = NULL;

  luaL_newlib (L, glib_methods);
  lua_setglobal (L, "GLib");

  luaL_newlib (L, log_funcs);
  lua_setglobal (L, "WpLog");

  luaL_newlib (L, core_funcs);
  lua_setglobal (L, "WpCore");

  luaL_newlib (L, plugin_funcs);
  lua_setglobal (L, "WpPlugin");

  wp_lua_scripting_pod_init (L);

  wplua_register_type_methods (L, G_TYPE_SOURCE,
      NULL, source_methods);
  wplua_register_type_methods (L, WP_TYPE_OBJECT,
      NULL, object_methods);
  wplua_register_type_methods (L, WP_TYPE_PROXY,
      NULL, proxy_methods);
  wplua_register_type_methods (L, WP_TYPE_GLOBAL_PROXY,
      NULL, global_proxy_methods);
  wplua_register_type_methods (L, WP_TYPE_OBJECT_INTEREST,
      object_interest_new, object_interest_methods);
  wplua_register_type_methods (L, WP_TYPE_OBJECT_MANAGER,
      object_manager_new, object_manager_methods);
  wplua_register_type_methods (L, WP_TYPE_METADATA,
      NULL, metadata_methods);
  wplua_register_type_methods (L, WP_TYPE_ENDPOINT,
      NULL, endpoint_methods);
  wplua_register_type_methods (L, WP_TYPE_DEVICE,
      device_new, NULL);
  wplua_register_type_methods (L, WP_TYPE_SPA_DEVICE,
      spa_device_new, spa_device_methods);
  wplua_register_type_methods (L, WP_TYPE_NODE,
      node_new, node_methods);
  wplua_register_type_methods (L, WP_TYPE_IMPL_NODE,
      impl_node_new, NULL);
  wplua_register_type_methods (L, WP_TYPE_PORT,
      NULL, port_methods);
  wplua_register_type_methods (L, WP_TYPE_LINK,
      link_new, NULL);
  wplua_register_type_methods (L, WP_TYPE_CLIENT,
      NULL, client_methods);
  wplua_register_type_methods (L, WP_TYPE_SESSION_ITEM,
      session_item_new, session_item_methods);
  wplua_register_type_methods (L, WP_TYPE_PIPEWIRE_OBJECT,
      NULL, pipewire_object_methods);
  wplua_register_type_methods (L, WP_TYPE_STATE,
      state_new, state_methods);
  wplua_register_type_methods (L, WP_TYPE_IMPL_MODULE,
      impl_module_new, NULL);

  wplua_load_uri (L, URI_API, 0, 0, &error);
  if (G_UNLIKELY (error))
    wp_critical ("Failed to load api: %s", error->message);
}
