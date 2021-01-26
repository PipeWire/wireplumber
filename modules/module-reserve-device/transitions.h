/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_RESERVE_DEVICE_TRANSITIONS_H__
#define __WIREPLUMBER_RESERVE_DEVICE_TRANSITIONS_H__

#include "reserve-device.h"

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE (WpReserveDeviceAcquireTransition,
    wp_reserve_device_acquire_transition,
    WP, RESERVE_DEVICE_ACQUIRE_TRANSITION, WpTransition)

WpTransition * wp_reserve_device_acquire_transition_new (WpReserveDevice *rd,
    GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer callback_data);

void wp_reserve_device_acquire_transition_name_acquired (WpTransition * tr);
void wp_reserve_device_acquire_transition_name_lost (WpTransition * tr);

gboolean wp_reserve_device_acquire_transition_finish (GAsyncResult * res,
    GError ** error);

G_END_DECLS

#endif
