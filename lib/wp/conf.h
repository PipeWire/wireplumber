/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CONF_H__
#define __WIREPLUMBER_CONF_H__

#include "spa-json.h"
#include "properties.h"

G_BEGIN_DECLS

/*!
 * \brief The WpConf GType
 * \ingroup wpconf
 */
#define WP_TYPE_CONF (wp_conf_get_type ())

WP_API
G_DECLARE_FINAL_TYPE (WpConf, wp_conf, WP, CONF, GObject)

WP_API
WpConf * wp_conf_get_instance (WpCore * core);

WP_API
WpSpaJson * wp_conf_get_section (WpConf *self, const gchar *section,
    WpSpaJson *fallback);

WP_API
WpSpaJson *wp_conf_get_value (WpConf *self,
    const gchar *section, const gchar *key, WpSpaJson *fallback);

WP_API
gboolean wp_conf_get_value_boolean (WpConf *self,
    const gchar *section, const gchar *key, gboolean fallback);

WP_API
gint wp_conf_get_value_int (WpConf *self,
    const gchar *section, const gchar *key, gint fallback);

WP_API
float wp_conf_get_value_float (WpConf *self,
    const gchar *section, const gchar *key, float fallback);

WP_API
gchar *wp_conf_get_value_string (WpConf *self,
    const gchar *section, const gchar *key, const gchar *fallback);

G_END_DECLS

#endif
