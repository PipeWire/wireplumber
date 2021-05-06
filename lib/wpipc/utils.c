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
#include <sys/eventfd.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#include <pwd.h>

#include "private.h"

#define MAX_POLL_EVENTS 128
#define MAX_LOG_MESSAGE 1024

/* log */

const char *wpipc_logger_level_text[] = {
  [WPIPC_LOG_LEVEL_ERROR] = "E",
  [WPIPC_LOG_LEVEL_WARN] = "W",
  [WPIPC_LOG_LEVEL_INFO] = "I",
};

struct wpipc_logger {
  enum wpipc_log_level level;
};

static const struct wpipc_logger *
wpipc_log_get_instance (void)
{
  static struct wpipc_logger logger_ = { 0, };
  static struct wpipc_logger* instance_ = NULL;

  if (instance_ == NULL) {
    char * val_str = NULL;
    enum wpipc_log_level val = 0;

    /* default to error */
    logger_.level = WPIPC_LOG_LEVEL_WARN;

    /* get level from env */
    val_str = getenv ("WPIPC_DEBUG");
    if (val_str && sscanf (val_str, "%u", &val) == 1 &&
        val >= WPIPC_LOG_LEVEL_NONE)
      logger_.level = val;

    instance_ = &logger_;
  }

  return instance_;
}

void
wpipc_logv (enum wpipc_log_level level, const char *fmt, va_list args)
{
  const struct wpipc_logger *logger = NULL;

  logger = wpipc_log_get_instance ();
  assert (logger);

  if (logger->level >= level) {
    assert (level > 0);
    char msg[MAX_LOG_MESSAGE];
    struct timespec time;
    clock_gettime (CLOCK_REALTIME, &time);
    vsnprintf (msg, MAX_LOG_MESSAGE, fmt, args);
    fprintf (stderr, "[%s][%lu.%lu] %s\n", wpipc_logger_level_text[level],
        time.tv_sec, time.tv_sec, msg);
  }
}

void
wpipc_log (enum wpipc_log_level level, const char *fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  wpipc_logv (level, fmt, args);
  va_end (args);
}

/* socket path */

int
wpipc_construct_socket_path (const char *name, char *buf, size_t buf_size)
{
  bool path_is_absolute;
  const char *runtime_dir = NULL;
  struct passwd pwd, *result = NULL;
  char buffer[4096];
  int name_size;

  path_is_absolute = name[0] == '/';

  if (!path_is_absolute) {
    runtime_dir = getenv("PIPEWIRE_RUNTIME_DIR");
    if (runtime_dir == NULL)
      runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir == NULL)
      runtime_dir = getenv("HOME");
    if (runtime_dir == NULL)
      runtime_dir = getenv("USERPROFILE");
    if (runtime_dir == NULL) {
      if (getpwuid_r(getuid(), &pwd, buffer, sizeof(buffer), &result) == 0)
        runtime_dir = result ? result->pw_dir : NULL;
    }
  }

  if (runtime_dir == NULL && !path_is_absolute)
    return -ENOENT;

  if (path_is_absolute)
    name_size = snprintf (buf, buf_size, "%s", name) + 1;
  else
    name_size = snprintf (buf, buf_size, "%s/%s", runtime_dir, name) + 1;

  if (name_size > (int) buf_size)
    return -ENAMETOOLONG;

  return 0;
}

/* socket */

ssize_t
wpipc_socket_write (int fd, const uint8_t *buffer, size_t size)
{
  size_t total_written = 0;
  size_t n;

  assert (fd >= 0);
  assert (buffer != NULL);
  assert (size > 0);

  do {
    n = write(fd, buffer, size);
    if (n < size) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return total_written;
      return -1;
    }
    total_written += n;
  } while (total_written < size);

  return total_written;
}

