/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_LIMITED_CREATION_BLUEZ5_H__
#define __WIREPLUMBER_LIMITED_CREATION_BLUEZ5_H__

#include <wp/wp.h>

#include "limited-creation.h"

G_BEGIN_DECLS

#define WP_TYPE_LIMITED_CREATION_BLUEZ5 (wp_limited_creation_bluez5_get_type ())
G_DECLARE_FINAL_TYPE (WpLimitedCreationBluez5, wp_limited_creation_bluez5, WP,
    LIMITED_CREATION_BLUEZ5, WpLimitedCreation);

WpLimitedCreation * wp_limited_creation_bluez5_new (WpDevice *device);

G_END_DECLS

#endif
