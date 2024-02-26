/* WirePlumber
 *
 * Copyright © 2024 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_BASE_DIRS_H__
#define __WIREPLUMBER_BASE_DIRS_H__

#include "defs.h"
#include "iterator.h"

G_BEGIN_DECLS

/*!
 * \brief Flags to specify lookup directories
 * \ingroup wpbasedirs
 */
typedef enum { /*< flags >*/
  WP_LOOKUP_DIR_ENV_CONFIG = (1 << 0),       /*!< $WIREPLUMBER_CONFIG_DIR */
  WP_LOOKUP_DIR_ENV_DATA = (1 << 1),         /*!< $WIREPLUMBER_DATA_DIR */

  WP_LOOKUP_DIR_XDG_CONFIG_HOME = (1 << 10), /*!< XDG_CONFIG_HOME/wireplumber */
  WP_LOOKUP_DIR_ETC = (1 << 11),             /*!< ($prefix)/etc/wireplumber */
  WP_LOOKUP_DIR_PREFIX_SHARE = (1 << 12),    /*!< $prefix/share/wireplumber */
} WpLookupDirs;

WP_API
gchar * wp_find_file (WpLookupDirs dirs, const gchar *filename,
    const gchar *subdir);

WP_API
WpIterator * wp_new_files_iterator (WpLookupDirs dirs, const gchar *subdir,
    const gchar *suffix);

G_END_DECLS

#endif
