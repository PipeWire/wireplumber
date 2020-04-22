/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>
#include <pipewire/impl.h>
#include <glib.h>
#include <unistd.h>

typedef struct {
  gchar *name;
  struct pw_thread_loop *thread_loop;
  struct pw_context *context;
} WpTestServer;

static inline void
wp_test_server_setup (WpTestServer *self)
{
  struct pw_properties *properties;

  self->name = g_strdup_printf ("wp-test-server-%d-%d", getpid(),
      g_random_int ());
  properties = pw_properties_new(
      PW_KEY_CORE_DAEMON, "1",
      PW_KEY_CORE_NAME, self->name,
      NULL);

  self->thread_loop = pw_thread_loop_new ("wp-test-server", NULL);
  self->context = pw_context_new (pw_thread_loop_get_loop (self->thread_loop),
      properties, 0);

  pw_context_load_module (self->context, "libpipewire-module-access", NULL, NULL);

  pw_thread_loop_start (self->thread_loop);
}

static inline void
wp_test_server_teardown (WpTestServer *self)
{
  pw_thread_loop_stop (self->thread_loop);
  pw_context_destroy (self->context);
  pw_thread_loop_destroy (self->thread_loop);
  g_free (self->name);
}
