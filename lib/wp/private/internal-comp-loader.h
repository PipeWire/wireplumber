/* WirePlumber
 *
 * Copyright © 2023 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_INTERNAL_COMP_LOADER_H__
#define __WIREPLUMBER_INTERNAL_COMP_LOADER_H__

#include "component-loader.h"

G_BEGIN_DECLS

#define WP_TYPE_INTERNAL_COMP_LOADER (wp_internal_comp_loader_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpInternalCompLoader, wp_internal_comp_loader,
                      WP, INTERNAL_COMP_LOADER, GObject)

G_END_DECLS

#endif
