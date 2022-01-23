/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

enum {
  AUDIO_SINK,
  AUDIO_SOURCE,
  VIDEO_SOURCE,
  N_DEFAULT_NODES
};

static const gchar * DEFAULT_KEY[N_DEFAULT_NODES] = {
  [AUDIO_SINK] = "default.audio.sink",
  [AUDIO_SOURCE] = "default.audio.source",
  [VIDEO_SOURCE] = "default.video.source",
};

static const gchar * NODE_TYPE_STR[N_DEFAULT_NODES] = {
  [AUDIO_SINK] = "Audio/Sink",
  [AUDIO_SOURCE] = "Audio/Source",
  [VIDEO_SOURCE] = "Video/Source",
};

static const gchar * DEFAULT_CONFIG_KEY[N_DEFAULT_NODES] = {
  [AUDIO_SINK] = "default.configured.audio.sink",
  [AUDIO_SOURCE] = "default.configured.audio.source",
  [VIDEO_SOURCE] = "default.configured.video.source",
};
