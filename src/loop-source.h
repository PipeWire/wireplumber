/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WIREPLUMBER_LOOP_SOURCE_H__
#define __WIREPLUMBER_LOOP_SOURCE_H__

#include <glib.h>
#include <pipewire/pipewire.h>

G_BEGIN_DECLS

/*
 * A GSource that integrates a pw_loop with GMainLoop.
 * Use g_source_ref/unref to manage lifetime.
 * The pw_loop is owned by the GSource.
 */

GSource * wp_loop_source_new (void);
struct pw_loop * wp_loop_source_get_loop (GSource * s);

G_END_DECLS

#endif
