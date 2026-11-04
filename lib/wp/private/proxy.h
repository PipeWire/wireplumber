/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_PROXY_H__
#define __WIREPLUMBER_PRIVATE_PROXY_H__

#include "../proxy.h"
#include "client-context.h"

G_BEGIN_DECLS

void wp_proxy_set_client_context (WpProxy * self, WpClientContext * ctx);

WpClientContext * wp_proxy_get_client_context (WpProxy * self);

struct pw_core * wp_proxy_get_export_pw_core (WpProxy * self, WpCore * core);

void wp_proxy_lock (WpProxy * self);

void wp_proxy_unlock (WpProxy * self);

G_END_DECLS

#endif
