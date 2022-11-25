/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_WP_H__
#define __WIREPLUMBER_WP_H__

#include "client.h"
#include "component-loader.h"
#include "core.h"
#include "dbus.h"
#include "device.h"
#include "endpoint.h"
#include "error.h"
#include "event-dispatcher.h"
#include "event-hook.h"
#include "global-proxy.h"
#include "iterator.h"
#include "link.h"
#include "log.h"
#include "metadata.h"
#include "module.h"
#include "node.h"
#include "object-interest.h"
#include "object-manager.h"
#include "object.h"
#include "plugin.h"
#include "port.h"
#include "properties.h"
#include "proxy.h"
#include "proxy-interfaces.h"
#include "session-item.h"
#include "si-factory.h"
#include "si-interfaces.h"
#include "spa-json.h"
#include "spa-pod.h"
#include "spa-type.h"
#include "state.h"
#include "transition.h"
#include "wpenums.h"
#include "wpversion.h"
#include "factory.h"
#include "settings.h"

G_BEGIN_DECLS

/*!
 * \ingroup wp
 * Flags for wp_init()
 */
typedef enum {
  /*! Initialize PipeWire by calling pw_init() */
  WP_INIT_PIPEWIRE = (1<<0),
  /*! Initialize support for dynamic spa types.
   * See wp_spa_dynamic_type_init() */
  WP_INIT_SPA_TYPES = (1<<1),
  /*! Override PipeWire's logging system with WirePlumber's one */
  WP_INIT_SET_PW_LOG = (1<<2),
  /*! Set wp_log_writer_default() as GLib's default log writer function */
  WP_INIT_SET_GLIB_LOG = (1<<3),
  /*! Initialize all of the above */
  WP_INIT_ALL = 0xf,
} WpInitFlags;

WP_API
void wp_init (WpInitFlags flags);

WP_API
const char * wp_get_library_version (void);

WP_API
const char * wp_get_library_api_version (void);

WP_API
const gchar * wp_get_module_dir (void);

/*!
 * \brief Flags to specify lookup directories
 * \ingroup wp
 */
typedef enum { /*< flags >*/
  WP_LOOKUP_DIR_ENV_CONFIG = (1 << 0),       /*!< $WIREPLUMBER_CONFIG_DIR */
  WP_LOOKUP_DIR_ENV_DATA = (1 << 1),         /*!< $WIREPLUMBER_DATA_DIR */
  WP_LOOKUP_DIR_ENV_TEST_SRCDIR = (1 << 2),    /*!< $WIREPLUMBER_TEST_DATA_DIR */

  WP_LOOKUP_DIR_XDG_CONFIG_HOME = (1 << 10), /*!< XDG_CONFIG_HOME/wireplumber */
  WP_LOOKUP_DIR_ETC = (1 << 11),             /*!< ($prefix)/etc/wireplumber */
  WP_LOOKUP_DIR_PREFIX_SHARE = (1 << 12),    /*!< $prefix/share/wireplumber */
} WpLookupDirs;

WP_API
gchar * wp_find_file (WpLookupDirs dirs, const gchar *filename,
    const char *subdir);

WP_API
WpIterator * wp_new_files_iterator (WpLookupDirs dirs, const gchar *subdir,
    const gchar *suffix);

G_END_DECLS

#endif
