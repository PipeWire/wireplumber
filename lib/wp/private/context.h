/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_CONTEXT_H__
#define __WIREPLUMBER_PRIVATE_CONTEXT_H__

#include "../context.h"

G_BEGIN_DECLS

/* Main Context */

WpMainContext * wp_main_context_new (WpCore * core,
    GMainContext * g_main_context, WpProperties * properties,
    WpSpaJson * spa_libs, WpSpaJson * modules);

G_END_DECLS

#endif