ssize_t
wpipc_socket_read (int fd, uint8_t **buffer, size_t *max_size)
{
  ssize_t n;
  ssize_t size;
  size_t offset = 0;

  assert (buffer);
  assert (*buffer);
  assert (max_size);
  assert (*max_size > 0);

again:
  size = *max_size - offset;
  n = read (fd, *buffer + offset, size);
  if (n == 0)
    return 0;

  /* check for errors */
  if (n < 0) {
    if (errno == EINTR)
      goto again;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return offset;
    return -1;
  }

  /* realloc if we need more space, and read again */
  if (n >= size) {
    *max_size += *max_size;
    *buffer = reallocarray (*buffer, *max_size, sizeof (uint8_t));
    offset += n;
    goto again;
  }

  return offset + n;
}

/* epoll thread */

bool
wpipc_epoll_thread_init (struct epoll_thread *self,
                         int socket_fd,
                         wpipc_epoll_thread_event_funct_t sock_func,
                         wpipc_epoll_thread_event_funct_t other_func,
                         void *data)
{
  struct epoll_event event;

  self->socket_fd = socket_fd;
  self->event_fd = -1;
  self->epoll_fd = -1;

  /* create event fd */
  self->event_fd = eventfd (0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (self->event_fd == -1)
    goto error;

  /* create epoll fd */
  self->epoll_fd = epoll_create1 (EPOLL_CLOEXEC);
  if (self->epoll_fd == -1)
    goto error;

  /* poll socket fd */
  event.events = EPOLLIN;
  event.data.fd = self->socket_fd;
  if (epoll_ctl (self->epoll_fd, EPOLL_CTL_ADD, self->socket_fd, &event) != 0)
    goto error;

  /* poll event fd */
  event.events = EPOLLIN;
  event.data.fd = self->event_fd;
  if (epoll_ctl (self->epoll_fd, EPOLL_CTL_ADD, self->event_fd, &event) != 0)
    goto error;

  self->socket_event_func = sock_func;
  self->other_event_func = other_func;
  self->event_data = data;
  return true;

error:
  if (self->epoll_fd != -1)
    close (self->epoll_fd);
  if (self->event_fd != -1)
    close (self->event_fd);
  return false;
}

static void *
epoll_thread_run (void *data)
{
  struct epoll_thread *self = data;
  bool exit = false;

  while (!exit) {
    /* wait for events */
    struct epoll_event ep[MAX_POLL_EVENTS];
    int n = epoll_wait (self->epoll_fd, ep, MAX_POLL_EVENTS, -1);
    if (n < 0) {
      wpipc_log_error ("epoll_thread: failed to wait for event: %s",
          strerror(errno));
      continue;
    }

    for (int i = 0; i < n; i++) {
      /* socket fd */
      if (ep[i].data.fd == self->socket_fd) {
        if (self->socket_event_func)
          self->socket_event_func (self, ep[i].data.fd, self->event_data);
      }

      /* event fd */
      else if (ep[i].data.fd == self->event_fd) {
        uint64_t stop = 0;
        ssize_t res = read (ep[i].data.fd, &stop, sizeof(uint64_t));
        if (res == sizeof(uint64_t) && stop == 1)
          exit = true;
      }

      /* other */
      else {
        if (self->other_event_func)
          self->other_event_func (self, ep[i].data.fd, self->event_data);
      }
    }
  }

  return NULL;
}

bool
wpipc_epoll_thread_start (struct epoll_thread *self)
{
  return pthread_create (&self->thread, NULL, epoll_thread_run, self) == 0;
}

void
wpipc_epoll_thread_stop (struct epoll_thread *self)
{
  uint64_t value = 1;
  ssize_t res = write (self->event_fd, &value, sizeof(uint64_t));
  if (res == sizeof(uint64_t))
    pthread_join (self->thread, NULL);
}

void
wpipc_epoll_thread_destroy (struct epoll_thread *self)
{
  close (self->epoll_fd);
  close (self->event_fd);
}
