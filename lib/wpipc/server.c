/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pthread.h>

#include "private.h"
#include "protocol.h"
#include "receiver.h"
#include "server.h"

#define BUFFER_SIZE 1024
#define MAX_REQUEST_HANDLERS 128

struct wpipc_server_client_handler
{
  wpipc_server_client_handler_func_t handler;
  void *data;
};

struct wpipc_server_request_handler
{
  const char *name;
  wpipc_server_request_handler_func_t handler;
  void *data;
};

struct wpipc_server_priv {
  pthread_mutex_t mutex;
  struct wpipc_server_client_handler client_handler;
  size_t n_request_handlers;
  struct wpipc_server_request_handler request_handlers[MAX_REQUEST_HANDLERS];
};

static void
sender_state (struct wpipc_receiver *base,
              int sender_fd,
              enum wpipc_receiver_sender_state sender_state,
              void *data)
{
  struct wpipc_server_priv *priv = wpipc_receiver_get_user_data (base);

  wpipc_log_info ("server: new state %d on client %d", sender_state, sender_fd);

  pthread_mutex_lock (&priv->mutex);
  if (priv->client_handler.handler)
    priv->client_handler.handler ((struct wpipc_server *)base, sender_fd,
        sender_state, priv->client_handler.data);
  pthread_mutex_unlock (&priv->mutex);
}

static bool
handle_message (struct wpipc_receiver *base,
                int sender_fd,
                const uint8_t *buffer,
                size_t size,
                void *data)
{
  struct wpipc_server_priv *priv = wpipc_receiver_get_user_data (base);
  const char *name = NULL;
  const struct spa_pod *args = NULL;

  wpipc_log_info ("server: message from client %d received", sender_fd);

  /* parse */
  if (!wpipc_protocol_parse_request (buffer, size, &name, &args)) {
    const char *msg = "could not parse request";
    const size_t s = wpipc_protocol_calculate_reply_error_size (msg);
    uint8_t b[s];
    wpipc_protocol_build_reply_error (b, s, msg);
    return wpipc_socket_write (sender_fd, b, s) == (ssize_t)s;
  }

  /* handle */
  size_t i;
  bool res = false;
  pthread_mutex_lock (&priv->mutex);

  for (i = 0; i < MAX_REQUEST_HANDLERS; i++) {
    struct wpipc_server_request_handler *rh = priv->request_handlers + i;
    if (rh->name != NULL && strcmp (rh->name, name) == 0 &&
        rh->handler != NULL) {
      res = rh->handler ((struct wpipc_server *)base, sender_fd, name, args,
          rh->data);
      pthread_mutex_unlock (&priv->mutex);
      return res;
    }
  }

  /* handler was not found, reply with error */
  res = wpipc_server_reply_error ((struct wpipc_server *)base, sender_fd,
      "request handler not found");

  pthread_mutex_unlock (&priv->mutex);
  return res;
}

static struct wpipc_receiver_events events = {
  .sender_state = sender_state,
  .handle_message = handle_message,
};

/* API */

struct wpipc_server *
wpipc_server_new (const char *path, bool start)
{
  struct wpipc_server_priv * priv = NULL;
  struct wpipc_receiver *base = NULL;

  base = wpipc_receiver_new (path, BUFFER_SIZE, &events, NULL,
      sizeof (struct wpipc_server_priv));
  if (base == NULL)
    return NULL;

  priv = wpipc_receiver_get_user_data (base);
  pthread_mutex_init (&priv->mutex, NULL);
  priv->n_request_handlers = 0;

  if (start && !wpipc_receiver_start (base)) {
    wpipc_log_error ("failed to start receiver");
    wpipc_server_free ((struct wpipc_server *)base);
    return NULL;
  }

  return (struct wpipc_server *)base;
}

