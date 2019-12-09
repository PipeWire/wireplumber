/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_FAKE_ENDPOINT_LINK_H__
#define __WIREPLUMBER_FAKE_ENDPOINT_LINK_H__

#include <wp/wp.h>

#define WP_FAKE_ENDPOINT_LINK_FACTORY_NAME "endpoint-link-fake"

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE (WpFakeEndpointLink, wp_fake_endpoint_link, WP,
    FAKE_ENDPOINT_LINK, WpBaseEndpointLink)

void wp_fake_endpoint_link_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer data);

guint wp_fake_endpoint_link_get_id (WpFakeEndpointLink *self);

G_END_DECLS

#endif
