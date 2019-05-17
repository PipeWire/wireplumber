/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WIREPLUMBER_SESSION_MANAGER_H__
#define __WIREPLUMBER_SESSION_MANAGER_H__

#include "endpoint.h"

G_BEGIN_DECLS

#define WP_TYPE_SESSION_MANAGER (wp_session_manager_get_type ())
G_DECLARE_FINAL_TYPE (WpSessionManager, wp_session_manager, WP, SESSION_MANAGER, GObject)

#define WP_GLOBAL_SESSION_MANAGER (wp_global_session_manager_quark ())
GQuark wp_global_session_manager_quark (void);

WpSessionManager * wp_session_manager_new (void);

void wp_session_manager_register_endpoint (WpSessionManager * self,
    WpEndpoint * ep);
void wp_session_manager_remove_endpoint (WpSessionManager * self,
    WpEndpoint * ep);

GPtrArray * wp_session_manager_find_endpoints (WpSessionManager * self,
    const gchar * media_class_lookup);

G_END_DECLS

#endif
