/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"
#include "../common/client-context-utils.h"

#include <pipewire/global.h>
#include <pipewire/impl-module.h>

#include <pthread.h>

#define LOOPBACK_ARGS \
  "{ capture.props = { node.name = \"test.impl-module.capture\" } " \
  "  playback.props = { node.name = \"test.impl-module.playback\" } }"

typedef struct {
  WpBaseTestFixture base;
} TestFixture;

static void
test_impl_module_setup (TestFixture * f, gconstpointer data)
{
  wp_base_test_fixture_setup (&f->base, 0);
}

static void
test_impl_module_teardown (TestFixture * f, gconstpointer data)
{
  wp_base_test_fixture_teardown (&f->base);
}

struct activate_data {
  gboolean done;
  gboolean ok;
  GError *error;
};

static void
on_activated (WpObject * object, GAsyncResult * res, struct activate_data * d)
{
  d->ok = wp_object_activate_finish (object, res, &d->error);
  d->done = TRUE;
}

static gboolean
activate_module (TestFixture * f, WpImplModule * module, GError ** error)
{
  struct activate_data d = { 0, };

  wp_object_activate (WP_OBJECT (module), WP_IMPL_MODULE_FEATURE_LOADED, NULL,
      (GAsyncReadyCallback) on_activated, &d);
  while (!d.done)
    g_main_context_iteration (f->base.context, TRUE);

  if (d.error)
    g_propagate_error (error, d.error);
  return d.ok;
}

struct module_lookup {
  struct pw_impl_module *module;
  guint count;
};

static int
find_loopback_module (void *data, struct pw_global *global)
{
  struct module_lookup *l = data;
  const struct pw_properties *props;

  if (!spa_streq (pw_global_get_type (global), PW_TYPE_INTERFACE_Module))
    return 0;

  props = pw_global_get_properties (global);
  if (props && spa_streq (pw_properties_get (props, PW_KEY_MODULE_NAME),
          "libpipewire-module-loopback")) {
    l->module = pw_global_get_object (global);
    l->count++;
  }
  return 0;
}

/* must run on the thread of the context */
static struct module_lookup
lookup_loopback_module (struct pw_context *context)
{
  struct module_lookup l = { NULL, 0 };
  pw_context_for_each_global (context, find_loopback_module, &l);
  return l;
}

/* the loopback modules loaded in the main context, which runs on this thread */
static struct module_lookup
main_loopback_modules (TestFixture * f)
{
  WpMainContext *ctx = wp_core_get_main_context (f->base.core);
  return lookup_loopback_module (wp_context_get_pw_context (WP_CONTEXT (ctx)));
}

static void
test_impl_module_basic (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpImplModule) module = wp_impl_module_new (f->base.core,
      "libpipewire-module-loopback", LOOPBACK_ARGS, NULL);

  g_assert_nonnull (module);
  g_assert_cmphex (wp_object_get_supported_features (WP_OBJECT (module)), ==,
      WP_IMPL_MODULE_FEATURE_LOADED);
  g_assert_false (wp_object_test_active_features (WP_OBJECT (module),
      WP_IMPL_MODULE_FEATURE_LOADED));
  g_assert_cmpuint (main_loopback_modules (f).count, ==, 0);

  g_assert_cmpstr (wp_impl_module_get_name (module), ==,
      "libpipewire-module-loopback");
  g_assert_cmpstr (wp_impl_module_get_args (module), ==, LOOPBACK_ARGS);
  g_assert_null (wp_impl_module_get_properties (module));
}

static void
test_impl_module_load_unload (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpImplModule) module = wp_impl_module_new (f->base.core,
      "libpipewire-module-loopback", LOOPBACK_ARGS, NULL);
  g_autoptr (GError) error = NULL;

  g_assert_true (activate_module (f, module, &error));
  g_assert_no_error (error);
  g_assert_true (wp_object_test_active_features (WP_OBJECT (module),
      WP_IMPL_MODULE_FEATURE_LOADED));
  g_assert_cmpuint (main_loopback_modules (f).count, ==, 1);

  wp_object_deactivate (WP_OBJECT (module), WP_IMPL_MODULE_FEATURE_LOADED);
  g_assert_false (wp_object_test_active_features (WP_OBJECT (module),
      WP_IMPL_MODULE_FEATURE_LOADED));
  g_assert_cmpuint (main_loopback_modules (f).count, ==, 0);

  /* deactivating again has no effect */
  wp_object_deactivate (WP_OBJECT (module), WP_IMPL_MODULE_FEATURE_LOADED);
  g_assert_false (wp_object_test_active_features (WP_OBJECT (module),
      WP_IMPL_MODULE_FEATURE_LOADED));
  g_assert_cmpuint (main_loopback_modules (f).count, ==, 0);
}

static void
test_impl_module_reload (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpProperties) props =
      wp_properties_new ("test.impl-module.prop", "value", NULL);
  g_autoptr (WpImplModule) module = wp_impl_module_new (f->base.core,
      "libpipewire-module-loopback", LOOPBACK_ARGS, props);
  g_autoptr (WpProperties) module_props =
      wp_impl_module_get_properties (module);

  g_assert_true (module_props == props);

  for (guint i = 0; i < 2; i++) {
    g_autoptr (GError) error = NULL;
    struct module_lookup l;

    g_assert_true (activate_module (f, module, &error));
    g_assert_no_error (error);
    g_assert_true (wp_object_test_active_features (WP_OBJECT (module),
        WP_IMPL_MODULE_FEATURE_LOADED));
    l = main_loopback_modules (f);
    g_assert_cmpuint (l.count, ==, 1);

    /* the properties are given to the module every time it is loaded */
    g_assert_cmpstr (pw_properties_get (
        pw_impl_module_get_properties (l.module), "test.impl-module.prop"),
        ==, "value");

    wp_object_deactivate (WP_OBJECT (module), WP_IMPL_MODULE_FEATURE_LOADED);
    g_assert_false (wp_object_test_active_features (WP_OBJECT (module),
        WP_IMPL_MODULE_FEATURE_LOADED));
    g_assert_cmpuint (main_loopback_modules (f).count, ==, 0);
  }
}

