/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

struct spa_audio_info_raw;
gboolean choose_sensible_raw_audio_format (WpIterator *formats,
    gint channels_preference, struct spa_audio_info_raw *result);
