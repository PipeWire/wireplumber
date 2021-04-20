/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WPIPC_SERVER_H__
#define __WPIPC_SERVER_H__

#include <spa/pod/pod.h>

#include "defs.h"

#include "receiver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define wpipc_server_to_receiver(self) ((struct wpipc_receiver *)(self))

struct wpipc_server;

typedef void (*wpipc_server_client_handler_func_t) (struct wpipc_server *self,
                                                    int client_fd,
                                                    enum wpipc_receiver_sender_state client_state,
                                                    void *data);

typedef bool (*wpipc_server_request_handler_func_t) (struct wpipc_server *self,
                                                     int client_fd,
                                                     const char *name,
                                                     const struct spa_pod *args,
                                                     void *data);

WPIPC_API
struct wpipc_server *
wpipc_server_new (const char *path, bool start);

WPIPC_API
void
wpipc_server_free (struct wpipc_server *self);

WPIPC_API
void
wpipc_server_set_client_handler (struct wpipc_server *self,
                                 wpipc_server_client_handler_func_t handler,
                                 void *data);

WPIPC_API
void
wpipc_server_clear_client_handler (struct wpipc_server *self);

WPIPC_API
bool
wpipc_server_set_request_handler (struct wpipc_server *self,
                                  const char *name,
                                  wpipc_server_request_handler_func_t handler,
                                  void *data);

WPIPC_API
void
wpipc_server_clear_request_handler (struct wpipc_server *self,
                                    const char *name);

/* for request handlers only */

WPIPC_API
bool
wpipc_server_reply_ok (struct wpipc_server *self,
                       int client_fd,
                       const struct spa_pod *value);

WPIPC_API
bool
wpipc_server_reply_error (struct wpipc_server *self,
                          int client_fd,
                          const char *msg);

#ifdef __cplusplus
}
#endif

#endif
