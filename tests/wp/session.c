/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"
#include <pipewire/extensions/session-manager/keys.h>

typedef struct {
  WpBaseTestFixture base;

  WpObjectManager *export_om;
  WpObjectManager *proxy_om;

  WpImplSession *impl_session;
  WpProxy *proxy_session;

  gint n_events;

} TestSessionFixture;

static void
test_session_setup (TestSessionFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_CLIENT_CORE);
  self->export_om = wp_object_manager_new ();
  self->proxy_om = wp_object_manager_new ();
}

static void
test_session_teardown (TestSessionFixture *self, gconstpointer user_data)
{
  g_clear_object (&self->proxy_om);
  g_clear_object (&self->export_om);
  wp_base_test_fixture_teardown (&self->base);
}

static void
test_session_basic_exported_object_added (WpObjectManager *om,
    WpSession *session, TestSessionFixture *fixture)
{
  g_debug ("exported object added");

  g_assert_true (WP_IS_IMPL_SESSION (session));

  g_assert_null (fixture->impl_session);
  fixture->impl_session = WP_IMPL_SESSION (session);

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_session_basic_exported_object_removed (WpObjectManager *om,
    WpSession *session, TestSessionFixture *fixture)
{
  g_debug ("exported object removed");

  g_assert_true (WP_IS_IMPL_SESSION (session));

  g_assert_nonnull (fixture->impl_session);
  fixture->impl_session = NULL;

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_session_basic_proxy_object_added (WpObjectManager *om,
    WpSession *session, TestSessionFixture *fixture)
{
  g_debug ("proxy object added");

  g_assert_true (WP_IS_SESSION (session));

  g_assert_null (fixture->proxy_session);
  fixture->proxy_session = WP_PROXY (session);

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_session_basic_proxy_object_removed (WpObjectManager *om,
    WpSession *session, TestSessionFixture *fixture)
{
  g_debug ("proxy object removed");

  g_assert_true (WP_IS_SESSION (session));

  g_assert_nonnull (fixture->proxy_session);
  fixture->proxy_session = NULL;

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_session_basic_export_done (WpObject * session, GAsyncResult * res,
    TestSessionFixture *fixture)
{
  g_autoptr (GError) error = NULL;

  g_debug ("export done");

  g_assert_true (wp_object_activate_finish (session, res, &error));
  g_assert_no_error (error);

  g_assert_true (WP_IS_IMPL_SESSION (session));

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_session_basic_notify_properties (WpSession * session, GParamSpec * param,
    TestSessionFixture *fixture)
{
  g_debug ("properties changed: %s", G_OBJECT_TYPE_NAME (session));

  g_assert_true (WP_IS_SESSION (session));

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_session_basic (TestSessionFixture *fixture, gconstpointer data)
{
  g_autoptr (WpImplSession) session = NULL;

  /* set up the export side */
  g_signal_connect (fixture->export_om, "object-added",
      (GCallback) test_session_basic_exported_object_added, fixture);
  g_signal_connect (fixture->export_om, "object-removed",
      (GCallback) test_session_basic_exported_object_removed, fixture);
  wp_object_manager_add_interest (fixture->export_om,
      WP_TYPE_IMPL_SESSION, NULL);
  wp_object_manager_request_object_features (fixture->export_om,
      WP_TYPE_IMPL_SESSION, WP_OBJECT_FEATURES_ALL);
  wp_core_install_object_manager (fixture->base.core, fixture->export_om);

  /* set up the proxy side */
  g_signal_connect (fixture->proxy_om, "object-added",
      (GCallback) test_session_basic_proxy_object_added, fixture);
  g_signal_connect (fixture->proxy_om, "object-removed",
      (GCallback) test_session_basic_proxy_object_removed, fixture);
  wp_object_manager_add_interest (fixture->proxy_om, WP_TYPE_SESSION, NULL);
  wp_object_manager_request_object_features (fixture->proxy_om,
      WP_TYPE_SESSION, WP_OBJECT_FEATURES_ALL);
  wp_core_install_object_manager (fixture->base.client_core, fixture->proxy_om);

  /* create session */
  session = wp_impl_session_new (fixture->base.core);
  wp_impl_session_set_property (session, "test.property", "test-value");

  /* verify properties are set before export */
  {
    g_autoptr (WpProperties) props =
        wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (session));
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "test-value");
  }

  /* do export */
  wp_object_activate (WP_OBJECT (session), WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) test_session_basic_export_done, fixture);

  /* run until objects are created and features are cached */
  fixture->n_events = 0;
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 3);
  g_assert_nonnull (fixture->impl_session);
  g_assert_nonnull (fixture->proxy_session);
  g_assert_true (fixture->impl_session == session);

  /* test round 1: verify the values on the proxy */

  g_assert_cmphex (
      wp_object_get_active_features (WP_OBJECT (fixture->proxy_session)), ==,
      wp_object_get_supported_features (WP_OBJECT (fixture->proxy_session)));

  g_assert_cmpuint (wp_proxy_get_bound_id (fixture->proxy_session), ==,
      wp_proxy_get_bound_id (WP_PROXY (session)));

  {
    g_autoptr (WpProperties) props = wp_pipewire_object_get_properties (
        WP_PIPEWIRE_OBJECT (fixture->proxy_session));
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "test-value");
  }

  /* setup change signals */
  g_signal_connect (fixture->proxy_session, "notify::properties",
      (GCallback) test_session_basic_notify_properties, fixture);
  g_signal_connect (session, "notify::properties",
      (GCallback) test_session_basic_notify_properties, fixture);

  /* change a property on the exported */
  fixture->n_events = 0;
  wp_impl_session_set_property (session, "test.property", "changed-value");

  /* run until the change is on both sides */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 2);

  /* verify the property change on both sides */
  {
    g_autoptr (WpProperties) props =
        wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (session));
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "changed-value");
  }
  {
    g_autoptr (WpProperties) props = wp_pipewire_object_get_properties (
        WP_PIPEWIRE_OBJECT (fixture->proxy_session));
    g_assert_cmpstr (wp_properties_get (props, "test.property"), ==,
        "changed-value");
  }

  /* destroy impl session */
  fixture->n_events = 0;
  g_clear_object (&session);

  /* run until objects are destroyed */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 2);
  g_assert_null (fixture->impl_session);
  g_assert_null (fixture->proxy_session);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/session/basic", TestSessionFixture, NULL,
      test_session_setup, test_session_basic, test_session_teardown);

  return g_test_run ();
}
