/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_ENDPOINT_AUDIOTESTSRC_H__
#define __WIREPLUMBER_ENDPOINT_AUDIOTESTSRC_H__

#include <wp/wp.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE (WpEndpointAudiotestsrc, wp_endpoint_audiotestsrc, WP,
    ENDPOINT_AUDIOTESTSRC, WpBaseEndpoint)

void wp_endpoint_audiotestsrc_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer data);

G_END_DECLS

#endif
