/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WP_TOML_FILE_H__
#define __WP_TOML_FILE_H__

#include <glib-object.h>

#include "table.h"

G_BEGIN_DECLS

/* WpTomlFile */
GType wp_toml_file_get_type (void);
typedef struct _WpTomlFile WpTomlFile;
WpTomlFile *wp_toml_file_new (const char *name);
WpTomlFile * wp_toml_file_ref (WpTomlFile * self);
void wp_toml_file_unref (WpTomlFile * self);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (WpTomlFile, wp_toml_file_unref)

/* API */
const char *wp_toml_file_get_name (const WpTomlFile *self);
WpTomlTable *wp_toml_file_get_table (const WpTomlFile *self);

G_END_DECLS

#endif