void
wpipc_server_free (struct wpipc_server *self)
{
  struct wpipc_receiver *base = wpipc_server_to_receiver (self);
  struct wpipc_server_priv *priv = wpipc_receiver_get_user_data (base);

  pthread_mutex_destroy (&priv->mutex);

  wpipc_receiver_free (base);
}

void
wpipc_server_set_client_handler (struct wpipc_server *self,
                                 wpipc_server_client_handler_func_t handler,
                                 void *data)
{
  struct wpipc_receiver *base = wpipc_server_to_receiver (self);
  struct wpipc_server_priv *priv = wpipc_receiver_get_user_data (base);

  pthread_mutex_lock (&priv->mutex);
  priv->client_handler.handler = handler;
  priv->client_handler.data = data;
  pthread_mutex_unlock (&priv->mutex);
}

void
wpipc_server_clear_client_handler (struct wpipc_server *self)
{
  struct wpipc_receiver *base = wpipc_server_to_receiver (self);
  struct wpipc_server_priv *priv = wpipc_receiver_get_user_data (base);

  pthread_mutex_lock (&priv->mutex);
  priv->client_handler.handler = NULL;
  priv->client_handler.data = NULL;
  pthread_mutex_unlock (&priv->mutex);
}

bool
wpipc_server_set_request_handler (struct wpipc_server *self,
                                  const char *name,
                                  wpipc_server_request_handler_func_t handler,
                                  void *data)
{
  struct wpipc_receiver *base = wpipc_server_to_receiver (self);
  struct wpipc_server_priv *priv = wpipc_receiver_get_user_data (base);
  size_t i;

  /* check params */
  if (name == NULL)
    return false;

  pthread_mutex_lock (&priv->mutex);

  /* make sure handler does not exist */
  for (i = 0; i < MAX_REQUEST_HANDLERS; i++) {
    struct wpipc_server_request_handler *rh = priv->request_handlers + i;
    if (rh->name != NULL && strcmp (rh->name, name) == 0) {
      pthread_mutex_unlock (&priv->mutex);
      return false;
    }
  }

  /* set handler */
  for (i = 0; i < MAX_REQUEST_HANDLERS; i++) {
    struct wpipc_server_request_handler *rh = priv->request_handlers + i;
    if (rh->name == NULL) {
      rh->name = name;
      rh->handler = handler;
      rh->data = data;
      pthread_mutex_unlock (&priv->mutex);
      return true;
    }
  }

  pthread_mutex_unlock (&priv->mutex);

  return false;
}

void
wpipc_server_clear_request_handler (struct wpipc_server *self,
                                    const char *name)
{
  struct wpipc_receiver *base = wpipc_server_to_receiver (self);
  struct wpipc_server_priv *priv = wpipc_receiver_get_user_data (base);
  size_t i;

  /* check params */
  if (name == NULL)
    return;

  pthread_mutex_lock (&priv->mutex);

  /* clear handler */
  for (i = 0; i < MAX_REQUEST_HANDLERS; i++) {
    struct wpipc_server_request_handler *rh = priv->request_handlers + i;
    if (rh->name != NULL && strcmp (rh->name, name) == 0) {
      rh->name = NULL;
      break;
    }
  }

  pthread_mutex_unlock (&priv->mutex);
}

bool
wpipc_server_reply_ok (struct wpipc_server *self,
                       int client_fd,
                       const struct spa_pod *value)
{
  const size_t s = wpipc_protocol_calculate_reply_ok_size (value);
  uint8_t b[s];
  wpipc_protocol_build_reply_ok (b, s, value);
  return wpipc_socket_write (client_fd, b, s) == (ssize_t)s;
}

bool
wpipc_server_reply_error (struct wpipc_server *self,
                          int client_fd,
                          const char *msg)
{
  if (msg == NULL)
    return false;

  const size_t s = wpipc_protocol_calculate_reply_error_size (msg);
  uint8_t b[s];
  wpipc_protocol_build_reply_error (b, s, msg);
  return wpipc_socket_write (client_fd, b, s) == (ssize_t)s;
}
