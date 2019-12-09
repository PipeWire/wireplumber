/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_AUDIO_CONVERT_H__
#define __WIREPLUMBER_AUDIO_CONVERT_H__

#include <gio/gio.h>
#include <wp/wp.h>

#include "stream.h"

G_BEGIN_DECLS

struct spa_audio_info_raw format;

#define WP_TYPE_AUDIO_CONVERT (wp_audio_convert_get_type ())
G_DECLARE_FINAL_TYPE (WpAudioConvert, wp_audio_convert, WP, AUDIO_CONVERT,
    WpAudioStream)

void wp_audio_convert_new (WpBaseEndpoint *endpoint, guint stream_id,
    const char *stream_name, enum pw_direction direction,
    WpAudioStream *target, const struct spa_audio_info_raw *format,
    GAsyncReadyCallback callback, gpointer user_data);

G_END_DECLS

#endif
