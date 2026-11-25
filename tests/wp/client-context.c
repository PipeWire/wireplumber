/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"
#include "../common/client-context-utils.h"

#include <pipewire/global.h>

#define LOOPBACK_ARGS \
  "{ capture.props = { node.name = \"test.client-context.capture\" } " \
  "  playback.props = { node.name = \"test.client-context.playback\" } }"

typedef struct {
  WpBaseTestFixture base;
} TestFixture;

static void
test_client_context_setup (TestFixture * f, gconstpointer data)
{
  wp_base_test_fixture_setup (&f->base, 0);
}

static void
test_client_context_teardown (TestFixture * f, gconstpointer data)
{
  wp_base_test_fixture_teardown (&f->base);
}

/*
 * Loading the client-context component registers a client context on the core,
 * which wp_client_context_find() then returns, with its own connection.
 */
static void
test_load (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpClientContext) ctx = NULL;
  g_autoptr (WpCore) core = NULL;

  ctx = wp_client_context_find (f->base.core);
  g_assert_null (ctx);

  wp_test_client_context_load (&f->base);

  ctx = wp_client_context_find (f->base.core);
  g_assert_nonnull (ctx);
  core = wp_context_get_core (WP_CONTEXT (ctx));
  g_assert_true (core == f->base.core);
  g_assert_nonnull (wp_context_get_pw_core (WP_CONTEXT (ctx)));
}

static int
count_loopback_node (void *data, struct pw_global *global)
{
  guint *found = data;
  const struct pw_properties *props;
  const gchar *name;

  if (!spa_streq (pw_global_get_type (global), PW_TYPE_INTERFACE_Node))
    return 0;

  props = pw_global_get_properties (global);
  name = props ? pw_properties_get (props, PW_KEY_NODE_NAME) : NULL;
  if (spa_streq (name, "test.client-context.capture") ||
      spa_streq (name, "test.client-context.playback"))
    (*found)++;
  return 0;
}

static void
on_server_global_added (void *data, struct pw_global *global)
{
  pw_thread_loop_signal (data, false);
}

static const struct pw_context_events server_context_events = {
  PW_VERSION_CONTEXT_EVENTS,
  .global_added = on_server_global_added,
};

/* waits on the server's thread until both loopback nodes are registered there,
   without iterating the main loop, and returns how many were found */
static guint
wait_server_loopback_nodes (TestFixture * f)
{
  struct pw_thread_loop *loop = f->base.server.thread_loop;
  struct spa_hook listener;
  guint found;

  pw_thread_loop_lock (loop);
  pw_context_add_listener (f->base.server.context, &listener,
      &server_context_events, loop);

  for (;;) {
    found = 0;
    pw_context_for_each_global (f->base.server.context, count_loopback_node,
        &found);
    /* any result other than 0 means the wait timed out */
    if (found >= 2 || pw_thread_loop_timed_wait (loop, 5) != 0)
      break;
  }

  spa_hook_remove (&listener);
  pw_thread_loop_unlock (loop);
  return found;
}

static void
on_module_loaded (WpObject * object, GAsyncResult * res, gboolean * done)
{
  g_autoptr (GError) error = NULL;
  gboolean ok = wp_object_activate_finish (object, res, &error);
  g_assert_no_error (error);
  g_assert_true (ok);
  *done = TRUE;
}

/*
 * PipeWire modules that WirePlumber loads in-process must not depend on
 * WirePlumber's GMainContext being iterated: that context also dispatches the
 * event dispatcher, whose hooks are Lua and can run for a long time, and
 * GSource priorities do not preempt. A loopback that only gets to negotiate its
 * streams once a hook returns loses the first audio periods.
 *
 * This test loads a real loopback module and then blocks the main thread
 * entirely, the way a long event-dispatch iteration would. The module is
 * expected to connect and export its two nodes anyway, because it lives in the
 * client context, on its own thread.
 */
static void
test_runs_while_main_loop_is_blocked (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpImplModule) module = wp_impl_module_new (f->base.core,
      "libpipewire-module-loopback", LOOPBACK_ARGS, NULL);
  gboolean done = FALSE;

  wp_test_client_context_load (&f->base);

  wp_object_activate (WP_OBJECT (module), WP_IMPL_MODULE_FEATURE_LOADED, NULL,
      (GAsyncReadyCallback) on_module_loaded, &done);
  while (!done)
    g_main_context_iteration (f->base.context, TRUE);

  /* From here on we deliberately never iterate the GMainContext, simulating a
     long event-dispatch iteration. Without the client context nothing can
     possibly happen, because the module would share our blocked pw_loop. */
  g_assert_cmpuint (wait_server_loopback_nodes (f), ==, 2);
}

/*
 * A LocalNode is created by a factory of the client context and exported over
 * its connection, so this exercises the locked factory call, the locked
 * pw_core_export() and the "bound" event coming back from the other thread.
 */
static void
test_local_node_in_client_context (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpObjectManager) om = NULL;
  WpImplNode *node = NULL;
  struct pw_impl_node *pw_node = NULL;
  guint bound_id;

  wp_test_client_context_load (&f->base);

  node = wp_impl_node_new_from_pw_factory (f->base.core, "adapter",
      wp_properties_new (
          "factory.name", "support.null-audio-sink",
          "node.name", "test.client-context.localnode",
          "media.class", "Audio/Sink",
          "audio.position", "[FL,FR]",
          NULL));
  g_assert_nonnull (node);

  /* it must live in the client context, not in the core's own one */
  g_object_get (node, "pw-impl-node", &pw_node, NULL);
  g_assert_nonnull (pw_node);
  WpMainContext *wp_ctx =
      wp_core_get_main_context (f->base.core);
  g_assert_true (pw_impl_node_get_context (pw_node) !=
      wp_context_get_pw_context (WP_CONTEXT (wp_ctx)));

  test_object_activate (&f->base, WP_OBJECT (node), WP_PROXY_FEATURE_BOUND);

  bound_id = wp_proxy_get_bound_id (WP_PROXY (node));
  g_assert_cmpuint (bound_id, !=, SPA_ID_INVALID);

  /* the exported node becomes visible through the main core's registry; the
     global arrives on the main connection, possibly after the "bound" event
     came back on the client context's one */
  om = wp_object_manager_new (f->base.core);
  wp_object_manager_add_interest (om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_NODE_NAME, "=s",
      "test.client-context.localnode", NULL);
  test_ensure_object_manager_is_installed (om, f->base.loop);
  while (wp_object_manager_get_n_objects (om) < 1)
    g_main_context_iteration (f->base.context, TRUE);
  g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 1);

  /* destroying it must unexport and destroy the pw_impl_node on the other
     thread, without deadlocking */
  g_object_unref (node);

  while (wp_object_manager_get_n_objects (om) > 0)
    g_main_context_iteration (f->base.context, TRUE);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/client-context/load", TestFixture, NULL,
      test_client_context_setup, test_load,
      test_client_context_teardown);

  g_test_add ("/wp/client-context/runs-while-main-loop-is-blocked",
      TestFixture, NULL,
      test_client_context_setup, test_runs_while_main_loop_is_blocked,
      test_client_context_teardown);

  g_test_add ("/wp/client-context/local-node",
      TestFixture, NULL,
      test_client_context_setup, test_local_node_in_client_context,
      test_client_context_teardown);

  return g_test_run ();
}
