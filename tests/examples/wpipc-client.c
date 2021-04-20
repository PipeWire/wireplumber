/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pthread.h>
#include <assert.h>

#include <spa/pod/builder.h>
#include <wpipc/wpipc.h>

struct client_data {
  struct wpipc_client *c;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool reply_received;
};

static void
reply_handler (struct wpipc_sender *self, const uint8_t *buffer, size_t size, void *p)
{
  struct client_data *data = p;
  const char *error = NULL;
  const struct spa_pod *pod = wpipc_client_send_request_finish (self, buffer, size, &error);
  if (!pod)
    printf ("error: %s\n", error ? error : "unknown");
  else
    printf ("success!\n");

  /* signal reply received */
  pthread_mutex_lock (&data->mutex);
  data->reply_received = true;
  pthread_cond_signal (&data->cond);
  pthread_mutex_unlock (&data->mutex);
}

int
main (int argc, char *argv[])
{
  struct client_data data;

  if (argc < 2) {
    printf ("usage: <server-path>\n");
    return -1;
  }

  /* init */
  data.c = wpipc_client_new (argv[1], true);
  pthread_mutex_init (&data.mutex, NULL);
  pthread_cond_init (&data.cond, NULL);
  data.reply_received = false;

  while (true) {
    char str[1024];

    printf ("> ");
    fgets (str, 1023, stdin);

    if (strncmp (str, "help", 4) == 0) {
      printf ("help\tprints this message\n");
      printf ("quit\texits the client\n");
      printf ("send\tsends a request, usage: send <request-name> [args]\n");
    } else if (strncmp (str, "quit", 4) == 0) {
      printf ("exiting...\n");
      break;
    } else if (strncmp (str, "send", 4) == 0) {
      char request_name[128];
      char request_args[1024];
      int n = sscanf(str, "send %s %s", request_name, request_args);
      if (n <= 0)
        continue;

      /* send request */
      if (n >= 2) {
        /* TODO: for now we always create a string pod for args */
        struct {
          struct spa_pod_string pod;
          char str[1024];
        } args;
        args.pod = SPA_POD_INIT_String(1024);
        strncpy (args.str, request_args, 1024);
        wpipc_client_send_request (data.c, request_name, (const struct spa_pod *)&args,
	    reply_handler, &data);
      } else {
        wpipc_client_send_request (data.c, request_name, NULL, reply_handler, &data);
      }

      /* wait for reply */
      pthread_mutex_lock (&data.mutex);
      while (!data.reply_received)
        pthread_cond_wait (&data.cond, &data.mutex);
      pthread_mutex_unlock (&data.mutex);
    }
  }

  /* clean up */
  pthread_cond_destroy (&data.cond);
  pthread_mutex_destroy (&data.mutex);
  wpipc_client_free (data.c);
  return 0;
}
