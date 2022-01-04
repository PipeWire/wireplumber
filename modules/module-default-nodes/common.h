/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <glib.h>
#include <errno.h>
#include <spa/utils/json.h>

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

#if COMPILING_MODULE_DEFAULT_NODES

static const gchar * DEFAULT_CONFIG_KEY[N_DEFAULT_NODES] = {
  [AUDIO_SINK] = "default.configured.audio.sink",
  [AUDIO_SOURCE] = "default.configured.audio.source",
  [VIDEO_SOURCE] = "default.configured.video.source",
};

static const gchar * MEDIA_CLASS_MATCH[N_DEFAULT_NODES] = {
  [AUDIO_SINK] = "Audio/*",
  [AUDIO_SOURCE] = "Audio/*",
  [VIDEO_SOURCE] = "Video/*",
};

static const gchar * N_PORTS_KEY[N_DEFAULT_NODES] = {
  [AUDIO_SINK] = "n-input-ports",
  [AUDIO_SOURCE] = "n-output-ports",
  [VIDEO_SOURCE] = "n-output-ports",
};

#endif

static int
json_object_find (const char *obj, const char *key, char *value, size_t len)
{
  struct spa_json it[2];
  const char *v;
  char k[128];

  spa_json_init(&it[0], obj, strlen(obj));
  if (spa_json_enter_object(&it[0], &it[1]) <= 0)
    return -EINVAL;

  while (spa_json_get_string(&it[1], k, sizeof(k)) > 0) {
    if (strcmp(k, key) == 0) {
      if (spa_json_get_string(&it[1], value, len) <= 0)
        continue;
      return 0;
    } else {
      if (spa_json_next(&it[1], &v) <= 0)
        break;
    }
  }
  return -ENOENT;
}
