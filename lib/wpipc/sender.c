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
#include "sender.h"

#define MAX_ASYNC_TASKS 128

struct wpipc_sender_task {
  wpipc_sender_reply_func_t func;
  void *data;
};

struct wpipc_sender {
  struct sockaddr_un addr;
  int socket_fd;

  uint8_t *buffer_read;
  size_t buffer_size;

  struct epoll_thread epoll_thread;
  bool is_connected;

  wpipc_sender_lost_conn_func_t lost_func;
  void *lost_data;
  bool lost_connection;

  struct wpipc_sender_task async_tasks[MAX_ASYNC_TASKS];

  /* for subclasses */
  void *user_data;
};

static int
push_sync_task (struct wpipc_sender *self,
                wpipc_sender_reply_func_t func,
                void *data)
{
  size_t i;
  for (i = MAX_ASYNC_TASKS; i > 1; i--) {
    struct wpipc_sender_task *curr = self->async_tasks + i - 1;
    struct wpipc_sender_task *next = self->async_tasks + i - 2;
    if (next->func != NULL && curr->func == NULL) {
      curr->func = func;
      curr->data = data;
      return i - 1;
    } else if (i - 2 == 0 && next->func == NULL) {
      /* empty queue */
      next->func = func;
      next->data = data;
      return 0;
    }
  }
  return -1;
}

static void
pop_sync_task (struct wpipc_sender *self,
               bool trigger,
               bool all,
               const uint8_t *buffer,
               size_t size)
{
  size_t i;
  for (i = 0; i < MAX_ASYNC_TASKS; i++) {
    struct wpipc_sender_task *task = self->async_tasks + i;
    if (task->func != NULL) {
      if (trigger)
        task->func (self, buffer, size, task->data);
      task->func = NULL;
      if (!all)
        return;
    }
  }
}

static void
socket_event_received (struct epoll_thread *t, int fd, void *data)
{
  struct wpipc_sender *self = data;

  /* receiver sends a reply, read it trigger corresponding task */
  ssize_t size = wpipc_socket_read (fd, &self->buffer_read, &self->buffer_size);
  if (size <= 0) {
    if (size < 0)
      wpipc_log_error ("sender: could not read reply: %s", strerror(errno));
    /* receiver disconnected */
    epoll_ctl (t->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    shutdown(self->socket_fd, SHUT_RDWR);
    self->is_connected = false;
    self->lost_connection = true;
    if (self->lost_func)
      self->lost_func (self, fd, self->lost_data);
    /* clear queue */
    pop_sync_task (self, true, true, NULL, 0);
    return;
  }

  /* trigger async task */
  pop_sync_task (self, true, false, self->buffer_read, size);
  return;
}

/* API */

struct wpipc_sender *
wpipc_sender_new (const char *path,
                  size_t buffer_size,
                  wpipc_sender_lost_conn_func_t lost_func,
                  void *lost_data,
                  size_t user_size)
{
  struct wpipc_sender *self;
  int res;

  if (path == NULL)
    return NULL;

  self = calloc (1, sizeof (struct wpipc_sender) + user_size);
  if (self == NULL)
    return NULL;

  self->socket_fd = -1;

  /* set address */
  self->addr.sun_family = AF_LOCAL;
  res = wpipc_construct_socket_path (path, self->addr.sun_path, sizeof(self->addr.sun_path));
  if (res < 0)
    goto error;

  /* create socket */
  self->socket_fd =
      socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC| SOCK_NONBLOCK, 0);
  if (self->socket_fd < 0)
    goto error;

  /* alloc buffer read */
  self->buffer_size = buffer_size;
  self->buffer_read = calloc (buffer_size, sizeof (uint8_t));
  if (self->buffer_read == NULL)
    goto error;

  /* init epoll thread */
  if (!wpipc_epoll_thread_init (&self->epoll_thread, self->socket_fd,
      socket_event_received, NULL, self))
    goto error;

  self->lost_func = lost_func;
  self->lost_data = lost_data;
  self->lost_connection = false;
  if (user_size > 0)
    self->user_data = (void *)((uint8_t *)self + sizeof (struct wpipc_sender));

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
wpipc_sender_free (struct wpipc_sender *self)
{
  wpipc_sender_disconnect (self);

  wpipc_epoll_thread_destroy (&self->epoll_thread);
  free (self->buffer_read);
  close (self->socket_fd);
  free (self);
}

bool
wpipc_sender_connect (struct wpipc_sender *self)
{
  if (wpipc_sender_is_connected (self))
    return true;

  /* if connection was lost, re-init epoll thread with new socket */
  if (self->lost_connection) {
    wpipc_epoll_thread_stop (&self->epoll_thread);
    wpipc_epoll_thread_destroy (&self->epoll_thread);
    self->socket_fd =
        socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC| SOCK_NONBLOCK, 0);
    if (self->socket_fd < 0)
      return false;
    if (!wpipc_epoll_thread_init (&self->epoll_thread, self->socket_fd,
        socket_event_received, NULL, self)) {
      close (self->socket_fd);
      return false;
    }
    self->lost_connection = false;
  }

  /* connect */
  if (connect(self->socket_fd, (struct sockaddr *)&self->addr,
        sizeof(self->addr)) == 0 &&
      wpipc_epoll_thread_start (&self->epoll_thread)) {
    self->is_connected = true;
    return true;
  }

  return false;
}

void
wpipc_sender_disconnect (struct wpipc_sender *self)
{
  if (wpipc_sender_is_connected (self)) {
    wpipc_epoll_thread_stop (&self->epoll_thread);
    shutdown(self->socket_fd, SHUT_RDWR);
    self->is_connected = false;
  }
}

bool
wpipc_sender_is_connected (struct wpipc_sender *self)
{
  return self->is_connected;
}

bool
wpipc_sender_send (struct wpipc_sender *self,
                   const uint8_t *buffer,
                   size_t size,
                   wpipc_sender_reply_func_t func,
                   void *data)
{
  int id = -1;

  if (buffer == NULL || size == 0)
    return false;

  if (!wpipc_sender_is_connected (self))
    return false;

  /* add the task in the queue */
  if (func) {
    id = push_sync_task (self, func, data);
    if (id == -1)
      return false;
  }

  /* write buffer and remove task if it fails */
  if (wpipc_socket_write (self->socket_fd, buffer, size) <= 0) {
    if (id != -1)
      self->async_tasks[id].func = NULL;
    return false;
  }

  return true;
}

void *
wpipc_sender_get_user_data (struct wpipc_sender *self)
{
  return self->user_data;
}
