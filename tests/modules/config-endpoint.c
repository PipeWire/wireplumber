/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"
#include "config-endpoint/endpoint-audiotestsrc.h"
#include "../../modules/module-config-endpoint/context.h"

typedef struct {
  WpBaseTestFixture base;
} TestConfigEndpointFixture;

static void
config_endpoint_setup (TestConfigEndpointFixture *self, gconstpointer data)
{
  wp_base_test_fixture_setup (&self->base, 0);

  /* load audiotestsrc */
  pw_thread_loop_lock (self->base.server.thread_loop);
  pw_context_add_spa_lib (self->base.server.context, "audiotestsrc",
      "audiotestsrc/libspa-audiotestsrc");
  if (!pw_context_load_module (self->base.server.context,
        "libpipewire-module-spa-node", "audiotestsrc", NULL)) {
    pw_thread_loop_unlock (self->base.server.thread_loop);
    g_test_skip ("audiotestsrc SPA plugin is not installed");
    return;
  }
  pw_thread_loop_unlock (self->base.server.thread_loop);

  /* Register the wp-endpoint-audiotestsrc */
  wp_factory_new (self->base.core, "wp-endpoint-audiotestsrc",
      wp_endpoint_audiotestsrc_factory);
}

static void
config_endpoint_teardown (TestConfigEndpointFixture *self, gconstpointer data)
{
  wp_base_test_fixture_teardown (&self->base);
}

static void
on_audiotestsrc_created (WpConfigEndpointContext *ctx, WpEndpoint *ep,
    TestConfigEndpointFixture *f)
{
  g_assert_nonnull (ep);
  g_main_loop_quit (f->base.loop);
}

static void
basic (TestConfigEndpointFixture *f, gconstpointer data)
{
  /* Set the configuration path */
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (f->base.core);
  g_assert_nonnull (config);
  wp_configuration_add_path (config, "config-endpoint/basic");

  /* Create the context and handle the endpoint-created callback */
  g_autoptr (WpConfigEndpointContext) ctx =
      wp_config_endpoint_context_new (f->base.core);
  g_assert_nonnull (ctx);
  g_assert_cmpint (wp_config_endpoint_context_get_length (ctx), ==, 0);

  /* Add a handler to stop the main loop when the endpoint is created */
  g_signal_connect (ctx, "endpoint-created",
      (GCallback) on_audiotestsrc_created, f);

  /* Run the main loop */
  g_main_loop_run (f->base.loop);

  /* Check if the endpoint was created */
  g_assert_cmpint (wp_config_endpoint_context_get_length (ctx), ==, 1);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  pw_init (NULL, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add ("/modules/config-endpoint/basic", TestConfigEndpointFixture,
      NULL, config_endpoint_setup, basic, config_endpoint_teardown);

  return g_test_run ();
}
