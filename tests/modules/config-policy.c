/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>

#include <wp/wp.h>

#include "config-policy/context.h"
#include "../wp/test-server.h"

typedef struct {
  WpTestServer server;

  GMutex mutex;
  GCond cond;
  gboolean created;

  GThread *loop_thread;
  GMainContext *context;
  GMainLoop *loop;

  WpCore *core;
} TestConfigPolicyFixture;

static void *
loop_thread_start (void *d)
{
  TestConfigPolicyFixture *self = d;

  /* Create the main loop using the default thread context */
  self->context = g_main_context_get_thread_default ();
  self->loop = g_main_loop_new (self->context, FALSE);

  /* Notify the main thread that the main loop has been created */
  g_mutex_lock (&self->mutex);
  self->created = TRUE;
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->mutex);

  /* Run the main loop */
  g_main_loop_run (self->loop);

  /* Clean up */
  g_clear_pointer (&self->loop, g_main_loop_unref);
  return NULL;
}

static void
config_policy_setup (TestConfigPolicyFixture *self, gconstpointer user_data)
{
  /* Create the server */
  g_autoptr (WpProperties) props = NULL;
  wp_test_server_setup (&self->server);
  props = wp_properties_new (PW_KEY_REMOTE_NAME, self->server.name, NULL);

  /* Data */
  g_mutex_init (&self->mutex);
  g_cond_init (&self->cond);
  self->created = FALSE;

  /* Start the main loop in a thread */
  self->loop_thread = g_thread_new("loop-thread", &loop_thread_start, self);

  /* Wait for the thread to create the main loop */
  g_mutex_lock (&self->mutex);
  while (!self->created)
    g_cond_wait (&self->cond, &self->mutex);
  g_mutex_unlock (&self->mutex);

  /* Create the core using the thread context */
  self->core = wp_core_new (self->context, props);

  /* Connect to the server */
  pw_remote_connect (wp_core_get_pw_remote (self->core));
}

static gboolean
loop_thread_stop (gpointer data)
{
  TestConfigPolicyFixture *self = data;
  g_main_loop_quit (self->loop);
  return G_SOURCE_REMOVE;
}

static void
config_policy_teardown (TestConfigPolicyFixture *self, gconstpointer user_data)
{
  /* Stop the main loop and wait until it is done */
  wp_core_idle_add (self->core, loop_thread_stop, self);
  g_thread_join (self->loop_thread);

  /* Destroy the core */
  g_clear_object (&self->core);

  g_mutex_clear (&self->mutex);
  g_cond_clear (&self->cond);
  self->created = FALSE;

  /* Destroy the server */
  wp_test_server_teardown (&self->server);
}

static void
playback (TestConfigPolicyFixture *f, gconstpointer data)
{
  g_autoptr (WpConfigPolicyContext) ctx = wp_config_policy_context_new (f->core,
      "config-policy/config-playback");
  g_autoptr (WpEndpointLink) link = NULL;
  g_autoptr (WpEndpoint) src = NULL;
  g_autoptr (WpEndpoint) sink = NULL;
  g_autoptr (WpEndpoint) ep1 = NULL;
  g_autoptr (WpEndpoint) ep2 = NULL;
  g_autoptr (WpEndpoint) ep3 = NULL;

  /* Create the device endpoint */
  ep1 = wp_config_policy_context_add_endpoint (ctx, "ep1", "Fake/Sink",
      PW_DIRECTION_INPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep1);
  g_assert_null (link);
  g_assert_false (wp_endpoint_is_linked (ep1));

  /* Create the first client endpoint */
  ep2 = wp_config_policy_context_add_endpoint (ctx, "ep2", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_endpoint_is_linked (ep2));
  g_assert_true (wp_endpoint_is_linked (ep1));
  src = wp_endpoint_link_get_source_endpoint (link);
  sink = wp_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep2 == src);
  g_assert_true (ep1 == sink);

  /* Create the second client endpoint */
  ep3 = wp_config_policy_context_add_endpoint (ctx, "ep3", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_endpoint_is_linked (ep3));
  g_assert_true (wp_endpoint_is_linked (ep1));
  src = wp_endpoint_link_get_source_endpoint (link);
  sink = wp_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep3 == src);
  g_assert_true (ep1 == sink);

  /* Make sure ep2 is unlinked after ep3 was linked */
  g_assert_false (wp_endpoint_is_linked (ep2));

  /* Remove the client endpoints */
  wp_config_policy_context_remove_endpoint (ctx, ep2);
  wp_config_policy_context_remove_endpoint (ctx, ep3);
}

