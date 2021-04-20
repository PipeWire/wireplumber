/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "private.h"
#include "protocol.h"
#include "sender.h"
#include "client.h"

#define BUFFER_SIZE 1024

static void
on_lost_connection (struct wpipc_sender *self,
                               int receiver_fd,
                               void *data)
{
  wpipc_log_warn ("client: lost connection with server %d", receiver_fd);
}

/* API */

struct wpipc_client *
wpipc_client_new (const char *path, bool connect)
{
  struct wpipc_sender *base;
  base = wpipc_sender_new (path, BUFFER_SIZE, on_lost_connection, NULL, 0);

  if (connect)
    wpipc_sender_connect (base);

  return (struct wpipc_client *)base;
}

void
wpipc_client_free (struct wpipc_client *self)
{
  struct wpipc_sender *base = wpipc_client_to_sender (self);
  wpipc_sender_free (base);
}

bool
wpipc_client_send_request (struct wpipc_client *self,
                           const char *name,
                           const struct spa_pod *args,
                           wpipc_sender_reply_func_t reply,
                           void *data)
{
  struct wpipc_sender *base = wpipc_client_to_sender (self);

  /* check params */
  if (name == NULL)
    return false;

  const size_t size = wpipc_protocol_calculate_request_size (name, args);
  uint8_t buffer[size];
  wpipc_protocol_build_request (buffer, size, name, args);
  return wpipc_sender_send (base, buffer, size, reply, data);
}

const struct spa_pod *
wpipc_client_send_request_finish (struct wpipc_sender *self,
                                  const uint8_t *buffer,
                                  size_t size,
                                  const char **error)
{
  /* error */
  if (wpipc_protocol_is_reply_error (buffer, size)) {
    wpipc_protocol_parse_reply_error (buffer, size, error);
    return NULL;
  }

  /* ok */
  if (wpipc_protocol_is_reply_ok (buffer, size)) {
    const struct spa_pod *value = NULL;
    if (wpipc_protocol_parse_reply_ok (buffer, size, &value))
      return value;
  }

  return NULL;
}
