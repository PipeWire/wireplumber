/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CONF_SECTIONS_H__
#define __WIREPLUMBER_CONF_SECTIONS_H__

#include "../spa-json.h"

G_BEGIN_DECLS

struct pw_context;

/*
 * Applies the given "context.spa-libs" and "context.modules" descriptions to
 * \a context. Both are optional; a NULL section is skipped.
 *
 * Returns the number of items that were parsed from \a modules, or -1 on error.
 */
gint wp_pw_context_apply_conf_sections (gpointer log_object,
    struct pw_context * context, WpSpaJson * spa_libs, WpSpaJson * modules,
    GError ** error);

G_END_DECLS

#endif