static void
capture (TestConfigPolicyFixture *f, gconstpointer data)
{
  g_autoptr (WpConfigPolicyContext) ctx = wp_config_policy_context_new (f->core,
      "config-policy/config-capture");
  g_autoptr (WpEndpointLink) link = NULL;
  g_autoptr (WpEndpoint) src = NULL;
  g_autoptr (WpEndpoint) sink = NULL;
  g_autoptr (WpEndpoint) ep1 = NULL;
  g_autoptr (WpEndpoint) ep2 = NULL;

  /* Create the device endpoint */
  ep1 = wp_config_policy_context_add_endpoint (ctx, "ep1", "Fake/Source",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep1);
  g_assert_null (link);
  g_assert_false (wp_endpoint_is_linked (ep1));

  /* Create the client endpoint */
  ep2 = wp_config_policy_context_add_endpoint (ctx, "ep2", "Stream/Input/Fake",
      PW_DIRECTION_INPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_endpoint_is_linked (ep2));
  g_assert_true (wp_endpoint_is_linked (ep1));
  src = wp_endpoint_link_get_source_endpoint (link);
  sink = wp_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep1 == src);
  g_assert_true (ep2 == sink);

  /* Remove the client endpoint */
  wp_config_policy_context_remove_endpoint (ctx, ep2);
}

static void
playback_capture (TestConfigPolicyFixture *f, gconstpointer data)
{
  g_autoptr (WpConfigPolicyContext) ctx = wp_config_policy_context_new (f->core,
      "config-policy/config-playback-capture");
  g_autoptr (WpEndpointLink) link = NULL;
  g_autoptr (WpEndpoint) src = NULL;
  g_autoptr (WpEndpoint) sink = NULL;
  g_autoptr (WpEndpoint) ep1 = NULL;
  g_autoptr (WpEndpoint) ep2 = NULL;
  g_autoptr (WpEndpoint) ep3 = NULL;
  g_autoptr (WpEndpoint) ep4 = NULL;

  /* Create the device endpoints */
  ep1 = wp_config_policy_context_add_endpoint (ctx, "ep1", "Fake/Sink",
      PW_DIRECTION_INPUT, NULL, NULL, 0, NULL);
  g_assert_nonnull (ep1);
  g_assert_null (link);
  g_assert_false (wp_endpoint_is_linked (ep1));
  ep2 = wp_config_policy_context_add_endpoint (ctx, "ep2", "Fake/Source",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, NULL);
  g_assert_nonnull (ep2);
  g_assert_null (link);
  g_assert_false (wp_endpoint_is_linked (ep2));

  /* Create the playback client endpoint */
  ep3 = wp_config_policy_context_add_endpoint (ctx, "ep3", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep3);
  g_assert_nonnull (link);
  g_assert_true (wp_endpoint_is_linked (ep3));
  g_assert_true (wp_endpoint_is_linked (ep1));
  g_assert_false (wp_endpoint_is_linked (ep2));
  src = wp_endpoint_link_get_source_endpoint (link);
  sink = wp_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep3 == src);
  g_assert_true (ep1 == sink);

  /* Create the capture client endpoint */
  ep4 = wp_config_policy_context_add_endpoint (ctx, "ep4", "Stream/Input/Fake",
      PW_DIRECTION_INPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep4);
  g_assert_nonnull (link);
  g_assert_true (wp_endpoint_is_linked (ep4));
  g_assert_true (wp_endpoint_is_linked (ep2));
  g_assert_true (wp_endpoint_is_linked (ep3));
  g_assert_true (wp_endpoint_is_linked (ep1));
  src = wp_endpoint_link_get_source_endpoint (link);
  sink = wp_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep2 == src);
  g_assert_true (ep4 == sink);

  /* Remove the client endpoints */
  wp_config_policy_context_remove_endpoint (ctx, ep4);
  wp_config_policy_context_remove_endpoint (ctx, ep3);
}

