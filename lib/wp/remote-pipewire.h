/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_REMOTE_PIPEWIRE_H__
#define __WIREPLUMBER_REMOTE_PIPEWIRE_H__

#include "remote.h"

G_BEGIN_DECLS

#define WP_TYPE_REMOTE_PIPEWIRE (wp_remote_pipewire_get_type ())
G_DECLARE_FINAL_TYPE (WpRemotePipewire, wp_remote_pipewire,
    WP, REMOTE_PIPEWIRE, WpRemote)

WpRemote *wp_remote_pipewire_new (WpCore *core, GMainContext *context);

G_END_DECLS

#endif
