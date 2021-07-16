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
#include "device.h"
#include "endpoint.h"
#include "error.h"
#include "global-proxy.h"
#include "iterator.h"
#include "link.h"
#include "log.h"
#include "metadata.h"
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
#include "spa-pod.h"
#include "spa-type.h"
#include "state.h"
#include "transition.h"
#include "wpenums.h"
#include "wpversion.h"

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
const gchar * wp_get_module_dir (void);

WP_API
const gchar * wp_get_xdg_state_dir (void);

WP_API
const gchar * wp_get_xdg_config_dir (void);

WP_API
G_DEPRECATED_FOR (wp_find_config_file)
const gchar * wp_get_config_dir (void);

WP_API
G_DEPRECATED_FOR (wp_find_sysconfig_file)
const gchar * wp_get_data_dir (void);

WP_API
gchar * wp_find_config_file (const gchar *filename, const char *subdir);

WP_API
gchar * wp_find_sysconfig_file (const gchar *filename, const char *subdir);

typedef gint (*wp_file_iter_func)(const gchar *filename, gpointer user_data,
                                  GError **error);

WP_API
gint wp_iter_config_files (const gchar *subdir, const gchar *suffix,
                           wp_file_iter_func func, gpointer user_data,
                           GError **error);

G_END_DECLS

#endif
