/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <gio/gio.h>
#include <wp/wp.h>

#ifndef __WP_PW_AUDIO_DSP_H__
#define __WP_PW_AUDIO_DSP_H__

G_DECLARE_FINAL_TYPE (WpPwAudioDsp, wp_pw_audio_dsp,
    WP_PW, AUDIO_DSP, GObject)

guint wp_pw_audio_dsp_id_encode (guint stream_id, guint control_id);
void wp_pw_audio_dsp_id_decode (guint id, guint *stream_id, guint *control_id);

void wp_pw_audio_dsp_new (WpEndpoint *endpoint, guint id, const char *name,
    enum pw_direction direction, gboolean convert,
    const struct pw_node_info *target, const struct spa_audio_info_raw *format,
    GAsyncReadyCallback callback, gpointer user_data);
WpPwAudioDsp * wp_pw_audio_dsp_new_finish (GObject *initable, GAsyncResult *res,
    GError **error);

const struct pw_node_info *wp_pw_audio_dsp_get_info (WpPwAudioDsp * self);
gboolean wp_pw_audio_dsp_prepare_link (WpPwAudioDsp * self,
    GVariant ** properties, GError ** error);
GVariant * wp_pw_audio_dsp_get_control_value (WpPwAudioDsp * self,
    guint32 control_id);
gboolean wp_pw_audio_dsp_set_control_value (WpPwAudioDsp * self,
    guint32 control_id, GVariant * value);

#endif
