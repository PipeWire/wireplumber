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

  /* Create the server */
  wp_test_server_setup (&self->server);

  /* Create the core and connect to the server */
  g_autoptr (WpProperties) props = NULL;
  props = wp_properties_new (PW_KEY_REMOTE_NAME, self->server.name, NULL);
  self->core = wp_core_new (self->context, props);
  g_assert_true (wp_core_connect (self->core));

  /* Notify the main thread that we are done */
  g_mutex_lock (&self->mutex);
  self->created = TRUE;
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->mutex);

  /* Run the main loop */
  g_main_loop_run (self->loop);

  /* Clean up */
  g_clear_object (&self->core);
  wp_test_server_teardown (&self->server);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  return NULL;
}

static void
config_policy_setup (TestConfigPolicyFixture *self, gconstpointer user_data)
{
  gint64 end_time;

  /* Data */
  g_mutex_init (&self->mutex);
  g_cond_init (&self->cond);
  self->created = FALSE;

  /* Initialize main loop, server and core in a thread */
  self->loop_thread = g_thread_new("loop-thread", &loop_thread_start, self);

  /* Wait for everything to be created */
  g_mutex_lock (&self->mutex);
  end_time = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
  while (!self->created)
    if (!g_cond_wait_until (&self->cond, &self->mutex, end_time)) {
      /* Abort when timeout has passed */
      g_warning ("Aborting due to timeout when waiting for connection");
      abort();
    }
  g_mutex_unlock (&self->mutex);
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
  g_autoptr (GSource) source = g_idle_source_new ();
  g_source_set_callback (source, loop_thread_stop, self, NULL);
  g_source_attach (source, self->context);
  g_thread_join (self->loop_thread);

  /* Data */
  g_mutex_clear (&self->mutex);
  g_cond_clear (&self->cond);
  self->created = FALSE;
}

static void
playback (TestConfigPolicyFixture *f, gconstpointer data)
{
  g_autoptr (WpConfigPolicyContext) ctx = wp_config_policy_context_new (f->core,
      "config-policy/config-playback");
  g_autoptr (WpBaseEndpointLink) link = NULL;
  g_autoptr (WpBaseEndpoint) src = NULL;
  g_autoptr (WpBaseEndpoint) sink = NULL;
  g_autoptr (WpBaseEndpoint) ep1 = NULL;
  g_autoptr (WpBaseEndpoint) ep2 = NULL;
  g_autoptr (WpBaseEndpoint) ep3 = NULL;

  /* Create the device endpoint */
  ep1 = wp_config_policy_context_add_endpoint (ctx, "ep1", "Fake/Sink",
      PW_DIRECTION_INPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep1);
  g_assert_null (link);
  g_assert_false (wp_base_endpoint_is_linked (ep1));

  /* Create the first client endpoint */
  ep2 = wp_config_policy_context_add_endpoint (ctx, "ep2", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep2));
  g_assert_true (wp_base_endpoint_is_linked (ep1));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep2 == src);
  g_assert_true (ep1 == sink);

  /* Create the second client endpoint */
  ep3 = wp_config_policy_context_add_endpoint (ctx, "ep3", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep3));
  g_assert_true (wp_base_endpoint_is_linked (ep1));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep3 == src);
  g_assert_true (ep1 == sink);

  /* Make sure ep2 is unlinked after ep3 was linked */
  g_assert_false (wp_base_endpoint_is_linked (ep2));

  /* Remove the client endpoints */
  wp_config_policy_context_remove_endpoint (ctx, ep2);
  wp_config_policy_context_remove_endpoint (ctx, ep3);
}

