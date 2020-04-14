/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-spa-type"

#include <spa/utils/type-info.h>

#include "spa-type.h"

static gboolean
name_equal_func (gconstpointer a, gconstpointer b) {
  const struct spa_type_info *ti = a;
  const char *name = b;
  return g_strcmp0 (ti->name, name) == 0;
}

static const struct spa_type_info *
spa_type_find (const struct spa_type_info *table, GEqualFunc func,
    gconstpointer data)
{
  for (guint32 i = 0; table[i].name; i++)
    if (func (table + i, data))
      return table + i;
  return NULL;
}

static const struct spa_type_info *
spa_type_find_by_name (const struct spa_type_info *table, const char *name)
{
  return spa_type_find (table, name_equal_func, name);
}

struct type_info {
  gboolean is_spa_type;
  union {
    const struct spa_type_info *spa;
    struct {
      guint32 type;
      char *name;
    } custom;
  } info;
  char *nick;
};

static struct type_info *
type_info_new_spa (const struct spa_type_info *info, const char* nick) {
  struct type_info *ti = g_slice_new0 (struct type_info);
  ti->is_spa_type = TRUE;
  ti->info.spa = info;
  ti->nick = g_strdup (nick);
  return ti;
}

static struct type_info *
type_info_new_custom (uint32_t type, const char *name, const char* nick) {
  struct type_info *ti = g_slice_new0 (struct type_info);
  ti->is_spa_type = FALSE;
  ti->info.custom.type = type;
  ti->info.custom.name = g_strdup (name);
  ti->nick = g_strdup (nick);
  return ti;
}

static void
type_info_free (struct type_info *ti) {
  g_return_if_fail (ti);
  if (!ti->is_spa_type)
    g_clear_pointer (&ti->info.custom.name, g_free);
  g_clear_pointer (&ti->nick, g_free);
  g_slice_free (struct type_info, ti);
}

struct spa_type_table_data {
  const struct spa_type_info *spa_table;
  guint32 last_id;
  GPtrArray *info_array;
  GHashTable *id_table;
  GHashTable *nick_table;
};

static struct spa_type_table_data s_tables [WP_SPA_TYPE_TABLE_LAST] = {
  [WP_SPA_TYPE_TABLE_BASIC]     = {spa_types, SPA_TYPE_VENDOR_Other, NULL, NULL, NULL },
  [WP_SPA_TYPE_TABLE_PARAM]     = {spa_type_param, SPA_N_ELEMENTS (spa_type_param), NULL, NULL, NULL, },
  [WP_SPA_TYPE_TABLE_PROPS]     = {spa_type_props, SPA_PROP_START_CUSTOM, NULL, NULL, NULL, },
  [WP_SPA_TYPE_TABLE_PROP_INFO] = {spa_type_prop_info, SPA_N_ELEMENTS (spa_type_prop_info), NULL, NULL, NULL, },
  [WP_SPA_TYPE_TABLE_CONTROL]   = {spa_type_control, SPA_CONTROL_LAST, NULL, NULL, NULL, },
  [WP_SPA_TYPE_TABLE_CHOICE]    = {spa_type_choice, SPA_N_ELEMENTS (spa_type_choice), NULL, NULL, NULL, },
};

static WpSpaTypeTable
wp_spa_type_table_find_by_spa_table (const struct spa_type_info *spa_table)
{
  for (guint32 i = 0; i < WP_SPA_TYPE_TABLE_LAST; i++)
    if (s_tables[i].spa_table == spa_table)
      return i;
  return 0;
}

/**
 * wp_spa_type_init:
 * @register_spa: whether spa types will be registered or not
 *
 * Initializes the spa type registry
 */
void
wp_spa_type_init (gboolean register_spa)
{
  /* Init the array and hash tables */
  for (guint32 i = 0; i < WP_SPA_TYPE_TABLE_LAST; i++) {
    struct spa_type_table_data *td = s_tables + i;
    if (!td->info_array)
        td->info_array = g_ptr_array_new_with_free_func ((GDestroyNotify)
            type_info_free);
    if (!td->id_table)
        td->id_table = g_hash_table_new_full (g_direct_hash, g_direct_equal,
            NULL, NULL);
    if (!td->nick_table)
        td->nick_table = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, NULL);
  }

  /* Register the spa types if requested */
  if (register_spa) {
    for (guint32 i = 0; i < WP_SPA_TYPE_TABLE_LAST; i++) {
      struct spa_type_table_data *td = s_tables + i;
      for (guint32 j = 0; td->spa_table && td->spa_table[j].name; j++) {
        const struct spa_type_info *t = td->spa_table + j;
        const char *nick = strrchr (t->name, ':');
        if (nick && strlen (nick) > 1)
          wp_spa_type_register (i, t->name, nick + 1);
      }
    }
  }
}

/**
 * wp_spa_type_deinit:
 *
 * Deinitializes the spa type registry
 */
void
wp_spa_type_deinit (void)
{
  for (guint32 i = 0; i < WP_SPA_TYPE_TABLE_LAST; i++) {
    struct spa_type_table_data *td = s_tables + i;
    g_clear_pointer (&td->info_array, g_ptr_array_unref);
    g_clear_pointer (&td->id_table, g_hash_table_unref);
    g_clear_pointer (&td->nick_table, g_hash_table_unref);
  }
}

/**
 * wp_spa_type_get_table_size:
 * @table: the table
 *
 * Gets the number of registered types in a given table
 *
 * Returns: The number of registered types
 */
