/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_AUDIO_ADAPTER_H__
#define __WIREPLUMBER_AUDIO_ADAPTER_H__

#include <gio/gio.h>
#include <wp/wp.h>

#include "stream.h"

G_BEGIN_DECLS

#define WP_TYPE_AUDIO_ADAPTER (wp_audio_adapter_get_type ())
G_DECLARE_FINAL_TYPE (WpAudioAdapter, wp_audio_adapter, WP, AUDIO_ADAPTER,
    WpAudioStream)

void wp_audio_adapter_new (WpEndpoint *endpoint, guint stream_id,
    const char *stream_name, enum pw_direction direction, guint adapter_id,
    gboolean convert, GAsyncReadyCallback callback, gpointer user_data);

guint wp_audio_adapter_get_adapter_id (WpAudioAdapter *self);
gboolean wp_audio_adapter_is_convert (WpAudioAdapter *self);

G_END_DECLS

#endif
