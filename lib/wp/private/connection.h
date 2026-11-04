/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_CONNECTION_H__
#define __WIREPLUMBER_PRIVATE_CONNECTION_H__

#include "../connection.h"

G_BEGIN_DECLS

WpConnection * wp_connection_new (WpCore * core, WpContext * context);

gboolean wp_connection_connect (WpConnection * self, gint socket_fd);

void wp_connection_disconnect (WpConnection * self);

G_END_DECLS

#endif
