/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <wplua/wplua.h>

static gboolean
load_components (lua_State *L, WpCore * core, GError ** error)
{
  lua_getglobal (L, "SANDBOX_COMMON_ENV");

  switch (lua_getfield (L, -1, "components")) {
  case LUA_TTABLE:
    break;
  case LUA_TNIL:
    wp_debug ("no components specified");
    goto done;
  default:
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "Expected 'components' to be a table");
    return FALSE;
  }

  lua_pushnil (L);
  while (lua_next (L, -2)) {
    /* value must be a table */
    if (lua_type (L, -1) != LUA_TTABLE) {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "'components' must be a table with tables as values");
      return FALSE;
    }

    /* record indexes to the current key and value of the components table */
    int key = lua_absindex (L, -2);
    int table = lua_absindex (L, -1);

    /* get component */
    if (lua_geti (L, table, 1) != LUA_TSTRING) {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "components['%s'] has a non-string or unspecified component name",
          lua_tostring (L, key));
      return FALSE;
    }
    const char * component = lua_tostring (L, -1);

    /* get component type */
    if (lua_getfield (L, table, "type") != LUA_TSTRING) {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "components['%s'] has a non-string or unspecified component type",
          lua_tostring (L, key));
      return FALSE;
    }
    const char * type = lua_tostring (L, -1);

    /* optional component arguments */
    GVariant *args = NULL;
    if (lua_getfield (L, table, "args") == LUA_TTABLE) {
      args = wplua_lua_to_gvariant (L, -1);
    }

    gboolean optional = FALSE;
    if (lua_getfield (L, table, "optional") == LUA_TBOOLEAN) {
      optional = lua_toboolean (L, -1);
    }

    wp_debug ("load component: %s (%s) optional(%s)",
     component, type, (optional ? "true" : "false"));

    g_autoptr (GError) load_error = NULL;
    if (!wp_core_load_component (core, component, type, args, &load_error)) {
      if (!optional) {
        g_propagate_error (error, g_steal_pointer (&load_error));
        return FALSE;
      } else {
        wp_message ("%s", load_error->message);
      }
    }

    /* clear the stack up to the key */
    lua_settop (L, key);
  }

done:
  lua_pop (L, 2); /* pop components & SANDBOX_COMMON_ENV */
  return TRUE;
}

static gboolean
load_file (const GValue *item, GValue *ret, gpointer data)
{
  lua_State *L = data;
  const gchar *path = g_value_get_string (item);
  g_autoptr (GError) error = NULL;
  int nargs;

  if (g_file_test (path, G_FILE_TEST_IS_DIR))
    return TRUE;

  wp_info ("loading config file: %s", path);

  nargs = wplua_push_sandbox (L);
  if (!wplua_load_path (L, path, &error) ||
      !wplua_pcall (L, nargs, 0, &error)) {
    lua_settop (L, 0);
    g_value_unset (ret);
    g_value_init (ret, G_TYPE_ERROR);
    g_value_take_boxed (ret, g_steal_pointer (&error));
    return FALSE;
  }

  g_value_set_int (ret, g_value_get_int (ret) + 1);
  return TRUE;
}

#define CONFIG_DIRS_LOOKUP_SET \
    (WP_LOOKUP_DIR_ENV_CONFIG | \
     WP_LOOKUP_DIR_XDG_CONFIG_HOME | \
     WP_LOOKUP_DIR_ETC | \
     WP_LOOKUP_DIR_PREFIX_SHARE)

gboolean
wp_lua_scripting_load_configuration (const gchar * conf_file,
    WpCore * core, GError ** error)
{
  g_autoptr (lua_State) L = wplua_new ();
  g_autofree gchar * path = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) fold_ret = G_VALUE_INIT;
  gint nfiles = 0;

  wplua_enable_sandbox (L, WP_LUA_SANDBOX_MINIMAL_STD);

  /* load conf_file itself */
  path = wp_find_file (CONFIG_DIRS_LOOKUP_SET, conf_file, NULL);
  if (path) {
    wp_info ("loading config file: %s", path);
    int nargs = wplua_push_sandbox (L);
    if (!wplua_load_path (L, path, error) ||
        !wplua_pcall (L, nargs, 0, error)) {
      lua_settop (L, 0);
      return FALSE;
    }
    nfiles = 1;
  }
  g_clear_pointer (&path, g_free);

  path = g_strdup_printf ("%s.d", conf_file);
  it = wp_new_files_iterator (CONFIG_DIRS_LOOKUP_SET, path, ".lua");

  g_value_init (&fold_ret, G_TYPE_INT);
  g_value_set_int (&fold_ret, nfiles);
  if (!wp_iterator_fold (it, load_file, &fold_ret, L)) {
    if (error && G_VALUE_HOLDS (&fold_ret, G_TYPE_ERROR))
      *error = g_value_dup_boxed (&fold_ret);
    return FALSE;
  }
  nfiles = g_value_get_int (&fold_ret);

  if (nfiles == 0) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
        "Could not locate configuration file '%s'", conf_file);
    return FALSE;
  }

  if (!load_components (L, core, error))
    return FALSE;

  return TRUE;
}
