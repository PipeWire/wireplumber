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
