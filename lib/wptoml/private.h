/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WP_TOML_PRIVATE_H__
#define __WP_TOML_PRIVATE_H__

#include <glib-object.h>

G_BEGIN_DECLS

/* Forward declaration */
struct _WpTomlArray;
typedef struct _WpTomlArray WpTomlArray;
struct _WpTomlTable;
typedef struct _WpTomlTable WpTomlTable;

WpTomlArray * wp_toml_array_new (gconstpointer data);
WpTomlTable * wp_toml_table_new (gconstpointer data);

G_END_DECLS

#endif