static void
capture (TestConfigPolicyFixture *f, gconstpointer data)
{
  g_autoptr (WpConfigPolicyContext) ctx = wp_config_policy_context_new (f->core,
      "config-policy/config-capture");
  g_autoptr (WpBaseEndpointLink) link = NULL;
  g_autoptr (WpBaseEndpoint) src = NULL;
  g_autoptr (WpBaseEndpoint) sink = NULL;
  g_autoptr (WpBaseEndpoint) ep1 = NULL;
  g_autoptr (WpBaseEndpoint) ep2 = NULL;

  /* Create the device endpoint */
  ep1 = wp_config_policy_context_add_endpoint (ctx, "ep1", "Fake/Source",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep1);
  g_assert_null (link);
  g_assert_false (wp_base_endpoint_is_linked (ep1));

  /* Create the client endpoint */
  ep2 = wp_config_policy_context_add_endpoint (ctx, "ep2", "Stream/Input/Fake",
      PW_DIRECTION_INPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep2));
  g_assert_true (wp_base_endpoint_is_linked (ep1));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
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
  g_autoptr (WpBaseEndpointLink) link = NULL;
  g_autoptr (WpBaseEndpoint) src = NULL;
  g_autoptr (WpBaseEndpoint) sink = NULL;
  g_autoptr (WpBaseEndpoint) ep1 = NULL;
  g_autoptr (WpBaseEndpoint) ep2 = NULL;
  g_autoptr (WpBaseEndpoint) ep3 = NULL;
  g_autoptr (WpBaseEndpoint) ep4 = NULL;

  /* Create the device endpoints */
  ep1 = wp_config_policy_context_add_endpoint (ctx, "ep1", "Fake/Sink",
      PW_DIRECTION_INPUT, NULL, NULL, 0, NULL);
  g_assert_nonnull (ep1);
  g_assert_null (link);
  g_assert_false (wp_base_endpoint_is_linked (ep1));
  ep2 = wp_config_policy_context_add_endpoint (ctx, "ep2", "Fake/Source",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, NULL);
  g_assert_nonnull (ep2);
  g_assert_null (link);
  g_assert_false (wp_base_endpoint_is_linked (ep2));

  /* Create the playback client endpoint */
  ep3 = wp_config_policy_context_add_endpoint (ctx, "ep3", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep3);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep3));
  g_assert_true (wp_base_endpoint_is_linked (ep1));
  g_assert_false (wp_base_endpoint_is_linked (ep2));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep3 == src);
  g_assert_true (ep1 == sink);

  /* Create the capture client endpoint */
  ep4 = wp_config_policy_context_add_endpoint (ctx, "ep4", "Stream/Input/Fake",
      PW_DIRECTION_INPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep4);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep4));
  g_assert_true (wp_base_endpoint_is_linked (ep2));
  g_assert_true (wp_base_endpoint_is_linked (ep3));
  g_assert_true (wp_base_endpoint_is_linked (ep1));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
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
  g_autoptr (WpBaseEndpointLink) link = NULL;
  g_autoptr (WpBaseEndpoint) src = NULL;
  g_autoptr (WpBaseEndpoint) sink = NULL;
  g_autoptr (WpBaseEndpoint) dev = NULL;
  g_autoptr (WpBaseEndpoint) ep1 = NULL;
  g_autoptr (WpBaseEndpoint) ep2 = NULL;
  g_autoptr (WpBaseEndpoint) ep3 = NULL;
  g_autoptr (WpBaseEndpoint) ep4 = NULL;
  g_autoptr (WpBaseEndpoint) ep5 = NULL;

  /* Create the device endpoint with 4 streams */
  dev = wp_config_policy_context_add_endpoint (ctx, "dev", "Fake/Sink",
      PW_DIRECTION_INPUT, NULL, NULL, 4, NULL);
  g_assert_nonnull (dev);
  g_assert_null (link);
  g_assert_false (wp_base_endpoint_is_linked (dev));

  /* Create the client endpoint for steam 2 (priority 2) and make sure it
   * is linked */
  ep2 = wp_config_policy_context_add_endpoint (ctx, "ep_for_stream_2",
      "Stream/Output/Fake", PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep2));
  g_assert_true (wp_base_endpoint_is_linked (dev));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep2 == src);
  g_assert_true (dev == sink);

  /* Create the client endpoint for steam 1 (priority 1) and make sure it
   * is not linked */
  ep1 = wp_config_policy_context_add_endpoint (ctx, "ep_for_stream_1",
      "Stream/Output/Fake", PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep1);
  g_assert_null (link);
  g_assert_false (wp_base_endpoint_is_linked (ep1));
  g_assert_true (wp_base_endpoint_is_linked (ep2));
  g_assert_true (wp_base_endpoint_is_linked (dev));

  /* Create the client endpoint for steam 3 (priority 3) and make sure it
   * is linked */
  ep3 = wp_config_policy_context_add_endpoint (ctx, "ep_for_stream_3",
      "Stream/Output/Fake", PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep3);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep3));
  g_assert_true (wp_base_endpoint_is_linked (dev));
  g_assert_false (wp_base_endpoint_is_linked (ep1));
  g_assert_false (wp_base_endpoint_is_linked (ep2));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep3 == src);
  g_assert_true (dev == sink);

  /* Remove ep2 and ep1 */
  wp_config_policy_context_remove_endpoint (ctx, ep2);
  wp_config_policy_context_remove_endpoint (ctx, ep1);

  /* Create the client endpoint with role "1" (priority 1) and make sure it
   * is not linked */
  ep4 = wp_config_policy_context_add_endpoint (ctx, "ep_with_role",
      "Stream/Output/Fake", PW_DIRECTION_OUTPUT, NULL, "1", 0, &link);
  g_assert_nonnull (ep4);
  g_assert_null (link);
  g_assert_false (wp_base_endpoint_is_linked (ep4));

  /* Create the client endpoint with role "3" (priority 3) and make sure it
   * is linked (last one wins) */
  ep5 = wp_config_policy_context_add_endpoint (ctx, "ep_with_role",
      "Stream/Output/Fake", PW_DIRECTION_OUTPUT, NULL, "3", 0, &link);
  g_assert_nonnull (ep5);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep5));
  g_assert_true (wp_base_endpoint_is_linked (dev));
  g_assert_false (wp_base_endpoint_is_linked (ep4));
  g_assert_false (wp_base_endpoint_is_linked (ep3));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep5 == src);
  g_assert_true (dev == sink);

  /* Remove ep4, ep5 and ep3 */
  wp_config_policy_context_remove_endpoint (ctx, ep4);
  wp_config_policy_context_remove_endpoint (ctx, ep5);
  wp_config_policy_context_remove_endpoint (ctx, ep3);
}

