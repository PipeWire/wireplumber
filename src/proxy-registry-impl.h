/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WP_PROXY_REGISTRY_IMPL_H__
#define __WP_PROXY_REGISTRY_IMPL_H__

#include <wp/core-interfaces.h>

G_BEGIN_DECLS

struct pw_remote;

G_DECLARE_FINAL_TYPE (WpProxyRegistryImpl, wp_proxy_registry_impl,
                      WP, PROXY_REGISTRY_IMPL, WpInterfaceImpl)

WpProxyRegistryImpl * wp_proxy_registry_impl_new (struct pw_remote * remote);

void wp_proxy_registry_impl_unload (WpProxyRegistryImpl * self);

G_END_DECLS

#endif
