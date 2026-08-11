/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

#include <pipewire/global.h>

/*
 * PipeWire modules that WirePlumber loads in-process must not depend on
 * WirePlumber's GMainContext being iterated: that context also dispatches the
 * event dispatcher, whose hooks are Lua and can run for a long time, and
 * GSource priorities do not preempt. A loopback that only gets to negotiate its
 * streams once a hook returns loses the first audio periods.
 *
 * These tests load a real loopback module and then block the main thread
 * entirely, the way a long event-dispatch iteration would. The module is
 * expected to connect and export its two nodes anyway, because it lives in the
 * client context, on its own thread.
 */

#define LOOPBACK_ARGS \
  "{ audio.channels = 2 " \
  "  capture.props = { " \
  "    node.name = \"test.client-context.capture\" " \
  "    media.class = \"Audio/Sink\" " \
  "  } " \
  "  playback.props = { " \
  "    node.name = \"test.client-context.playback\" " \
  "    node.passive = true " \
  "  } " \
  "}"

typedef struct {
  WpBaseTestFixture base;
} TestFixture;

static void
test_client_context_setup (TestFixture * f, gconstpointer data)
{
  wp_base_test_fixture_setup (&f->base, 0);

  /* the loopback module's streams export a local pw_impl_node through the
     client-node factory; the test server's daemon config already provides it */
}

static void
test_client_context_teardown (TestFixture * f, gconstpointer data)
{
  wp_base_test_fixture_teardown (&f->base);
}

static void
on_component_loaded (WpCore * core, GAsyncResult * res, TestFixture * f)
{
  g_autoptr (GError) error = NULL;
  gboolean ok = wp_core_load_component_finish (core, res, &error);
  g_assert_no_error (error);
  g_assert_true (ok);
  g_main_loop_quit (f->base.loop);
}

struct node_count {
  const gchar * const *names;
  guint found;
};

static int
count_nodes (void *data, struct pw_global *global)
{
  struct node_count *c = data;
  const struct pw_properties *props;
  const gchar *name;

  if (!spa_streq (pw_global_get_type (global), PW_TYPE_INTERFACE_Node))
    return 0;

  props = pw_global_get_properties (global);
  name = props ? pw_properties_get (props, PW_KEY_NODE_NAME) : NULL;

  for (guint i = 0; c->names[i]; i++) {
    if (spa_streq (name, c->names[i]))
      c->found++;
  }

  return 0;
}

/*
 * Counts nodes on the server side, without ever iterating our own
 * GMainContext, i.e. exactly what a blocked WirePlumber main loop can see.
 */
static guint
server_nodes (TestFixture * f, const gchar * const * names)
{
  g_autoptr (WpTestServerLocker) lock =
      wp_test_server_locker_new (&f->base.server);
  struct node_count c = { .names = names, .found = 0 };

  pw_context_for_each_global (f->base.server.context, count_nodes, &c);

  return c.found;
}

static const gchar * const loopback_node_names[] = {
  "test.client-context.capture", "test.client-context.playback", NULL
};

static const gchar * const local_node_names[] = {
  "test.client-context.localnode", NULL
};

static void
load_client_context (TestFixture * f)
{
  wp_core_load_component (f->base.core, "client-context", "built-in", NULL,
      NULL, NULL, (GAsyncReadyCallback) on_component_loaded, f);
  g_main_loop_run (f->base.loop);
}

static void
test_module_runs_while_main_loop_is_blocked (TestFixture * f,
    gconstpointer data)
{
  g_autoptr (WpImplModule) module = NULL;
  guint found = 0;

  load_client_context (f);

  module = wp_impl_module_load (f->base.core, "libpipewire-module-loopback",
      LOOPBACK_ARGS, NULL);
  g_assert_nonnull (module);

  /* From here on we deliberately never iterate the GMainContext, simulating a
     long event-dispatch iteration. Without the client context nothing can
     possibly happen, because the module would share our blocked pw_loop. */
  for (guint i = 0; i < 100 && found < 2; i++) {
    g_usleep (20 * 1000);
    found = server_nodes (f, loopback_node_names);
  }

  g_assert_cmpuint (found, ==, 2);
}

/*
 * Without the client-context component, module loading must keep working the
 * way it always has, in the core's own pw_context.
 */
static void
test_module_fallback_to_main_context (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpImplModule) module = NULL;
  struct pw_impl_module *pw_module = NULL;

  module = wp_impl_module_load (f->base.core, "libpipewire-module-loopback",
      LOOPBACK_ARGS, NULL);
  g_assert_nonnull (module);

  g_object_get (module, "pw-impl-module", &pw_module, NULL);
  g_assert_nonnull (pw_module);
  g_assert_true (pw_impl_module_get_context (pw_module) ==
      wp_core_get_pw_context (f->base.core));
}

static void
on_object_activated (WpObject * object, GAsyncResult * res, TestFixture * f)
{
  g_autoptr (GError) error = NULL;
  gboolean ok = wp_object_activate_finish (object, res, &error);
  g_assert_no_error (error);
  g_assert_true (ok);
  g_main_loop_quit (f->base.loop);
}

/*
 * A LocalNode is created by a factory of the client context and exported over
 * its connection, so this exercises the locked factory call, the locked
 * pw_core_export() and the "bound" event coming back from the other thread.
 */
static void
test_local_node_in_client_context (TestFixture * f, gconstpointer data)
{
  WpImplNode *node = NULL;
  struct pw_impl_node *pw_node = NULL;
  guint bound_id;

  load_client_context (f);

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
  g_assert_true (pw_impl_node_get_context (pw_node) !=
      wp_core_get_pw_context (f->base.core));

  wp_object_activate (WP_OBJECT (node), WP_PROXY_FEATURE_BOUND, NULL,
      (GAsyncReadyCallback) on_object_activated, f);
  g_main_loop_run (f->base.loop);

  bound_id = wp_proxy_get_bound_id (WP_PROXY (node));
  g_assert_cmpuint (bound_id, !=, SPA_ID_INVALID);
  g_assert_cmpuint (server_nodes (f, local_node_names), ==, 1);

  /* destroying it must unexport and destroy the pw_impl_node on the other
     thread, without deadlocking */
  g_object_unref (node);

  while (g_main_context_iteration (f->base.context, FALSE));
  for (guint i = 0; i < 100 && server_nodes (f, local_node_names) > 0; i++)
    g_usleep (20 * 1000);

  g_assert_cmpuint (server_nodes (f, local_node_names), ==, 0);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/client-context/module-runs-while-main-loop-is-blocked",
      TestFixture, NULL,
      test_client_context_setup, test_module_runs_while_main_loop_is_blocked,
      test_client_context_teardown);

  g_test_add ("/wp/client-context/module-fallback-to-main-context",
      TestFixture, NULL,
      test_client_context_setup, test_module_fallback_to_main_context,
      test_client_context_teardown);

  g_test_add ("/wp/client-context/local-node",
      TestFixture, NULL,
      test_client_context_setup, test_local_node_in_client_context,
      test_client_context_teardown);

  return g_test_run ();
}
