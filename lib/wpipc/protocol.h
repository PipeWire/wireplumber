/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WPIPC_PROTOCOL_H__
#define __WPIPC_PROTOCOL_H__

#include <spa/pod/pod.h>

#include "defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* request */

WPIPC_API
size_t
wpipc_protocol_calculate_request_size (const char *name,
                                       const struct spa_pod *args);

WPIPC_API
void
wpipc_protocol_build_request (uint8_t *buffer,
                              size_t size,
                              const char *name,
                              const struct spa_pod *args);

WPIPC_API
bool
wpipc_protocol_parse_request (const uint8_t *buffer,
                              size_t size,
                              const char **name,
                              const struct spa_pod **args);

/* reply */

WPIPC_API
size_t
wpipc_protocol_calculate_reply_ok_size (const struct spa_pod *value);

WPIPC_API
size_t
wpipc_protocol_calculate_reply_error_size (const char *msg);

WPIPC_API
void
wpipc_protocol_build_reply_ok (uint8_t *buffer,
                               size_t size,
                               const struct spa_pod *value);

WPIPC_API
void
wpipc_protocol_build_reply_error (uint8_t *buffer,
                                  size_t size,
                                  const char *msg);

WPIPC_API
bool
wpipc_protocol_is_reply_ok (const uint8_t *buffer, size_t size);

WPIPC_API
bool
wpipc_protocol_is_reply_error (const uint8_t *buffer, size_t size);

WPIPC_API
bool
wpipc_protocol_parse_reply_ok (const uint8_t *buffer,
                               size_t size,
                               const struct spa_pod **value);

WPIPC_API
bool
wpipc_protocol_parse_reply_error (const uint8_t *buffer,
                                  size_t size,
                                  const char **msg);

#ifdef __cplusplus
}
#endif

#endif
