/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/* include after base-test-fixture.h */

static void
on_client_context_loaded (WpCore * core, GAsyncResult * res,
    WpBaseTestFixture * f)
{
  g_autoptr (GError) error = NULL;

  g_assert_true (wp_core_load_component_finish (core, res, &error));
  g_assert_no_error (error);
  g_main_loop_quit (f->loop);
}

/* loads the client-context component on the fixture's core */
static G_GNUC_UNUSED void
wp_test_client_context_load (WpBaseTestFixture * f)
{
  /* the main context has no spa-libs or modules to inherit, because the
     fixture creates the core without a WpConf */
  g_autoptr (WpSpaJson) args = wp_spa_json_new_from_string (
      "{"
      "  context.spa-libs = {"
      "    audio.convert.* = audioconvert/libspa-audioconvert"
      "    support.*       = support/libspa-support"
      "  }"
      "  context.modules = ["
      "    { name = libpipewire-module-protocol-native }"
      "    { name = libpipewire-module-client-node }"
      "    { name = libpipewire-module-adapter }"
      "  ]"
      "}");

  wp_core_load_component (f->core, "client-context", "built-in", args,
      NULL, NULL, (GAsyncReadyCallback) on_client_context_loaded, f);
  g_main_loop_run (f->loop);
}

struct wp_test_run_data {
  void (*func) (gpointer data);
  gpointer data;
  GMutex lock;
  GCond cond;
  gboolean done;
};

static int
wp_test_do_run (struct spa_loop *loop, bool async, uint32_t seq,
    const void *data, size_t size, void *user_data)
{
  struct wp_test_run_data *r = user_data;

  r->func (r->data);

  g_mutex_lock (&r->lock);
  r->done = TRUE;
  g_cond_signal (&r->cond);
  g_mutex_unlock (&r->lock);
  return 0;
}

/* runs @func on the client context's thread, which owns its pw_context, and
   waits for it; a blocking invoke cannot be used for this from here, as older
   PipeWire versions require the caller to hold the thread loop's lock */
static G_GNUC_UNUSED void
wp_test_client_context_run (WpBaseTestFixture * f, void (*func) (gpointer),
    gpointer data)
{
  g_autoptr (WpClientContext) ctx = wp_client_context_find (f->core);
  struct pw_context *context = wp_context_get_pw_context (WP_CONTEXT (ctx));
  struct wp_test_run_data r = { .func = func, .data = data, .done = FALSE };

  g_mutex_init (&r.lock);
  g_cond_init (&r.cond);

  pw_loop_invoke (pw_context_get_main_loop (context), wp_test_do_run,
      SPA_ID_INVALID, NULL, 0, false, &r);

  g_mutex_lock (&r.lock);
  while (!r.done)
    g_cond_wait (&r.cond, &r.lock);
  g_mutex_unlock (&r.lock);

  g_cond_clear (&r.cond);
  g_mutex_clear (&r.lock);
}
