/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <glib.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <wpipc/wpipc.h>

static void
test_wpipc_protocol ()
{
  uint8_t b[1024];

  /* request null value */
  {
    wpipc_protocol_build_request (b, sizeof(b), "name", NULL);
    const char *name = NULL;
    const struct spa_pod *value = NULL;
    g_assert_true (wpipc_protocol_parse_request (b, sizeof(b), &name, &value));
    g_assert_cmpstr (name, ==, "name");
    g_assert_true (spa_pod_is_none (value));
  }

  /* request */
  {
    struct spa_pod_int i = SPA_POD_INIT_Int (8);
    wpipc_protocol_build_request (b, sizeof(b), "name", (struct spa_pod *)&i);
    const char *name = NULL;
    const struct spa_pod_int *value = NULL;
    g_assert_true (wpipc_protocol_parse_request (b, sizeof(b), &name, (const struct spa_pod **)&value));
    g_assert_cmpstr (name, ==, "name");
    g_assert_cmpint (value->value, ==, 8);
  }

  /* reply error */
  {
    wpipc_protocol_build_reply_error (b, sizeof(b), "error message");
    g_assert_true (wpipc_protocol_is_reply_error (b, sizeof(b)));
    const char *msg = NULL;
    g_assert_true (wpipc_protocol_parse_reply_error (b, sizeof(b), &msg));
    g_assert_cmpstr (msg, ==, "error message");
  }

  /* reply ok null value */
  {
    wpipc_protocol_build_reply_ok (b, sizeof(b), NULL);
    g_assert_true (wpipc_protocol_is_reply_ok (b, sizeof(b)));
    const struct spa_pod *value = NULL;
    g_assert_true (wpipc_protocol_parse_reply_ok (b, sizeof(b), &value));
    g_assert_true (spa_pod_is_none (value));
  }

  /* reply ok */
  {
    struct spa_pod_int i = SPA_POD_INIT_Int (3);
    wpipc_protocol_build_reply_ok (b, sizeof(b), (struct spa_pod *)&i);
    g_assert_true (wpipc_protocol_is_reply_ok (b, sizeof(b)));
    const struct spa_pod_int *value = NULL;
    g_assert_true (wpipc_protocol_parse_reply_ok (b, sizeof(b), (const struct spa_pod **)&value));
    g_assert_cmpint (value->value, ==, 3);
  }
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/wpipc/wpipc-protocol", test_wpipc_protocol);

  return g_test_run ();
}
