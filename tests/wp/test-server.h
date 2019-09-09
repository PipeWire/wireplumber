/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>

typedef struct {
  gchar *name;
  struct pw_core *core;
  struct pw_loop *loop;
  struct pw_thread_loop *thread_loop;
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

  self->loop = pw_loop_new (NULL);
  self->thread_loop = pw_thread_loop_new (self->loop, "wp-test-server");
  self->core = pw_core_new (self->loop, properties, 0);

  pw_module_load (self->core, "libpipewire-module-protocol-native", NULL, NULL);
  pw_module_load (self->core, "libpipewire-module-access", NULL, NULL);

  pw_thread_loop_start (self->thread_loop);
}

static inline void
wp_test_server_teardown (WpTestServer *self)
{
  pw_thread_loop_stop (self->thread_loop);
  pw_core_destroy (self->core);
  pw_thread_loop_destroy (self->thread_loop);
  pw_loop_destroy (self->loop);
}
