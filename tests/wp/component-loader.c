/* WirePlumber
 *
 * Copyright © 2023 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include "../common/base-test-fixture.h"

struct _WpTestPlugin
{
  WpPlugin parent;
  gboolean enabled;
};

#define WP_TYPE_TEST_PLUGIN (wp_test_plugin_get_type ())
G_DECLARE_FINAL_TYPE (WpTestPlugin, wp_test_plugin, WP, TEST_PLUGIN, WpPlugin)
G_DEFINE_TYPE (WpTestPlugin, wp_test_plugin, WP_TYPE_PLUGIN)

static void
wp_test_plugin_init (WpTestPlugin * self)
{
}

static void
wp_test_plugin_enable (WpPlugin * self, WpTransition * transition)
{
  WP_TEST_PLUGIN (self)->enabled = TRUE;

  if (g_str_equal (wp_plugin_get_name (self), "fail")) {
    wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVALID_ARGUMENT, "fail"));
  } else {
    wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
  }
}

static void
wp_test_plugin_class_init (WpTestPluginClass * klass)
{
  WpPluginClass *pclass = (WpPluginClass *) klass;
  pclass->enable = wp_test_plugin_enable;
}


struct _WpTestCompLoader
{
  GObject parent;
  GPtrArray *history;
};

static void wp_test_comp_loader_iface_init (WpComponentLoaderInterface * iface);

#define WP_TYPE_TEST_COMP_LOADER (wp_test_comp_loader_get_type ())
G_DECLARE_FINAL_TYPE (WpTestCompLoader, wp_test_comp_loader,
                      WP, TEST_COMP_LOADER, GObject)
G_DEFINE_TYPE_WITH_CODE (WpTestCompLoader, wp_test_comp_loader,
                         G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (
                         WP_TYPE_COMPONENT_LOADER,
                         wp_test_comp_loader_iface_init))

static void
wp_test_comp_loader_init (WpTestCompLoader * self)
{
  self->history = g_ptr_array_new_with_free_func (g_free);
}

static void
wp_test_comp_loader_finalize (GObject * self)
{
  g_clear_pointer (&WP_TEST_COMP_LOADER (self)->history, g_ptr_array_unref);
  G_OBJECT_CLASS (wp_test_comp_loader_parent_class)->finalize (self);
}

static void
wp_test_comp_loader_class_init (WpTestCompLoaderClass * klass)
{
  GObjectClass *oclass = (GObjectClass *) klass;
  oclass->finalize = wp_test_comp_loader_finalize;
}

static gboolean
wp_test_comp_loader_supports_type (WpComponentLoader * cl, const gchar * type)
{
  return g_str_equal (type, "test");
}

static void
wp_test_comp_loader_load (WpComponentLoader * self, WpCore * core,
    const gchar * component, const gchar * type, WpSpaJson * args,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer data)
{
  g_autoptr (GTask) task = g_task_new (self, cancellable, callback, data);
  GObject *plugin = g_object_new (WP_TYPE_TEST_PLUGIN,
      "name", component,
      "core", core,
      NULL);
  g_ptr_array_add (WP_TEST_COMP_LOADER (self)->history, g_strdup (component));
  g_task_return_pointer (task, plugin, g_object_unref);
}

static GObject *
wp_test_comp_loader_load_finish (WpComponentLoader * self,
    GAsyncResult * res, GError ** error)
{
  return g_task_propagate_pointer (G_TASK (res), error);
}

static void
wp_test_comp_loader_iface_init (WpComponentLoaderInterface * iface)
{
  iface->supports_type = wp_test_comp_loader_supports_type;
  iface->load = wp_test_comp_loader_load;
  iface->load_finish = wp_test_comp_loader_load_finish;
}


typedef struct {
  WpBaseTestFixture base;
  WpTestCompLoader *loader;
} TestFixture;

static void
test_setup (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, 0);
  self->loader = g_object_new (WP_TYPE_TEST_COMP_LOADER, NULL);
  wp_core_register_object (self->base.core, self->loader);
}

static void
test_teardown (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_teardown (&self->base);
}

static void
on_component_loaded (WpCore * core, GAsyncResult * res, TestFixture *f)
{
  gboolean loaded;
  GError *error = NULL;

  loaded = wp_core_load_component_finish (core, res, &error);
  g_assert_no_error (error);
  g_assert_true (loaded);

  g_main_loop_quit (f->base.loop);
}

static void
test_load (TestFixture *f, gconstpointer data)
{
  wp_core_load_component (f->base.core, "name123", "test", NULL,
      "feature.name123", NULL, (GAsyncReadyCallback) on_component_loaded, f);
  g_main_loop_run (f->base.loop);

  g_autoptr (WpPlugin) plugin = wp_plugin_find (f->base.core, "name123");
  g_assert_nonnull (plugin);
  g_assert_true (WP_IS_TEST_PLUGIN (plugin));
  g_assert_true (WP_TEST_PLUGIN (plugin)->enabled);
  g_assert_true (wp_core_test_feature (f->base.core, "feature.name123"));
}

static void
on_component_failed (WpCore * core, GAsyncResult * res, TestFixture *f)
{
  gboolean loaded;
  g_autoptr (GError) error = NULL;

  loaded = wp_core_load_component_finish (core, res, &error);
  g_assert_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT);
  g_assert_false (loaded);

  g_main_loop_quit (f->base.loop);
}

static void
test_load_failure (TestFixture *f, gconstpointer data)
{
  wp_core_load_component (f->base.core, "fail", "test", NULL,
      "feature.fail", NULL, (GAsyncReadyCallback) on_component_failed, f);
  g_main_loop_run (f->base.loop);

  g_assert_cmpuint (f->loader->history->len, ==, 1);
  g_assert_cmpstr (f->loader->history->pdata[0], ==, "fail");

  g_autoptr (WpPlugin) plugin = wp_plugin_find (f->base.core, "fail");
  g_assert_null (plugin);
  g_assert_false (wp_core_test_feature (f->base.core, "feature.fail"));
}

static void
test_dependencies_setup (TestFixture *f, gconstpointer data)
{
  f->base.conf_file =
    g_strdup_printf ("%s/component-loader.conf", g_getenv ("G_TEST_SRCDIR"));
  test_setup (f, data);
}

static void
test_dependencies (TestFixture *f, gconstpointer data)
{
  wp_core_load_component (f->base.core, "test", "profile", NULL,
      NULL, NULL, (GAsyncReadyCallback) on_component_loaded, f);
  g_main_loop_run (f->base.loop);

  // NULL-terminate the array
  g_ptr_array_add (f->loader->history, NULL);

  /* verify the order of loading the plugins was as expected */
  const gchar *expected[] = {
    "five", "one", "seven", "ten", "eleven", "six", "two", "three", "four", "nine", NULL };
  g_assert_cmpstrv (f->loader->history->pdata, expected);

  g_assert_true (wp_core_test_feature (f->base.core, "support.one"));
  g_assert_true (wp_core_test_feature (f->base.core, "support.two"));
  g_assert_true (wp_core_test_feature (f->base.core, "support.three"));
  g_assert_true (wp_core_test_feature (f->base.core, "support.four"));
  g_assert_true (wp_core_test_feature (f->base.core, "virtual.four"));
  g_assert_true (wp_core_test_feature (f->base.core, "support.five"));
  g_assert_true (wp_core_test_feature (f->base.core, "support.six"));
  g_assert_false (wp_core_test_feature (f->base.core, "support.seven"));
  g_assert_false (wp_core_test_feature (f->base.core, "support.eight"));
  g_assert_true (wp_core_test_feature (f->base.core, "support.nine"));
  g_assert_true (wp_core_test_feature (f->base.core, "support.ten"));
  g_assert_true (wp_core_test_feature (f->base.core, "support.eleven"));
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/comploader/load", TestFixture, NULL,
      test_setup, test_load, test_teardown);
  g_test_add ("/wp/comploader/load_failure", TestFixture, NULL,
      test_setup, test_load_failure, test_teardown);
  g_test_add ("/wp/comploader/dependencies", TestFixture, NULL,
      test_dependencies_setup, test_dependencies, test_teardown);

  return g_test_run ();
}
