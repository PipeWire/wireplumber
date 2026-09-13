/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_STREAM_H__
#define __WIREPLUMBER_PRIVATE_STREAM_H__

#include "../stream.h"

G_BEGIN_DECLS

/* Audio Stream */

WpAudioStream * wp_audio_stream_new (WpNode *node, WpProperties *props,
    WpDirection dir, const gchar *port_group);

gboolean wp_audio_stream_has_pending_ports_config (WpAudioStream *self);


/* Video Stream */

WpVideoStream * wp_video_stream_new (WpNode *node, WpProperties *props,
    WpDirection dir, const gchar *port_group);

G_END_DECLS

#endif
