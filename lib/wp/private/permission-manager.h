/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_PERMISSION_MANAGER_H__
#define __WIREPLUMBER_PRIVATE_PERMISSION_MANAGER_H__

#include "client.h"

G_BEGIN_DECLS

typedef struct _WpPermissionManager WpPermissionManager;

void wp_permission_manager_add_client (WpPermissionManager *self,
    WpClient *client);

void wp_permission_manager_remove_client (WpPermissionManager *self,
    WpClient *client);

G_END_DECLS

#endif
