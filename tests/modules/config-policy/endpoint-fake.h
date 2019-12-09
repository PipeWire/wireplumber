/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_FAKE_ENDPOINT_H__
#define __WIREPLUMBER_FAKE_ENDPOINT_H__

#include <wp/wp.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE (WpFakeEndpoint, wp_fake_endpoint, WP, FAKE_ENDPOINT,
    WpBaseEndpoint)

void
wp_fake_endpoint_new_async (WpCore *core, const char *name,
    const char *media_class, guint direction,
    WpProperties *props, const char *role, guint streams,
    GAsyncReadyCallback ready, gpointer data);

guint wp_fake_endpoint_get_id (WpFakeEndpoint *self);

G_END_DECLS

#endif
