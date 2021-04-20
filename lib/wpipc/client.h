/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WPIPC_CLIENT_H__
#define __WPIPC_CLIENT_H__

#include <spa/pod/pod.h>

#include <stddef.h>

#include "sender.h"
#include "defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define wpipc_client_to_sender(self) ((struct wpipc_sender *)(self))

struct wpipc_client;

WPIPC_API
struct wpipc_client *
wpipc_client_new (const char *path, bool connect);

WPIPC_API
void
wpipc_client_free (struct wpipc_client *self);

WPIPC_API
bool
wpipc_client_send_request (struct wpipc_client *self,
                           const char *name,
                           const struct spa_pod *args,
                           wpipc_sender_reply_func_t reply,
                           void *data);

/* for reply handlers only */

WPIPC_API
const struct spa_pod *
wpipc_client_send_request_finish (struct wpipc_sender *self,
                                  const uint8_t *buffer,
                                  size_t size,
                                  const char **error);

#ifdef __cplusplus
}
#endif

#endif