static void
playback_keep (TestConfigPolicyFixture *f, gconstpointer data)
{
  g_autoptr (WpConfigPolicyContext) ctx = wp_config_policy_context_new (f->core,
      "config-policy/config-playback-keep");
  g_autoptr (WpBaseEndpointLink) link = NULL;
  g_autoptr (WpBaseEndpoint) src = NULL;
  g_autoptr (WpBaseEndpoint) sink = NULL;
  g_autoptr (WpBaseEndpoint) ep1 = NULL;
  g_autoptr (WpBaseEndpoint) ep2 = NULL;
  g_autoptr (WpBaseEndpoint) ep3 = NULL;

  /* Create the device endpoint */
  ep1 = wp_config_policy_context_add_endpoint (ctx, "ep1", "Fake/Sink",
      PW_DIRECTION_INPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep1);
  g_assert_null (link);
  g_assert_false (wp_base_endpoint_is_linked (ep1));

  /* Create the first client endpoint */
  ep2 = wp_config_policy_context_add_endpoint (ctx, "ep2", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep2));
  g_assert_true (wp_base_endpoint_is_linked (ep1));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep2 == src);
  g_assert_true (ep1 == sink);

  /* Create the second client endpoint */
  ep3 = wp_config_policy_context_add_endpoint (ctx, "ep3", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep3));
  g_assert_true (wp_base_endpoint_is_linked (ep1));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep3 == src);
  g_assert_true (ep1 == sink);

  /* Make sure ep2 is still linked after ep3 was linked */
  g_assert_true (wp_base_endpoint_is_linked (ep2));

  /* Remove the client endpoints */
  wp_config_policy_context_remove_endpoint (ctx, ep2);
  wp_config_policy_context_remove_endpoint (ctx, ep3);
}

