/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>

#include <spa/pod/builder.h>
#include <spa/pod/parser.h>

#include "protocol.h"

#define SIZE_PADDING 128

enum wpipc_protocol_reply_code {
  REPLY_CODE_ERROR = 0,
  REPLY_CODE_OK,
};

static bool
is_reply (const uint8_t *buffer, size_t size, int code)
{
  const struct spa_pod *pod = (const struct spa_pod *)buffer;
  struct spa_pod_parser p;
  struct spa_pod_frame f;
  int parsed_code = 0;

  /* check if struct */
  if (!spa_pod_is_struct (pod))
    return false;

  /* parse */
  spa_pod_parser_pod (&p, pod);
  spa_pod_parser_push_struct(&p, &f);
  spa_pod_parser_get_int (&p, &parsed_code);

  return parsed_code == code;
}

/* API */

size_t
wpipc_protocol_calculate_request_size (const char *name,
                                       const struct spa_pod *args)
{
  assert (name);
  return strlen(name) + (args ? SPA_POD_SIZE(args) : 8) + SIZE_PADDING;
}

void
wpipc_protocol_build_request (uint8_t *buffer,
                              size_t size,
                              const char *name,
                              const struct spa_pod *args)
{
  const struct spa_pod none = SPA_POD_INIT_None();
  struct spa_pod_builder b;
  struct spa_pod_frame f;

  memset (buffer, 0, size);

  if (args == NULL)
    args = &none;

  spa_pod_builder_init (&b, buffer, size);
  spa_pod_builder_push_struct (&b, &f);
  spa_pod_builder_string (&b, name);
  spa_pod_builder_primitive (&b, args);
  spa_pod_builder_pop(&b, &f);
}

bool
wpipc_protocol_parse_request (const uint8_t *buffer,
                              size_t size,
                              const char **name,
                              const struct spa_pod **args)
{
  const struct spa_pod *pod = (const struct spa_pod *)buffer;
  struct spa_pod_parser p;
  struct spa_pod_frame f;
  const char *parsed_name = NULL;
  struct spa_pod *parsed_args = NULL;

  /* check if struct */
  if (!spa_pod_is_struct (pod))
    return false;

  /* parse */
  spa_pod_parser_pod (&p, pod);
  spa_pod_parser_push_struct(&p, &f);
  spa_pod_parser_get_string (&p, &parsed_name);
  spa_pod_parser_get_pod (&p, &parsed_args);
  spa_pod_parser_pop(&p, &f);

  /* check name and args */
  if (name == NULL || args == NULL)
    return false;

  if (name != NULL)
    *name = parsed_name;
  if (args != NULL)
    *args = parsed_args;
  return true;
}

size_t
wpipc_protocol_calculate_reply_ok_size (const struct spa_pod *value)
{
  return (value ? SPA_POD_SIZE(value) : 8) + SIZE_PADDING;
}

size_t
wpipc_protocol_calculate_reply_error_size (const char *msg)
{
  assert (msg);
  return strlen(msg) + SIZE_PADDING;
}

void
wpipc_protocol_build_reply_ok (uint8_t *buffer,
                               size_t size,
                               const struct spa_pod *value)
{
  const struct spa_pod none = SPA_POD_INIT_None();
  struct spa_pod_builder b;
  struct spa_pod_frame f;

  memset (buffer, 0, size);

  if (value == NULL)
    value = &none;

  spa_pod_builder_init (&b, buffer, size);
  spa_pod_builder_push_struct (&b, &f);
  spa_pod_builder_int (&b, REPLY_CODE_OK);
  spa_pod_builder_primitive (&b, value);
  spa_pod_builder_pop(&b, &f);
}

void
wpipc_protocol_build_reply_error (uint8_t *buffer,
                                  size_t size,
                                  const char *msg)
{
  struct spa_pod_builder b;
  struct spa_pod_frame f;

  memset (buffer, 0, size);

  spa_pod_builder_init (&b, buffer, size);
  spa_pod_builder_push_struct (&b, &f);
  spa_pod_builder_int (&b, REPLY_CODE_ERROR);
  spa_pod_builder_string (&b, msg);
  spa_pod_builder_pop(&b, &f);
}

bool
wpipc_protocol_is_reply_ok (const uint8_t *buffer, size_t size)
{
  return is_reply (buffer, size, REPLY_CODE_OK);
}

bool
wpipc_protocol_is_reply_error (const uint8_t *buffer, size_t size)
{
  return is_reply (buffer, size, REPLY_CODE_ERROR);
}

bool
wpipc_protocol_parse_reply_ok (const uint8_t *buffer,
                               size_t size,
                               const struct spa_pod **value)
{
  const struct spa_pod *pod = (const struct spa_pod *)buffer;
  struct spa_pod_parser p;
  struct spa_pod_frame f;
  int parsed_code = 0;
  struct spa_pod *parsed_value = NULL;

  /* check if struct */
  if (!spa_pod_is_struct (pod))
    return false;

  /* parse */
  spa_pod_parser_pod (&p, pod);
  spa_pod_parser_push_struct(&p, &f);
  spa_pod_parser_get_int (&p, &parsed_code);
  spa_pod_parser_get_pod (&p, &parsed_value);
  spa_pod_parser_pop (&p, &f);

  if (value != NULL)
    *value = parsed_value;
  return true;
}

bool
wpipc_protocol_parse_reply_error (const uint8_t *buffer,
                                  size_t size,
                                  const char **msg)
{
  const struct spa_pod *pod = (const struct spa_pod *)buffer;
  struct spa_pod_parser p;
  struct spa_pod_frame f;
  int parsed_code = 0;
  const char *parsed_msg = NULL;

  /* check if struct */
  if (!spa_pod_is_struct (pod))
    return false;

  /* parse */
  spa_pod_parser_pod (&p, pod);
  spa_pod_parser_push_struct(&p, &f);
  spa_pod_parser_get_int (&p, &parsed_code);
  spa_pod_parser_get_string (&p, &parsed_msg);
  spa_pod_parser_pop (&p, &f);

  if (msg != NULL)
    *msg = parsed_msg;
  return true;
}
