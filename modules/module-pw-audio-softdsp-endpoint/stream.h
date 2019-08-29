/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_AUDIO_STREAM_H__
#define __WIREPLUMBER_AUDIO_STREAM_H__

#include <gio/gio.h>
#include <wp/wp.h>

G_BEGIN_DECLS

guint wp_audio_stream_id_encode (guint stream_id, guint control_id);
void wp_audio_stream_id_decode (guint id, guint *stream_id, guint *control_id);

#define WP_TYPE_AUDIO_STREAM (wp_audio_stream_get_type ())
G_DECLARE_DERIVABLE_TYPE (WpAudioStream, wp_audio_stream, WP, AUDIO_STREAM, GObject)

/* The audio stream base class */
struct _WpAudioStreamClass
{
  GObjectClass parent_class;
};

WpAudioStream * wp_audio_stream_new_finish (GObject *initable,
    GAsyncResult *res, GError **error);
const char *wp_audio_stream_get_name (WpAudioStream * self);
enum pw_direction wp_audio_stream_get_direction (WpAudioStream * self);
WpProxyNode * wp_audio_stream_get_proxy_node (WpAudioStream * self);
const struct pw_node_info * wp_audio_stream_get_info (WpAudioStream * self);
gboolean wp_audio_stream_prepare_link (WpAudioStream * self,
      GVariant ** properties, GError ** error);
GVariant * wp_audio_stream_get_control_value (WpAudioStream * self,
    guint32 control_id);
gboolean wp_audio_stream_set_control_value (WpAudioStream * self,
    guint32 control_id, GVariant * value);

/* for subclasses */

WpRemotePipewire *wp_audio_stream_get_remote (WpAudioStream * self);
void wp_audio_stream_init_task_finish (WpAudioStream * self, GError * error);
void wp_audio_stream_set_port_config (WpAudioStream * self,
    const struct spa_pod * param);

G_END_DECLS

#endif
