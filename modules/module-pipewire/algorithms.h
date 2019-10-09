/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

typedef void (*CreateLinkCb) (WpProperties *, gpointer);
gboolean multiport_link_create (GVariant * src_data, GVariant * sink_data,
    CreateLinkCb create_link_cb, gpointer user_data, GError ** error);

struct spa_audio_info_raw;
gboolean choose_sensible_raw_audio_format (GPtrArray *formats,
    struct spa_audio_info_raw *result);
