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
WpNode * wp_audio_stream_get_node (WpAudioStream * self);
const struct pw_node_info * wp_audio_stream_get_info (WpAudioStream * self);
gboolean wp_audio_stream_prepare_link (WpAudioStream * self,
      GVariant ** properties, GError ** error);

gfloat wp_audio_stream_get_volume (WpAudioStream * self);
gboolean wp_audio_stream_get_mute (WpAudioStream * self);
void wp_audio_stream_set_volume (WpAudioStream * self, gfloat volume);
void wp_audio_stream_set_mute (WpAudioStream * self, gboolean mute);

/* for subclasses */

WpCore *wp_audio_stream_get_core (WpAudioStream * self);
void wp_audio_stream_init_task_finish (WpAudioStream * self, GError * error);
void wp_audio_stream_set_port_config (WpAudioStream * self,
    const struct spa_pod * param);
void wp_audio_stream_finish_port_config (WpAudioStream * self);

G_END_DECLS

#endif
