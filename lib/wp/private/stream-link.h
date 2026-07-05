/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_STREAM_LINK_H__
#define __WIREPLUMBER_PRIVATE_STREAM_LINK_H__

#include "../stream-link.h"

G_BEGIN_DECLS

/* Audio Stream Link */

WpAudioStreamLink * wp_audio_stream_link_new (WpCore *core,
    WpAudioStream *output, WpAudioStream *input);


/* Video Stream Link */

WpVideoStreamLink * wp_video_stream_link_new (WpCore *core,
    WpVideoStream *output, WpVideoStream *input);

G_END_DECLS

#endif