static void
test_impl_module_load_error (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpImplModule) module = wp_impl_module_new (f->base.core,
      "libpipewire-module-does-not-exist", NULL, NULL);
  g_autoptr (GError) error = NULL;

  g_assert_false (activate_module (f, module, &error));
  g_assert_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED);
  g_assert_nonnull (strstr (error->message, g_strerror (ENOENT)));
  g_assert_false (wp_object_test_active_features (WP_OBJECT (module),
      WP_IMPL_MODULE_FEATURE_LOADED));
}

static void
test_impl_module_unload_on_dispose (TestFixture * f, gconstpointer data)
{
  WpImplModule *module = wp_impl_module_new (f->base.core,
      "libpipewire-module-loopback", LOOPBACK_ARGS, NULL);
  g_autoptr (GError) error = NULL;

  g_assert_true (activate_module (f, module, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (main_loopback_modules (f).count, ==, 1);

  g_object_unref (module);
  g_assert_cmpuint (main_loopback_modules (f).count, ==, 0);
}

/* the tests below load the module in the client context */

struct client_destroy_data {
  struct pw_impl_module *module;
  struct spa_hook listener;
  pthread_t thread;
  guint count;
};

static int
do_nothing (struct spa_loop *loop, bool async, uint32_t seq,
    const void *data, size_t size, void *user_data)
{
  return 0;
}

/* behaves like module-filter-chain with an LV2 plugin in its graph: its streams
   schedule the destruction of the module while it is being destroyed, and then
   the LV2 plugin's cleanup does a blocking invoke on the context's main loop */
static void
on_client_module_destroy (void *data)
{
  struct client_destroy_data *d = data;
  struct pw_context *context = pw_impl_module_get_context (d->module);

  d->thread = pthread_self ();
  d->count++;

  pw_impl_module_schedule_destroy (d->module);
  pw_loop_invoke (pw_context_get_main_loop (context), do_nothing, 0, NULL, 0,
      true, NULL);
}

static const struct pw_impl_module_events client_destroy_events = {
  PW_VERSION_IMPL_MODULE_EVENTS,
  .destroy = on_client_module_destroy,
};

static void
run_nothing (gpointer data)
{
}

/* returns once everything queued on the client context's thread has run */
static void
flush_client_thread (TestFixture * f)
{
  wp_test_client_context_run (&f->base, run_nothing, NULL);
}

struct add_listener_data {
  struct pw_context *context;
  struct client_destroy_data *d;
};

static void
do_add_destroy_listener (gpointer data)
{
  struct add_listener_data *a = data;

  a->d->module = lookup_loopback_module (a->context).module;
  if (a->d->module)
    pw_impl_module_add_listener (a->d->module, &a->d->listener,
        &client_destroy_events, a->d);
}

/*
 * The module must be destroyed on the client context's thread. When it is
 * destroyed from the main thread instead, a blocking invoke on the context's
 * loop from within the module's teardown releases the loop lock, and the loop
 * thread then runs the destroy that the module has scheduled for itself,
 * destroying (and unloading) it a second time underneath the first one.
 */
static void
test_impl_module_unload_in_client_context (TestFixture * f,
    gconstpointer data)
{
  g_autoptr (WpImplModule) module = wp_impl_module_new (f->base.core,
      "libpipewire-module-loopback", LOOPBACK_ARGS, NULL);
  g_autoptr (WpClientContext) ctx = NULL;
  g_autoptr (GError) error = NULL;
  struct client_destroy_data d = { 0, };
  struct add_listener_data a = { .d = &d };

  wp_test_client_context_load (&f->base);

  g_assert_true (activate_module (f, module, &error));
  g_assert_no_error (error);

  ctx = wp_client_context_find (f->base.core);
  a.context = wp_context_get_pw_context (WP_CONTEXT (ctx));
  wp_test_client_context_run (&f->base, do_add_destroy_listener, &a);
  g_assert_nonnull (d.module);

  wp_object_deactivate (WP_OBJECT (module), WP_IMPL_MODULE_FEATURE_LOADED);

  g_assert_cmpuint (d.count, ==, 1);
  g_assert_false (pthread_equal (d.thread, pthread_self ()));

  /* run any destroy the module has left scheduled on the loop thread */
  flush_client_thread (f);
  g_assert_cmpuint (d.count, ==, 1);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/module/basic", TestFixture, NULL,
      test_impl_module_setup, test_impl_module_basic,
      test_impl_module_teardown);
  g_test_add ("/wp/module/load-unload", TestFixture, NULL,
      test_impl_module_setup, test_impl_module_load_unload,
      test_impl_module_teardown);
  g_test_add ("/wp/module/reload", TestFixture, NULL,
      test_impl_module_setup, test_impl_module_reload,
      test_impl_module_teardown);
  g_test_add ("/wp/module/load-error", TestFixture, NULL,
      test_impl_module_setup, test_impl_module_load_error,
      test_impl_module_teardown);
  g_test_add ("/wp/module/unload-on-dispose", TestFixture, NULL,
      test_impl_module_setup, test_impl_module_unload_on_dispose,
      test_impl_module_teardown);
  g_test_add ("/wp/module/unload-in-client-context", TestFixture, NULL,
      test_impl_module_setup, test_impl_module_unload_in_client_context,
      test_impl_module_teardown);

  return g_test_run ();
}
