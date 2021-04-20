/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WPIPC_SENDER_H__
#define __WPIPC_SENDER_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "defs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wpipc_sender;

typedef void (*wpipc_sender_lost_conn_func_t) (struct wpipc_sender *self,
                                               int receiver_fd,
                                               void *data);

typedef void (*wpipc_sender_reply_func_t) (struct wpipc_sender *self,
                                           const uint8_t *buffer,
                                           size_t size,
                                           void *data);

WPIPC_API
struct wpipc_sender *
wpipc_sender_new (const char *path,
                  size_t buffer_size,
                  wpipc_sender_lost_conn_func_t lost_func,
                  void *lost_data,
                  size_t user_size);

WPIPC_API
void
wpipc_sender_free (struct wpipc_sender *self);

WPIPC_API
bool
wpipc_sender_connect (struct wpipc_sender *self);

WPIPC_API
void
wpipc_sender_disconnect (struct wpipc_sender *self);

WPIPC_API
bool
wpipc_sender_is_connected (struct wpipc_sender *self);

WPIPC_API
bool
wpipc_sender_send (struct wpipc_sender *self,
                   const uint8_t *buffer,
                   size_t size,
                   wpipc_sender_reply_func_t reply,
                   void *data);

/* for subclasses only */

WPIPC_API
void *
wpipc_sender_get_user_data (struct wpipc_sender *self);

#ifdef __cplusplus
}
#endif

#endif
