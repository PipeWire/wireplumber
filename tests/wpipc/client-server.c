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
#include <unistd.h>

static bool
increment_request_handler (struct wpipc_server *self, int client_fd,
    const char *name, const struct spa_pod *args, void *data)
{
  int32_t val = 0;
  g_assert_true (spa_pod_is_int (args));
  g_assert_true (spa_pod_get_int (args, &val) == 0);
  struct spa_pod_int res = SPA_POD_INIT_Int (val + 1);
  return wpipc_server_reply_ok (self, client_fd, (struct spa_pod *)&res);
}

static bool
error_request_handler (struct wpipc_server *self, int client_fd,
    const char *name, const struct spa_pod *args, void *data)
{
  return wpipc_server_reply_error (self, client_fd, "error message");
}

struct reply_data {
  int32_t incremented;
  const char *error;
  int n_replies;
  GMutex mutex;
  GCond cond;
};

static void
wait_for_reply (struct reply_data *data, int n_replies)
{
  g_mutex_lock (&data->mutex);
  while (data->n_replies < n_replies)
    g_cond_wait (&data->cond, &data->mutex);
  g_mutex_unlock (&data->mutex);
}

static void
reply_handler (struct wpipc_sender *self, const uint8_t *buffer, size_t size, void *p)
{
  struct reply_data *data = p;
  g_assert_nonnull (data);

  g_mutex_lock (&data->mutex);

  const struct spa_pod *pod = wpipc_client_send_request_finish (self, buffer, size, &data->error);
  if (pod) {
    g_assert_true (spa_pod_is_int (pod));
    g_assert_true (spa_pod_get_int (pod, &data->incremented) == 0);
  }
  data->n_replies++;
  g_cond_signal (&data->cond);

  g_mutex_unlock (&data->mutex);
}

static void
test_wpipc_server_client ()
{
  g_autofree gchar *address = g_strdup_printf ("%s/wpipc-test-%d-%d",
          g_get_tmp_dir(), getpid(), g_random_int ());
  struct wpipc_server *s = wpipc_server_new (address, true);
  g_assert_nonnull (s);
  struct wpipc_client *c = wpipc_client_new (address, true);
  g_assert_nonnull (c);
  struct reply_data data;
  g_mutex_init (&data.mutex);
  g_cond_init (&data.cond);

  /* add request handlers */
  g_assert_true (wpipc_server_set_request_handler (s, "INCREMENT", increment_request_handler, NULL));
  g_assert_true (wpipc_server_set_request_handler (s, "ERROR", error_request_handler, NULL));

  /* send an INCREMENT request of 3, and make sure the returned value is 4 */
  data.incremented = -1;
  data.error = NULL;
  data.n_replies = 0;
  struct spa_pod_int i = SPA_POD_INIT_Int (3);
  g_assert_true (wpipc_client_send_request (c, "INCREMENT", (struct spa_pod *)&i, reply_handler, &data));
  wait_for_reply (&data, 1);
  g_assert_null (data.error);
  g_assert_cmpint (data.incremented, ==, 4);

  /* send an ERROR request, and make sure the returned value is an error */
  data.error = NULL;
  data.n_replies = 0;
  g_assert_true (wpipc_client_send_request (c, "ERROR", NULL, reply_handler, &data));
  wait_for_reply (&data, 1);
  g_assert_cmpstr (data.error, ==, "error message");

  /* send an unhandled request, and make sure the server replies with an error */
  data.error = NULL;
  data.n_replies = 0;
  g_assert_true (wpipc_client_send_request (c, "UNHANDLED-REQUEST", NULL, reply_handler, &data));
  wait_for_reply (&data, 1);
  g_assert_cmpstr (data.error, ==, "request handler not found");

  /* clean up */
  g_cond_clear (&data.cond);
  g_mutex_clear (&data.mutex);
  wpipc_client_free (c);
  wpipc_server_free (s);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/wpipc/wpipc-server-client", test_wpipc_server_client);

  return g_test_run ();
}
