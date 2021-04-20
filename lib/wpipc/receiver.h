/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WPIPC_RECEIVER_H__
#define __WPIPC_RECEIVER_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "defs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wpipc_receiver;

enum wpipc_receiver_sender_state {
  WPIPC_RECEIVER_SENDER_STATE_CONNECTED = 0,
  WPIPC_RECEIVER_SENDER_STATE_DISCONNECTED
};

struct wpipc_receiver_events {
  /* emitted when a sender state changes */
  void (*sender_state) (struct wpipc_receiver *self,
                        int sender_fd,
                        enum wpipc_receiver_sender_state state,
                        void *data);

  /* emitted when message is received and needs to be handled */
  bool (*handle_message) (struct wpipc_receiver *self,
                          int sender_fd,
                          const uint8_t *buffer,
                          size_t size,
                          void *data);
};

WPIPC_API
struct wpipc_receiver *
wpipc_receiver_new (const char *path,
                    size_t buffer_size,
                    const struct wpipc_receiver_events *events,
                    void *events_data,
                    size_t user_size);

WPIPC_API
void
wpipc_receiver_free (struct wpipc_receiver *self);

WPIPC_API
bool
wpipc_receiver_start (struct wpipc_receiver *self);

WPIPC_API
void
wpipc_receiver_stop (struct wpipc_receiver *self);

WPIPC_API
bool
wpipc_receiver_is_running (struct wpipc_receiver *self);

/* for subclasses only */

WPIPC_API
void *
wpipc_receiver_get_user_data (struct wpipc_receiver *self);

#ifdef __cplusplus
}
#endif

#endif
