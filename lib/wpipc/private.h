/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WPIPC_PRIVATE_H__
#define __WPIPC_PRIVATE_H__

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/types.h>
#include <stdarg.h>

#include "defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* log */

#define wpipc_log_info(F, ...) \
    wpipc_log(WPIPC_LOG_LEVEL_INFO, (F), ##__VA_ARGS__)
#define wpipc_log_warn(F, ...) \
    wpipc_log(WPIPC_LOG_LEVEL_WARN, (F), ##__VA_ARGS__)
#define wpipc_log_error(F, ...) \
    wpipc_log(WPIPC_LOG_LEVEL_ERROR, (F), ##__VA_ARGS__)

enum wpipc_log_level {
  WPIPC_LOG_LEVEL_NONE = 0,
  WPIPC_LOG_LEVEL_ERROR,
  WPIPC_LOG_LEVEL_WARN,
  WPIPC_LOG_LEVEL_INFO,
};

void
wpipc_logv (enum wpipc_log_level level,
            const char *fmt,
            va_list args) __attribute__ ((format (printf, 2, 0)));

void
wpipc_log (enum wpipc_log_level level,
           const char *fmt,
           ...) __attribute__ ((format (printf, 2, 3)));

/* socket path */

int
wpipc_construct_socket_path (const char *name, char *buf, size_t buf_size);

/* socket */

ssize_t
wpipc_socket_write (int fd, const uint8_t *buffer, size_t size);

ssize_t
wpipc_socket_read (int fd, uint8_t **buffer, size_t *max_size);

/* epoll thread */

struct epoll_thread;

typedef void (*wpipc_epoll_thread_event_funct_t) (struct epoll_thread *self,
                                                  int fd,
                                                  void *data);

struct epoll_thread {
  int socket_fd;
  int epoll_fd;
  int event_fd;
  pthread_t thread;
  wpipc_epoll_thread_event_funct_t socket_event_func;
  wpipc_epoll_thread_event_funct_t other_event_func;
  void *event_data;
};

bool
wpipc_epoll_thread_init (struct epoll_thread *self,
                         int socket_fd,
                         wpipc_epoll_thread_event_funct_t sock_func,
                         wpipc_epoll_thread_event_funct_t other_func,
                         void *data);

bool
wpipc_epoll_thread_start (struct epoll_thread *self);

void
wpipc_epoll_thread_stop (struct epoll_thread *self);

void
wpipc_epoll_thread_destroy (struct epoll_thread *self);

#ifdef __cplusplus
}
#endif

#endif