size_t
wp_spa_type_get_table_size (WpSpaTypeTable table)
{
  struct spa_type_table_data *td = NULL;

  g_return_val_if_fail (table < WP_SPA_TYPE_TABLE_LAST, 0);

  /* Get the table data */
  td = s_tables + table;

  return td->info_array->len;
}

/**
 * wp_spa_type_register:
 * @table: the table
 * @name: the name of the type
 * @nick: the nick name of the type
 *
 * Registers a type name with a nickname in the registry
 *
 * Returns: TRUE if the type could be registered, FALSE otherwise
 */
gboolean
wp_spa_type_register (WpSpaTypeTable table, const char *name, const char *nick)
{
  struct spa_type_table_data *td = NULL;
  const struct spa_type_info *spa_info = NULL;
  struct type_info *info = NULL;
  guint32 id;

  g_return_val_if_fail (table < WP_SPA_TYPE_TABLE_LAST, FALSE);
  g_return_val_if_fail (name, FALSE);
  g_return_val_if_fail (nick, FALSE);

  /* Get the table data */
  td = s_tables + table;

  /* Return false if the nick already exists in the nick table */
  if (g_hash_table_contains (td->nick_table, nick))
    return FALSE;

  /* Add the type in the custom table */
  spa_info = spa_type_find_by_name (td->spa_table, name);
  if (spa_info) {
    id = spa_info->type;
    info = type_info_new_spa (spa_info, nick);
  } else {
    id = ++td->last_id;
    info = type_info_new_custom (id, name, nick);
  }
  g_ptr_array_add (td->info_array, info);

  /* Insert the id and nick in the hash tables */
  return g_hash_table_insert (td->id_table, GUINT_TO_POINTER (id), info) &&
      g_hash_table_insert (td->nick_table, g_strdup (nick), info);
}

/**
 * wp_spa_type_unregister:
 * @table: the table
 * @nick: the nick name of the type
 *
 * Unregisters a type given its nick name
 */
void
wp_spa_type_unregister (WpSpaTypeTable table, const char *nick)
{
  struct spa_type_table_data *td = NULL;
  struct type_info *info = NULL;
  guint32 id;

  g_return_if_fail (table < WP_SPA_TYPE_TABLE_LAST);
  g_return_if_fail (nick);

  /* Get the table data */
  td = s_tables + table;

  /* Lookup the info by nick */
  info = g_hash_table_lookup (td->nick_table, nick);
  if (!info)
    return;

  /* Get id */
  id = info->is_spa_type ? info->info.spa->type : info->info.custom.type;

  /* Remove info from hash tables */
  g_hash_table_remove (td->nick_table, nick);
  g_hash_table_remove (td->id_table, GUINT_TO_POINTER (id));

  /* Remove info from array */
  g_ptr_array_remove_fast (td->info_array, info);
}

/**
 * wp_spa_type_get_by_nick:
 * @table: the table
 * @nick: the nick name of the type
 * @id: (out) (optional): the id of the type
 * @name: (out) (optional): the name of the type
 * @values_table: (out) (optional): the values table of the type
 *
 * Gets the id and name of the registered type given its nick name
 *
 * Returns: TRUE if the type was found, FALSE otherwise
 */
gboolean
wp_spa_type_get_by_nick (WpSpaTypeTable table, const char *nick,
    guint32 *id, const char **name, WpSpaTypeTable *values_table)
{
  struct spa_type_table_data *td = NULL;
  const struct type_info *info = NULL;

  g_return_val_if_fail (table < WP_SPA_TYPE_TABLE_LAST, FALSE);

  /* Make sure nick is valid */
  if (!nick)
    return FALSE;

  /* Get the table data */
  td = s_tables + table;

  /* Lookup the info by nick */
  info = g_hash_table_lookup (td->nick_table, nick);
  if (!info)
    return FALSE;

  if (id)
    *id = info->is_spa_type ? info->info.spa->type : info->info.custom.type;
  if (name)
    *name = info->is_spa_type ? info->info.spa->name : info->info.custom.name;
  if (values_table && info->is_spa_type)
    *values_table = wp_spa_type_table_find_by_spa_table (info->info.spa->values);
  return TRUE;
}

/**
 * wp_spa_type_get_by_id:
 * @table: the table
 * @id: the id of the type
 * @name: (out) (optional): the name of the type
 * @nick: (out) (optional): the nick name of the type
 * @values_table: (out) (optional): the values table of the type
 *
 * Gets the name and nick name of the registered type given its id
 *
 * Returns: TRUE if the type was found, FALSE otherwise
 */
gboolean
wp_spa_type_get_by_id (WpSpaTypeTable table, guint32 id,
    const char **name, const char **nick, WpSpaTypeTable *values_table)
{
  struct spa_type_table_data *td = NULL;
  const struct type_info *info = NULL;

  g_return_val_if_fail (table < WP_SPA_TYPE_TABLE_LAST, FALSE);

  /* Get the table data */
  td = s_tables + table;

  /* Lookup the info by id */
  info = g_hash_table_lookup (td->id_table, GUINT_TO_POINTER (id));
  if (!info)
    return FALSE;

  if (name)
    *name = info->is_spa_type ? info->info.spa->name : info->info.custom.name;
  if (nick)
    *nick = info->nick;
  if (values_table && info->is_spa_type)
    *values_table = wp_spa_type_table_find_by_spa_table (info->info.spa->values);
  return TRUE;
}