static void
playback_role (TestConfigPolicyFixture *f, gconstpointer data)
{
  g_autoptr (WpConfigPolicyContext) ctx = wp_config_policy_context_new (f->core,
      "config-policy/config-playback-role");
  g_autoptr (WpBaseEndpointLink) link = NULL;
  g_autoptr (WpBaseEndpoint) src = NULL;
  g_autoptr (WpBaseEndpoint) sink = NULL;
  g_autoptr (WpBaseEndpoint) dev = NULL;
  g_autoptr (WpBaseEndpoint) ep1 = NULL;
  g_autoptr (WpBaseEndpoint) ep2 = NULL;
  g_autoptr (WpBaseEndpoint) ep3 = NULL;

  /* Create the device with 2 roles: "0" with id 0, and "1" with id 1 */
  dev = wp_config_policy_context_add_endpoint (ctx, "dev", "Fake/Sink",
      PW_DIRECTION_INPUT, NULL, NULL, 2, &link);
  g_assert_nonnull (dev);
  g_assert_null (link);
  g_assert_false (wp_base_endpoint_is_linked (dev));

  /* Create the first client endpoint with role "0" and make sure the role
   * defined in the configuration file which is "1" is used */
  ep1 = wp_config_policy_context_add_endpoint (ctx, "ep1", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, "0", 0, &link);
  g_assert_nonnull (ep1);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep1));
  g_assert_true (wp_base_endpoint_is_linked (dev));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep1 == src);
  g_assert_true (dev == sink);
  g_assert_true (
      WP_STREAM_ID_NONE == wp_base_endpoint_link_get_source_stream (link));
  g_assert_true (1 == wp_base_endpoint_link_get_sink_stream (link));
  wp_config_policy_context_remove_endpoint (ctx, ep1);

  /* Create the second client endpoint with role "1" and make sure it uses it
   * because there is none defined in the configuration file */
  ep2 = wp_config_policy_context_add_endpoint (ctx, "ep2", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, "1", 0, &link);
  g_assert_nonnull (ep2);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep2));
  g_assert_true (wp_base_endpoint_is_linked (dev));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep2 == src);
  g_assert_true (dev == sink);
  g_assert_true (
      WP_STREAM_ID_NONE == wp_base_endpoint_link_get_source_stream (link));
  g_assert_true (1 == wp_base_endpoint_link_get_sink_stream (link));
  wp_config_policy_context_remove_endpoint (ctx, ep2);

  /* Create the third client endpoint without role and make sure it uses the
   * lowest priority on from the streams file because the endpoint-link file
   * does not have any either */
  ep3 = wp_config_policy_context_add_endpoint (ctx, "ep3", "Stream/Output/Fake",
      PW_DIRECTION_OUTPUT, NULL, NULL, 0, &link);
  g_assert_nonnull (ep3);
  g_assert_nonnull (link);
  g_assert_true (wp_base_endpoint_is_linked (ep3));
  g_assert_true (wp_base_endpoint_is_linked (dev));
  src = wp_base_endpoint_link_get_source_endpoint (link);
  sink = wp_base_endpoint_link_get_sink_endpoint (link);
  g_assert_true (ep3 == src);
  g_assert_true (dev == sink);
  g_assert_true (
      WP_STREAM_ID_NONE == wp_base_endpoint_link_get_source_stream (link));
  g_assert_true (0 == wp_base_endpoint_link_get_sink_stream (link));
  wp_config_policy_context_remove_endpoint (ctx, ep3);
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
