/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_SETTINGS_H__
#define __WIREPLUMBER_SETTINGS_H__

#include "object.h"

G_BEGIN_DECLS

/*!
 * \brief Flags to be used as WpObjectFeatures on WpSettings subclasses.
 * \ingroup wpsettings
 */
typedef enum {
  /* loads the metadata */
  WP_SETTINGS_LOADED = (1 << 0),
} WpSettingsFeatures;

/*!
 * \brief The WpSettings GType
 * \ingroup wpsettings
 */
#define WP_TYPE_SETTINGS (wp_settings_get_type ())

WP_API
G_DECLARE_FINAL_TYPE (WpSettings, wp_settings, WP, SETTINGS, WpObject)

WP_API
WpSettings * wp_settings_get_instance (WpCore * core,
    const gchar *metadata_name);

WP_API
gboolean wp_settings_get_boolean (WpSettings *self, const gchar *setting);

WP_API
gboolean wp_settings_apply_rule (WpSettings *self, const gchar *rule,
    WpProperties *client_props, WpProperties *applied_props);

G_END_DECLS

#endif