static void
playback_priority (TestConfigPolicyFixture *f, gconstpointer data)
{
  g_autoptr (WpConfigPolicyContext) ctx = wp_config_policy_context_new (f->core,
      "config-policy/config-playback-priority");
  g_autoptr (WpEndpointLink) link = NULL;
  g_autoptr (WpEndpoint) src = NULL;
  g_autoptr (WpEndpoint) sink = NULL;
  g_autoptr (WpEndpoint) dev = NULL;
  g_autoptr (WpEndpoint) ep1 = NULL;
  g_autoptr (WpEndpoint) ep2 = NULL;
  g_autoptr (WpEndpoint) ep3 = NULL;

  /* Create the device endpoint with 4 streams */
  dev = wp_config_policy_context_add_endpoint (ctx, "dev", "Fake/Sink",
      PW_DIRECTION_INPUT, NULL, NULL, 4, NULL);
  g_assert_nonnull (dev);
  g_assert_null (link);
  g_assert_false (wp_endpoint_is_linked (dev));

  /* Create the client endpoint for steam 2 (priority 50) and make sure it
   * is linked */
  ep2 = wp_config_policy_context_add_endpoint (ctx, "ep_for_stream_2",
      "Stream/Output/Fake", PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_endpoint_is_linked (ep2));
  g_assert_true (wp_endpoint_is_linked (dev));
  src = wp_endpoint_link_get_source_endpoint (link);
  sink = wp_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep2 == src);
  g_assert_true (dev == sink);

  /* Create the client endpoint for steam 1 (priority 25) and make sure it
   * is not linked */
  ep1 = wp_config_policy_context_add_endpoint (ctx, "ep_for_stream_1",
      "Stream/Output/Fake", PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep1);
  g_assert_null (link);
  g_assert_false (wp_endpoint_is_linked (ep1));
  g_assert_true (wp_endpoint_is_linked (ep2));
  g_assert_true (wp_endpoint_is_linked (dev));

  /* Create the client endpoint for steam 3 (priority 75) and make sure it
   * is linked */
  ep3 = wp_config_policy_context_add_endpoint (ctx, "ep_for_stream_3",
      "Stream/Output/Fake", PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep3);
  g_assert_nonnull (link);
  g_assert_true (wp_endpoint_is_linked (ep3));
  g_assert_true (wp_endpoint_is_linked (dev));
  g_assert_false (wp_endpoint_is_linked (ep1));
  g_assert_false (wp_endpoint_is_linked (ep2));
  src = wp_endpoint_link_get_source_endpoint (link);
  sink = wp_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep3 == src);
  g_assert_true (dev == sink);

  /* Remove the client endpoints */
  wp_config_policy_context_remove_endpoint (ctx, ep2);
  wp_config_policy_context_remove_endpoint (ctx, ep3);
  wp_config_policy_context_remove_endpoint (ctx, ep1);
}

static void
playback_keep (TestConfigPolicyFixture *f, gconstpointer data)
{
  g_autoptr (WpConfigPolicyContext) ctx = wp_config_policy_context_new (f->core,
      "config-policy/config-playback-keep");
  g_autoptr (WpEndpointLink) link = NULL;
  g_autoptr (WpEndpoint) src = NULL;
  g_autoptr (WpEndpoint) sink = NULL;
  g_autoptr (WpEndpoint) ep1 = NULL;
  g_autoptr (WpEndpoint) ep2 = NULL;
  g_autoptr (WpEndpoint) ep3 = NULL;

  /* Create the device endpoint */
  ep1 = wp_config_policy_context_add_endpoint (ctx, "ep1", "Fake/Sink",
      PW_DIRECTION_INPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep1);
  g_assert_null (link);
  g_assert_false (wp_endpoint_is_linked (ep1));

  /* Create the first client endpoint */
  ep2 = wp_config_policy_context_add_endpoint (ctx, "ep2", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_endpoint_is_linked (ep2));
  g_assert_true (wp_endpoint_is_linked (ep1));
  src = wp_endpoint_link_get_source_endpoint (link);
  sink = wp_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep2 == src);
  g_assert_true (ep1 == sink);

  /* Create the second client endpoint */
  ep3 = wp_config_policy_context_add_endpoint (ctx, "ep3", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_endpoint_is_linked (ep3));
  g_assert_true (wp_endpoint_is_linked (ep1));
  src = wp_endpoint_link_get_source_endpoint (link);
  sink = wp_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep3 == src);
  g_assert_true (ep1 == sink);

  /* Make sure ep2 is still linked after ep3 was linked */
  g_assert_true (wp_endpoint_is_linked (ep2));

  /* Remove the client endpoints */
  wp_config_policy_context_remove_endpoint (ctx, ep2);
  wp_config_policy_context_remove_endpoint (ctx, ep3);
}

