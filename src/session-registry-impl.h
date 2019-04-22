/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WP_SESSION_REGISTRY_IMPL_H__
#define __WP_SESSION_REGISTRY_IMPL_H__

#include <wp/core-interfaces.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE (WpSessionRegistryImpl, wp_session_registry_impl,
                      WP, SESSION_REGISTRY_IMPL, WpInterfaceImpl)

WpSessionRegistryImpl * wp_session_registry_impl_new (void);

G_END_DECLS

#endif
