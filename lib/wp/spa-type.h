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
 *
 * The diferent tables (namespaces) the registry has.
 * WP_SPA_TYPE_TABLE_BASIC (0) - The basic type table
 * WP_SPA_TYPE_TABLE_PARAM (1) – The param type table (used as object id)
 * WP_SPA_TYPE_TABLE_PROPS (2) – The object properties type table
 * WP_SPA_TYPE_TABLE_PROP_INFO (3) – The object property info type table
 * WP_SPA_TYPE_TABLE_CONTROL (4) - The sequence control type table
 * WP_SPA_TYPE_TABLE_CHOICE (5) - The choice type table
 */
typedef enum {
  WP_SPA_TYPE_TABLE_BASIC = 0,
  WP_SPA_TYPE_TABLE_PARAM,
  WP_SPA_TYPE_TABLE_PROPS,
  WP_SPA_TYPE_TABLE_PROP_INFO,
  WP_SPA_TYPE_TABLE_CONTROL,
  WP_SPA_TYPE_TABLE_CHOICE,
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
