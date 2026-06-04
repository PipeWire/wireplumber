/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_STREAM_MANAGER_H__
#define __WIREPLUMBER_STREAM_MANAGER_H__

#include "object.h"

G_BEGIN_DECLS

/*!
 * \brief Flags to be used as WpObjectFeatures on WpStreamManager.
 * \ingroup wpstreammanager
 */
typedef enum { /*< flags >*/
  /*! Loads the stream manager */
  WP_STREAM_MANAGER_FEATURE_LOADED = (1 << 0),
} WpStreamManagerFeatures;

/*!
 * \brief The WpStreamManager GType
 * \ingroup wpstreammanager
 */
#define WP_TYPE_STREAM_MANAGER (wp_stream_manager_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpStreamManager, wp_stream_manager, WP, STREAM_MANAGER,
    WpObject)

WP_API
WpStreamManager * wp_stream_manager_new (WpCore *core);

G_END_DECLS

#endif
