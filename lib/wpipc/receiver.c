/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "private.h"
#include "receiver.h"

#include "wpipc.h"

#define MAX_SENDERS 128

struct wpipc_receiver {
  struct sockaddr_un addr;
  int socket_fd;

  uint8_t *buffer_read;
  size_t buffer_size;

  struct epoll_thread epoll_thread;
  bool thread_running;

  const struct wpipc_receiver_events *events;
  void *events_data;

  /* for subclasses */
  void *user_data;
};

static bool
reply_message (struct wpipc_receiver *self,
               int sender_fd,
               uint8_t *buffer,
               size_t size)
{
  return self->events && self->events->handle_message ?
    self->events->handle_message (self, sender_fd, buffer, size, self->events_data) :
    wpipc_socket_write (sender_fd, buffer, size) == (ssize_t)size;
}

static void
socket_event_received (struct epoll_thread *t, int fd, void *data)
{
  /* sender wants to connect, accept connection */
  struct wpipc_receiver *self = data;
  socklen_t addr_size = sizeof(self->addr);
  int sender_fd = accept4 (fd, (struct sockaddr*)&self->addr, &addr_size,
      SOCK_CLOEXEC | SOCK_NONBLOCK);
  struct epoll_event event;
  event.events = EPOLLIN;
  event.data.fd = sender_fd;
  epoll_ctl (t->epoll_fd, EPOLL_CTL_ADD, sender_fd, &event);
  if (self->events && self->events->sender_state)
    self->events->sender_state (self, sender_fd,
        WPIPC_RECEIVER_SENDER_STATE_CONNECTED, self->events_data);
}

static void
other_event_received (struct epoll_thread *t, int fd, void *data)
{
  struct wpipc_receiver *self = data;

  /* sender sends a message, read it and reply */
  ssize_t size = wpipc_socket_read (fd, &self->buffer_read, &self->buffer_size);
  if (size <= 0) {
    if (size < 0)
      wpipc_log_error ("receiver: could not read message: %s", strerror(errno));
    /* client disconnected */
    epoll_ctl (t->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    close (fd);
    if (self->events && self->events->sender_state)
      self->events->sender_state (self, fd,
          WPIPC_RECEIVER_SENDER_STATE_DISCONNECTED, self->events_data);
    return;
  }

  /* reply */
  if (!reply_message (self, fd, self->buffer_read, size))
    wpipc_log_error ("receiver: could not reply message: %s", strerror(errno));

  return;
}

/* API */

struct wpipc_receiver *
wpipc_receiver_new (const char *path,
                    size_t buffer_size,
                    const struct wpipc_receiver_events *events,
                    void *events_data,
                    size_t user_size)
{
  struct wpipc_receiver *self;
  int res;

  /* check params */
  if (path == NULL || buffer_size == 0)
    return NULL;

  self = calloc (1, sizeof (struct wpipc_receiver) + user_size);
  if (self == NULL)
    return NULL;

  self->socket_fd = -1;

  /* set address */
  self->addr.sun_family = AF_LOCAL;
  res = wpipc_construct_socket_path (path, self->addr.sun_path, sizeof(self->addr.sun_path));
  if (res < 0)
    goto error;

  unlink (self->addr.sun_path);

  /* create socket */
  self->socket_fd =
      socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (self->socket_fd < 0)
    goto error;

  /* bind socket */
  if (bind (self->socket_fd, (struct sockaddr *)&self->addr,
      sizeof(self->addr)) != 0)
    goto error;

  /* listen socket */
  if (listen (self->socket_fd, MAX_SENDERS) != 0)
    goto error;

  /* alloc buffer read */
  self->buffer_size = buffer_size;
  self->buffer_read = calloc (buffer_size, sizeof (uint8_t));
  if (self->buffer_read == NULL)
    goto error;

  /* init epoll thread */
  if (!wpipc_epoll_thread_init (&self->epoll_thread, self->socket_fd,
      socket_event_received, other_event_received, self))
    goto error;

  self->events = events;
  self->events_data = events_data;
  if (user_size > 0)
    self->user_data = (void *)((uint8_t *)self + sizeof (struct wpipc_receiver));

  return self;

error:
  if (self->buffer_read)
    free (self->buffer_read);
  if (self->socket_fd != -1)
    close (self->socket_fd);
  free (self);
  return NULL;
}

void
wpipc_receiver_free (struct wpipc_receiver *self)
{
  wpipc_receiver_stop (self);

  wpipc_epoll_thread_destroy (&self->epoll_thread);
  free (self->buffer_read);
  close (self->socket_fd);
  unlink (self->addr.sun_path);
  free (self);
}

bool
wpipc_receiver_start (struct wpipc_receiver *self)
{
  if (wpipc_receiver_is_running (self))
    return true;

  self->thread_running = wpipc_epoll_thread_start (&self->epoll_thread);
  return self->thread_running;
}

void
wpipc_receiver_stop (struct wpipc_receiver *self)
{
  if (wpipc_receiver_is_running (self)) {
    wpipc_epoll_thread_stop (&self->epoll_thread);
    self->thread_running = false;
  }
}

bool
wpipc_receiver_is_running (struct wpipc_receiver *self)
{
  return self->thread_running;
}

void *
wpipc_receiver_get_user_data (struct wpipc_receiver *self)
{
  return self->user_data;
}
