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
 * export context, on its own thread.
 */

#define LOOPBACK_ARGS \
  "{ audio.channels = 2 " \
  "  capture.props = { " \
  "    node.name = \"test.export-context.capture\" " \
  "    media.class = \"Audio/Sink\" " \
  "  } " \
  "  playback.props = { " \
  "    node.name = \"test.export-context.playback\" " \
  "    node.passive = true " \
  "  } " \
  "}"

typedef struct {
  WpBaseTestFixture base;
} TestFixture;

static void
test_export_context_setup (TestFixture * f, gconstpointer data)
{
  wp_base_test_fixture_setup (&f->base, 0);

  /* the loopback module's streams export a local pw_impl_node through the
     client-node factory; the test server's daemon config already provides it */
}

static void
test_export_context_teardown (TestFixture * f, gconstpointer data)
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

static int
count_loopback_nodes (void *data, struct pw_global *global)
{
  guint *found = data;
  const struct pw_properties *props;
  const gchar *name;

  if (!spa_streq (pw_global_get_type (global), PW_TYPE_INTERFACE_Node))
    return 0;

  props = pw_global_get_properties (global);
  name = props ? pw_properties_get (props, PW_KEY_NODE_NAME) : NULL;

  if (spa_streq (name, "test.export-context.capture") ||
      spa_streq (name, "test.export-context.playback"))
    (*found)++;

  return 0;
}

/*
 * Counts the loopback's nodes on the server side, without ever iterating our
 * own GMainContext, i.e. exactly what a blocked WirePlumber main loop can see.
 */
static guint
server_loopback_nodes (TestFixture * f)
{
  g_autoptr (WpTestServerLocker) lock =
      wp_test_server_locker_new (&f->base.server);
  guint found = 0;

  pw_context_for_each_global (f->base.server.context, count_loopback_nodes,
      &found);

  return found;
}

static void
test_module_runs_while_main_loop_is_blocked (TestFixture * f,
    gconstpointer data)
{
  g_autoptr (WpImplModule) module = NULL;
  guint found = 0;

  wp_core_load_component (f->base.core, "export-context", "built-in", NULL,
      NULL, NULL, (GAsyncReadyCallback) on_component_loaded, f);
  g_main_loop_run (f->base.loop);

  module = wp_impl_module_load (f->base.core, "libpipewire-module-loopback",
      LOOPBACK_ARGS, NULL);
  g_assert_nonnull (module);

  /* From here on we deliberately never iterate the GMainContext, simulating a
     long event-dispatch iteration. Without the export context nothing can
     possibly happen, because the module would share our blocked pw_loop. */
  for (guint i = 0; i < 100 && found < 2; i++) {
    g_usleep (20 * 1000);
    found = server_loopback_nodes (f);
  }

  g_assert_cmpuint (found, ==, 2);
}

/*
 * Without the export-context component, module loading must keep working the
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

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/export-context/module-runs-while-main-loop-is-blocked",
      TestFixture, NULL,
      test_export_context_setup, test_module_runs_while_main_loop_is_blocked,
      test_export_context_teardown);

  g_test_add ("/wp/export-context/module-fallback-to-main-context",
      TestFixture, NULL,
      test_export_context_setup, test_module_fallback_to_main_context,
      test_export_context_teardown);

  return g_test_run ();
}