static void
playback_role (TestConfigPolicyFixture *f, gconstpointer data)
{
  g_autoptr (WpConfigPolicyContext) ctx = wp_config_policy_context_new (f->core,
      "config-policy/config-playback-role");
  g_autoptr (WpEndpointLink) link = NULL;
  g_autoptr (WpEndpoint) src = NULL;
  g_autoptr (WpEndpoint) sink = NULL;
  g_autoptr (WpEndpoint) ep1 = NULL;
  g_autoptr (WpEndpoint) ep2 = NULL;
  g_autoptr (WpEndpoint) ep3 = NULL;

  /* Create the device with 2 roles: "0" with id 0, and "1" with id 1 */
  ep1 = wp_config_policy_context_add_endpoint (ctx, "ep1", "Fake/Sink",
      PW_DIRECTION_INPUT, NULL, NULL, 2, &link);
  g_assert_nonnull (ep1);
  g_assert_null (link);
  g_assert_false (wp_endpoint_is_linked (ep1));

  /* Create the first client endpoint with role "0" and make sure it has
   * priority over the one defined in the configuration file which is "1" */
  ep2 = wp_config_policy_context_add_endpoint (ctx, "ep2", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, "0", 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_endpoint_is_linked (ep2));
  g_assert_true (wp_endpoint_is_linked (ep1));
  src = wp_endpoint_link_get_source_endpoint (link);
  sink = wp_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep2 == src);
  g_assert_true (ep1 == sink);
  g_assert_true (
      WP_STREAM_ID_NONE == wp_endpoint_link_get_source_stream (link));
  g_assert_true (0 == wp_endpoint_link_get_sink_stream (link));

  /* Create the second client endpoint without role and make sure it uses
   * the one defined in the configuration file which is "1" */
  ep3 = wp_config_policy_context_add_endpoint (ctx, "ep3", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep3);
  g_assert_nonnull (link);
  g_assert_true (wp_endpoint_is_linked (ep3));
  g_assert_true (wp_endpoint_is_linked (ep1));
  src = wp_endpoint_link_get_source_endpoint (link);
  sink = wp_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep3 == src);
  g_assert_true (ep1 == sink);
  g_assert_true (
      WP_STREAM_ID_NONE == wp_endpoint_link_get_source_stream (link));
  g_assert_true (1 == wp_endpoint_link_get_sink_stream (link));
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  pw_init (NULL, NULL);

  g_test_add ("/modules/config-policy/playback", TestConfigPolicyFixture,
      NULL, config_policy_setup, playback, config_policy_teardown);
  g_test_add ("/modules/config-policy/capture", TestConfigPolicyFixture,
      NULL, config_policy_setup, capture, config_policy_teardown);
  g_test_add ("/modules/config-policy/playback-capture", TestConfigPolicyFixture,
      NULL, config_policy_setup, playback_capture, config_policy_teardown);
  g_test_add ("/modules/config-policy/playback-priority", TestConfigPolicyFixture,
      NULL, config_policy_setup, playback_priority, config_policy_teardown);
  g_test_add ("/modules/config-policy/playback-keep", TestConfigPolicyFixture,
      NULL, config_policy_setup, playback_keep, config_policy_teardown);
  g_test_add ("/modules/config-policy/playback-role", TestConfigPolicyFixture,
      NULL, config_policy_setup, playback_role, config_policy_teardown);

  return g_test_run ();
}
