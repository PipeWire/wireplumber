/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_ENDPOINT_LINK_FAKE_H__
#define __WIREPLUMBER_ENDPOINT_LINK_FAKE_H__

#include <wp/wp.h>

#define WP_ENDPOINT_LINK_FAKE_FACTORY_NAME "endpoint-link-fake"

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE (WpEndpointLinkFake, wp_endpoint_link_fake, WP,
    ENDPOINT_LINK_FAKE, WpEndpointLink)

void wp_endpoint_link_fake_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer data);

guint wp_endpoint_link_fake_get_id (WpEndpointLinkFake *self);

G_END_DECLS

#endif
