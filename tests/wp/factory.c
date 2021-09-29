/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
  WpObjectManager *om;
} TestFixture;

static void
test_factory_setup (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, 0);
  self->om = wp_object_manager_new ();
}

static void
test_factory_teardown (TestFixture *self, gconstpointer user_data)
{
  g_clear_object (&self->om);
  wp_base_test_fixture_teardown (&self->base);
}

static void
test_factory_enumeration_object_added (WpObjectManager *om,
    WpFactory *factory, TestFixture *fixture)
{
  g_autoptr (WpProperties) properties =
    wp_global_proxy_get_global_properties(WP_GLOBAL_PROXY(factory));
  const gchar* name = wp_properties_get (properties, PW_KEY_FACTORY_NAME);
  g_assert_nonnull(name);
  g_debug("factory name=%s", name);

  /* among all the pw factory objects look for client-node-factory object */
  if (!g_strcmp0(name, "client-node"))
    g_main_loop_quit(fixture->base.loop);
}

static void
test_factory_enumeration (TestFixture *self, gconstpointer user_data)
{
  g_signal_connect (self->om, "object_added",
      (GCallback) test_factory_enumeration_object_added, self);

  wp_object_manager_add_interest(self->om, WP_TYPE_FACTORY, NULL);
  wp_core_install_object_manager(self->base.core, self->om);
  g_main_loop_run(self->base.loop);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/factory/enumeration", TestFixture, NULL,
      test_factory_setup, test_factory_enumeration, test_factory_teardown);

  return g_test_run ();
}
