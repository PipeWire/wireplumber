/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_SPA_TYPE_H__
#define __WIREPLUMBER_SPA_TYPE_H__

#include <gio/gio.h>
#include "defs.h"

G_BEGIN_DECLS

/**
 * WpSpaTypeTable:
 * @WP_SPA_TYPE_TABLE_BASIC: The basic type table
 * @WP_SPA_TYPE_TABLE_PARAM: The param type table (used as object id)
 * @WP_SPA_TYPE_TABLE_PROPS: The object properties type table
 * @WP_SPA_TYPE_TABLE_PROP_INFO: The object property info type table
 * @WP_SPA_TYPE_TABLE_CONTROL: The sequence control type table
 * @WP_SPA_TYPE_TABLE_CHOICE: The choice type table
 * @WP_SPA_TYPE_TABLE_FORMAT: The object format type table
 * @WP_SPA_TYPE_TABLE_PARAM_PORT_CONFIG: The object param port config type table
 * @WP_SPA_TYPE_TABLE_PARAM_PROFILE: The sequence control type table
 *
 * The diferent tables (namespaces) the registry has.
 */
typedef enum {
  WP_SPA_TYPE_TABLE_BASIC = 0,
  WP_SPA_TYPE_TABLE_PARAM,
  WP_SPA_TYPE_TABLE_PROPS,
  WP_SPA_TYPE_TABLE_PROP_INFO,
  WP_SPA_TYPE_TABLE_CONTROL,
  WP_SPA_TYPE_TABLE_CHOICE,
  WP_SPA_TYPE_TABLE_FORMAT,
  WP_SPA_TYPE_TABLE_PARAM_PORT_CONFIG,
  WP_SPA_TYPE_TABLE_PARAM_PROFILE,
  WP_SPA_TYPE_TABLE_LAST,
} WpSpaTypeTable;

WP_API
void wp_spa_type_init (gboolean register_spa);

WP_API
void wp_spa_type_deinit (void);

WP_API
size_t wp_spa_type_get_table_size (WpSpaTypeTable table);

WP_API
gboolean wp_spa_type_register (WpSpaTypeTable table, const char *name,
  const char *nick);

WP_API
void wp_spa_type_unregister (WpSpaTypeTable table, const char *nick);

WP_API
gboolean wp_spa_type_get_by_nick (WpSpaTypeTable table, const char *nick,
    guint32 *id, const char **name, WpSpaTypeTable *values_table);

WP_API
gboolean wp_spa_type_get_by_id (WpSpaTypeTable table, guint32 id,
    const char **name, const char **nick, WpSpaTypeTable *values_table);

G_END_DECLS

#endif
