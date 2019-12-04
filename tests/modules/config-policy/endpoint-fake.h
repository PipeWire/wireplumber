/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_ENDPOINT_FAKE_H__
#define __WIREPLUMBER_ENDPOINT_FAKE_H__

#include <wp/wp.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE (WpEndpointFake, wp_endpoint_fake, WP, ENDPOINT_FAKE,
    WpEndpoint)

void
wp_endpoint_fake_new_async (WpCore *core, const char *name,
    const char *media_class, guint direction,
    WpProperties *props, const char *role, guint streams,
    GAsyncReadyCallback ready, gpointer data);

guint wp_endpoint_fake_get_id (WpEndpointFake *self);

G_END_DECLS

#endif
