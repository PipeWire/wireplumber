/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

/* a plain object to register on the core, so that the object manager has
   something to hand out that is not a proxy to a PipeWire global */
struct _TestDummy
{
  GObject parent;
  gchar *name;
};

G_DECLARE_FINAL_TYPE (TestDummy, test_dummy, TEST, DUMMY, GObject)
G_DEFINE_TYPE (TestDummy, test_dummy, G_TYPE_OBJECT)

enum { PROP_0, PROP_NAME };

static void
test_dummy_init (TestDummy * self)
{
}

static void
test_dummy_finalize (GObject * object)
{
  TestDummy *self = TEST_DUMMY (object);
  g_clear_pointer (&self->name, g_free);
  G_OBJECT_CLASS (test_dummy_parent_class)->finalize (object);
}

static void
test_dummy_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  TestDummy *self = TEST_DUMMY (object);

  switch (prop_id) {
  case PROP_NAME:
    g_value_set_string (value, self->name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
test_dummy_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  TestDummy *self = TEST_DUMMY (object);

  switch (prop_id) {
  case PROP_NAME:
    g_clear_pointer (&self->name, g_free);
    self->name = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
test_dummy_class_init (TestDummyClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = test_dummy_finalize;
  object_class->get_property = test_dummy_get_property;
  object_class->set_property = test_dummy_set_property;

  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "name", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
test_register_dummy (WpCore * core, const gchar * name)
{
  wp_core_register_object (core,
      g_object_new (test_dummy_get_type (), "name", name, NULL));
}

typedef struct {
  WpBaseTestFixture base;
  WpObjectManager *om;
} TestFixture;

static void
test_om_setup (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_CLIENT_CORE);
}

static void
test_om_teardown (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_teardown (&self->base);
}

static void
test_om_interest_on_pw_props (TestFixture *f, gconstpointer user_data)
{
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpObjectManager) om = NULL;

  /* load modules on the server side */
  {
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&f->base.server);

    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), ==, 0);
    if (!test_is_spa_lib_installed (&f->base, "audiotestsrc")) {
      g_test_skip ("The pipewire audiotestsrc factory was not found");
      return;
    }
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-adapter", NULL, NULL));
  }

  /* export node on the client core */
  node = wp_node_new_from_factory (f->base.client_core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", "Test Source",
          "test.answer", "42",
          NULL));
  g_assert_nonnull (node);

  wp_object_activate (WP_OBJECT (node), WP_OBJECT_FEATURES_ALL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* ensure the base core is in sync */
  wp_core_sync (f->base.core, NULL, (GAsyncReadyCallback) test_core_done_cb, f);
  g_main_loop_run (f->base.loop);

  /* request that node from the base core */
  om = wp_object_manager_new ();
  wp_object_manager_add_interest (om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "node.name", "=s", "Test Source",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "test.answer", "=s", "42",
      NULL);
  test_ensure_object_manager_is_installed (om, f->base.core, f->base.loop);

  g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 1);
  g_clear_object (&om);

  /* request "test.answer" to be absent... this will not match */
  om = wp_object_manager_new ();
  wp_object_manager_add_interest (om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "node.name", "=s", "Test Source",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "test.answer", "-",
      NULL);
  test_ensure_object_manager_is_installed (om, f->base.core, f->base.loop);

  g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 0);
}

static void
test_om_iterate_remove (TestFixture *f, gconstpointer user_data)
{
  g_autoptr (WpObjectManager) om = NULL;

  test_register_dummy (f->base.core, "4321");
  test_register_dummy (f->base.core, "2345");
  test_register_dummy (f->base.core, "1234");
  test_register_dummy (f->base.core, "1234");

  om = wp_object_manager_new ();
  wp_object_manager_add_interest (om, test_dummy_get_type (), NULL);
  test_ensure_object_manager_is_installed (om, f->base.core, f->base.loop);

  g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 4);

  /* remove half of the objects while iterating the object manager that
     contains them; the iteration must not be disturbed by the removals */
  {
    g_autoptr (WpIterator) it = wp_object_manager_new_iterator (om);
    g_auto (GValue) value = G_VALUE_INIT;
    while (wp_iterator_next (it, &value)) {
      TestDummy *dummy = g_value_get_object (&value);
      g_assert_true (TEST_IS_DUMMY (dummy));
      if (!g_strcmp0 (dummy->name, "1234"))
        wp_core_remove_object (f->base.core, dummy);
      g_value_unset (&value);
    }
  }

  g_assert_cmpuint (wp_object_manager_get_n_objects (om), ==, 2);
  g_assert_null (wp_object_manager_lookup (om, test_dummy_get_type (),
      WP_CONSTRAINT_TYPE_G_PROPERTY, "name", "=s", "1234", NULL));
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/om/interest-on-pw-props", TestFixture, NULL,
      test_om_setup, test_om_interest_on_pw_props, test_om_teardown);
  g_test_add ("/wp/om/iterate-remove", TestFixture, NULL,
      test_om_setup, test_om_iterate_remove, test_om_teardown);

  return g_test_run ();
}
