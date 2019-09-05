/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WP_TOML_ARRAY_H__
#define __WP_TOML_ARRAY_H__

#include <glib-object.h>

#include <stdint.h>

G_BEGIN_DECLS

/* WpTomlArray */
GType wp_toml_array_get_type (void);
typedef struct _WpTomlArray WpTomlArray;
WpTomlArray * wp_toml_array_ref (WpTomlArray * self);
void wp_toml_array_unref (WpTomlArray * self);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (WpTomlArray, wp_toml_array_unref)

/* API */
typedef void (*WpTomlArrayForEachBoolFunc)(const gboolean *, gpointer);
void wp_toml_array_for_each_boolean (const WpTomlArray *self,
    WpTomlArrayForEachBoolFunc func, gpointer user_data);
typedef void (*WpTomlArrayForEachInt64Func)(const int64_t *, gpointer);
void wp_toml_array_for_each_int64 (const WpTomlArray *self,
    WpTomlArrayForEachInt64Func func, gpointer user_data);
typedef void (*WpTomlArrayForEachDoubleFunc)(const double *, gpointer);
void wp_toml_array_for_each_double (const WpTomlArray *self,
    WpTomlArrayForEachDoubleFunc func, gpointer user_data);
typedef void (*WpTomlArrayForEachStringFunc)(const char *, gpointer);
void wp_toml_array_for_each_string (const WpTomlArray *self,
    WpTomlArrayForEachStringFunc func, gpointer user_data);
typedef void (*WpTomlArrayForEachArrayFunc)(WpTomlArray *, gpointer);
void wp_toml_array_for_each_array (const WpTomlArray *self,
    WpTomlArrayForEachArrayFunc func, gpointer user_data);

G_END_DECLS

#endif
