/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CORE_H__
#define __WIREPLUMBER_CORE_H__

#include "object.h"
#include "context.h"
#include "connection.h"
#include "registry.h"

G_BEGIN_DECLS

/*!
 * \brief Flags to be used as WpObjectFeatures on WpCore
 * \ingroup wpcore
 */
typedef enum { /*< flags >*/
  /*! connects to pipewire */
  WP_CORE_FEATURE_CONNECTED = (1 << 0),
  /*! loads components defined in the configuration */
  WP_CORE_FEATURE_COMPONENTS = (1 << 1),
} WpCoreFeatures;

/*!
 * \brief The WpCore GType
 * \ingroup wpcore
 */
#define WP_TYPE_CORE (wp_core_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpCore, wp_core, WP, CORE, WpObject)


/* Basic */

WP_API
WpCore * wp_core_new (GMainContext * context, WpConf * conf,
    WpProperties * properties);

WP_API
WpConf * wp_core_get_conf (WpCore * self);

WP_API
WpMainContext * wp_core_get_main_context (WpCore * self);

WP_API
WpConnection * wp_core_get_connection (WpCore * self);

WP_API
WpRegistry * wp_core_get_registry (WpCore * self);

G_END_DECLS

#endif
