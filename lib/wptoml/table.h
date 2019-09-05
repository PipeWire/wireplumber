/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WP_TOML_TABLE_H__
#define __WP_TOML_TABLE_H__

#include <glib-object.h>

#include <stdint.h>

#include "array.h"

G_BEGIN_DECLS

/* WpTomlTable */
GType wp_toml_table_get_type (void);
typedef struct _WpTomlTable WpTomlTable;
WpTomlTable * wp_toml_table_ref (WpTomlTable * self);
void wp_toml_table_unref (WpTomlTable * self);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (WpTomlTable, wp_toml_table_unref)

/* WpTomlTableArray */
GType wp_toml_table_array_get_type (void);
typedef struct _WpTomlTableArray WpTomlTableArray;
WpTomlTableArray * wp_toml_table_array_ref (WpTomlTableArray * self);
void wp_toml_table_array_unref (WpTomlTableArray * self);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (WpTomlTableArray, wp_toml_table_array_unref)

/* API */
gboolean wp_toml_table_contains (const WpTomlTable *self, const char *key);
gboolean wp_toml_table_get_boolean (const WpTomlTable *self, const char *key,
    gboolean *val);
gboolean wp_toml_table_get_qualified_boolean (const WpTomlTable *self,
    const char *key, gboolean *val);
gboolean wp_toml_table_get_int8 (const WpTomlTable *self, const char *key,
    int8_t *val);
gboolean wp_toml_table_get_qualified_int8 (const WpTomlTable *self,
    const char *key, int8_t *val);
gboolean wp_toml_table_get_uint8 (const WpTomlTable *self, const char *key,
    uint8_t *val);
gboolean wp_toml_table_get_qualified_uint8 (const WpTomlTable *self,
    const char *key, uint8_t *val);
gboolean wp_toml_table_get_int16 (const WpTomlTable *self, const char *key,
    int16_t *val);
gboolean wp_toml_table_get_qualified_int16 (const WpTomlTable *self,
    const char *key, int16_t *val);
gboolean wp_toml_table_get_uint16 (const WpTomlTable *self, const char *key,
    uint16_t *val);
gboolean wp_toml_table_get_qualified_uint16 (const WpTomlTable *self,
    const char *key, uint16_t *val);
gboolean wp_toml_table_get_int32 (const WpTomlTable *self, const char *key,
    int32_t *val);
gboolean wp_toml_table_get_qualified_int32 (const WpTomlTable *self,
    const char *key, int32_t *val);
gboolean wp_toml_table_get_uint32 (const WpTomlTable *self, const char *key,
    uint32_t *val);
gboolean wp_toml_table_get_qualified_uint32 (const WpTomlTable *self,
    const char *key, uint32_t *val);
gboolean wp_toml_table_get_int64 (const WpTomlTable *self, const char *key,
    int64_t *val);
gboolean wp_toml_table_get_qualified_int64 (const WpTomlTable *self,
    const char *key, int64_t *val);
gboolean wp_toml_table_get_uint64 (const WpTomlTable *self, const char *key,
    uint64_t *val);
gboolean wp_toml_table_get_qualified_uint64 (const WpTomlTable *self,
    const char *key, uint64_t *val);
gboolean wp_toml_table_get_double (const WpTomlTable *self, const char *key,
    double *val);
gboolean wp_toml_table_get_qualified_double (const WpTomlTable *self,
    const char *key, double *val);
char *wp_toml_table_get_string (const WpTomlTable *self, const char *key);
char *wp_toml_table_get_qualified_string (const WpTomlTable *self,
    const char *key);
WpTomlArray *wp_toml_table_get_array (const WpTomlTable *self, const char *key);
WpTomlArray *wp_toml_table_get_qualified_array (const WpTomlTable *self,
    const char *key);
WpTomlTable *wp_toml_table_get_table (const WpTomlTable *self, const char *key);
WpTomlTable *wp_toml_table_get_qualified_table (const WpTomlTable *self,
    const char *key);
WpTomlTableArray *wp_toml_table_get_array_table (const WpTomlTable *self,
    const char *key);
WpTomlTableArray *wp_toml_table_get_qualified_array_table (
    const WpTomlTable *self, const char *key);
typedef void (*WpTomlTableArrayForEachFunc)(const WpTomlTable *, gpointer);
void wp_toml_table_array_for_each (const WpTomlTableArray *self,
    WpTomlTableArrayForEachFunc func, gpointer uder_data);

G_END_DECLS

#endif
