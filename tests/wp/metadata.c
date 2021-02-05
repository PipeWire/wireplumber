/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;

  WpObjectManager *export_om;
  WpObjectManager *proxy_om;

  WpMetadata *impl_metadata;
  WpMetadata *proxy_metadata;

  gint n_events;

} TestFixture;

static void
test_metadata_setup (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_CLIENT_CORE);
  self->export_om = wp_object_manager_new ();
  self->proxy_om = wp_object_manager_new ();
}

static void
test_metadata_teardown (TestFixture *self, gconstpointer user_data)
{
  g_clear_object (&self->proxy_om);
  g_clear_object (&self->export_om);
  wp_base_test_fixture_teardown (&self->base);
}

static void
test_metadata_basic_exported_object_added (WpObjectManager *om,
    WpMetadata *metadata, TestFixture *fixture)
{
  g_debug ("exported object added");

  g_assert_true (WP_IS_IMPL_METADATA (metadata));

  g_assert_null (fixture->impl_metadata);
  fixture->impl_metadata = WP_METADATA (metadata);

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_metadata_basic_exported_object_removed (WpObjectManager *om,
    WpMetadata *metadata, TestFixture *fixture)
{
  g_debug ("exported object removed");

  g_assert_true (WP_IS_IMPL_METADATA (metadata));

  g_assert_nonnull (fixture->impl_metadata);
  fixture->impl_metadata = NULL;

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_metadata_basic_proxy_object_added (WpObjectManager *om,
    WpMetadata *metadata, TestFixture *fixture)
{
  g_debug ("proxy object added");

  g_assert_true (WP_IS_METADATA (metadata));

  g_assert_null (fixture->proxy_metadata);
  fixture->proxy_metadata = WP_METADATA (metadata);

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_metadata_basic_proxy_object_removed (WpObjectManager *om,
    WpMetadata *metadata, TestFixture *fixture)
{
  g_debug ("proxy object removed");

  g_assert_true (WP_IS_METADATA (metadata));

  g_assert_nonnull (fixture->proxy_metadata);
  fixture->proxy_metadata = NULL;

  if (++fixture->n_events == 2)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_metadata_basic_export_done (WpObject * metadata, GAsyncResult * res,
    TestFixture *fixture)
{
  g_autoptr (GError) error = NULL;

  g_debug ("export done");

  g_assert_true (wp_object_activate_finish (metadata, res, &error));
  g_assert_no_error (error);

  g_assert_true (WP_IS_IMPL_METADATA (metadata));

  if (++fixture->n_events == 3)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_metadata_basic_changed (WpMetadata *metadata, guint32 subject,
    const gchar *key, const gchar *type, const gchar *value,
    TestFixture *fixture)
{
  g_debug ("changed: %u %s", subject, key);

  g_assert_true (WP_IS_METADATA (metadata));
  g_assert_cmpuint (subject, !=, PW_ID_ANY);

  if (++fixture->n_events == 4)
    g_main_loop_quit (fixture->base.loop);
}

static void
test_metadata_basic (TestFixture *fixture, gconstpointer data)
{
  g_autoptr (WpMetadata) metadata = NULL;

  /* set up the export side */
  g_signal_connect (fixture->export_om, "object_added",
      (GCallback) test_metadata_basic_exported_object_added, fixture);
  g_signal_connect (fixture->export_om, "object-removed",
      (GCallback) test_metadata_basic_exported_object_removed, fixture);
  wp_object_manager_add_interest (fixture->export_om,
      WP_TYPE_IMPL_METADATA, NULL);
  wp_object_manager_request_object_features (fixture->export_om,
      WP_TYPE_IMPL_METADATA, WP_OBJECT_FEATURES_ALL);
  wp_core_install_object_manager (fixture->base.core, fixture->export_om);

  /* set up the proxy side */
  g_signal_connect (fixture->proxy_om, "object_added",
      (GCallback) test_metadata_basic_proxy_object_added, fixture);
  g_signal_connect (fixture->proxy_om, "object-removed",
      (GCallback) test_metadata_basic_proxy_object_removed, fixture);
  wp_object_manager_add_interest (fixture->proxy_om, WP_TYPE_METADATA, NULL);
  wp_object_manager_request_object_features (fixture->proxy_om,
      WP_TYPE_METADATA, WP_OBJECT_FEATURES_ALL);
  wp_core_install_object_manager (fixture->base.client_core, fixture->proxy_om);

  /* create metadata */
  metadata = WP_METADATA (wp_impl_metadata_new (fixture->base.core));
  wp_metadata_set (metadata, 0, "test-key", NULL, "test-value");
  wp_metadata_set (metadata, 15, "toast", "Spa:Int", "15");

  /* verify properties are set before export */
  {
    g_autoptr (WpIterator) iter = wp_metadata_new_iterator (metadata, PW_ID_ANY);
    g_auto (GValue) val = G_VALUE_INIT;
    guint subject = -1;
    const gchar *key = NULL, *type = NULL, *value = NULL;

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "test-key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "test-value");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 15);
    g_assert_cmpstr (key, ==, "toast");
    g_assert_cmpstr (type, ==, "Spa:Int");
    g_assert_cmpstr (value, ==, "15");
    g_value_unset (&val);

    g_assert_false (wp_iterator_next (iter, &val));
  }

  /* do export */
  wp_object_activate (WP_OBJECT (metadata), WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) test_metadata_basic_export_done, fixture);

  /* run until objects are created and features are cached */
  fixture->n_events = 0;
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 3);
  g_assert_nonnull (fixture->impl_metadata);
  g_assert_nonnull (fixture->proxy_metadata);
  g_assert_true (fixture->impl_metadata == metadata);

  /* test round 1: verify the values on the proxy */
  {
    g_autoptr (WpIterator) iter =
        wp_metadata_new_iterator (fixture->proxy_metadata, PW_ID_ANY);
    g_auto (GValue) val = G_VALUE_INIT;
    guint subject = -1;
    const gchar *key = NULL, *type = NULL, *value = NULL;

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "test-key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "test-value");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 15);
    g_assert_cmpstr (key, ==, "toast");
    g_assert_cmpstr (type, ==, "Spa:Int");
    g_assert_cmpstr (value, ==, "15");
    g_value_unset (&val);

    g_assert_false (wp_iterator_next (iter, &val));
  }

  /* setup change signals */
  g_signal_connect (fixture->proxy_metadata, "changed",
      (GCallback) test_metadata_basic_changed, fixture);
  g_signal_connect (metadata, "changed",
      (GCallback) test_metadata_basic_changed, fixture);

  /* change properties on the proxy */
  wp_metadata_set (fixture->proxy_metadata, 15, "toast", "Spa:Int", "20");
  wp_metadata_set (fixture->proxy_metadata, 0, "3rd.key", NULL, "3rd.value");

  /* run until the change is on both sides */
  fixture->n_events = 0;
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 4);

  /* test round 2: verify the value change on both sides */
  {
    g_autoptr (WpIterator) iter =
        wp_metadata_new_iterator (fixture->proxy_metadata, PW_ID_ANY);
    g_auto (GValue) val = G_VALUE_INIT;
    guint subject = -1;
    const gchar *key = NULL, *type = NULL, *value = NULL;

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "test-key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "test-value");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 15);
    g_assert_cmpstr (key, ==, "toast");
    g_assert_cmpstr (type, ==, "Spa:Int");
    g_assert_cmpstr (value, ==, "20");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "3rd.key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "3rd.value");
    g_value_unset (&val);

    g_assert_false (wp_iterator_next (iter, &val));
  }
  {
    g_autoptr (WpIterator) iter =
        wp_metadata_new_iterator (metadata, PW_ID_ANY);
    g_auto (GValue) val = G_VALUE_INIT;
    guint subject = -1;
    const gchar *key = NULL, *type = NULL, *value = NULL;

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "test-key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "test-value");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 15);
    g_assert_cmpstr (key, ==, "toast");
    g_assert_cmpstr (type, ==, "Spa:Int");
    g_assert_cmpstr (value, ==, "20");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "3rd.key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "3rd.value");
    g_value_unset (&val);

    g_assert_false (wp_iterator_next (iter, &val));
  }

  /* change properties on the exported */
  fixture->n_events = 0;
  wp_metadata_set (metadata, 0, "4th.key", NULL, "4th.value");
  wp_metadata_set (metadata, 0, "test-key", NULL, "new.value");

  /* run until the change is on both sides */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 4);

  /* test round 3: verify the value change on both sides */
  {
    g_autoptr (WpIterator) iter =
        wp_metadata_new_iterator (fixture->proxy_metadata, PW_ID_ANY);
    g_auto (GValue) val = G_VALUE_INIT;
    guint subject = -1;
    const gchar *key = NULL, *type = NULL, *value = NULL;

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "test-key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "new.value");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 15);
    g_assert_cmpstr (key, ==, "toast");
    g_assert_cmpstr (type, ==, "Spa:Int");
    g_assert_cmpstr (value, ==, "20");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "3rd.key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "3rd.value");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "4th.key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "4th.value");
    g_value_unset (&val);

    g_assert_false (wp_iterator_next (iter, &val));
  }
  {
    g_autoptr (WpIterator) iter =
        wp_metadata_new_iterator (metadata, PW_ID_ANY);
    g_auto (GValue) val = G_VALUE_INIT;
    guint subject = -1;
    const gchar *key = NULL, *type = NULL, *value = NULL;

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "test-key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "new.value");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 15);
    g_assert_cmpstr (key, ==, "toast");
    g_assert_cmpstr (type, ==, "Spa:Int");
    g_assert_cmpstr (value, ==, "20");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "3rd.key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "3rd.value");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "4th.key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "4th.value");
    g_value_unset (&val);

    g_assert_false (wp_iterator_next (iter, &val));
  }

  /* find with constraints */
  {
    g_autoptr (WpIterator) iter = wp_metadata_new_iterator (metadata, 0);
    g_auto (GValue) val = G_VALUE_INIT;
    guint subject = -1;
    const gchar *key = NULL, *type = NULL, *value = NULL;

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "test-key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "new.value");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "3rd.key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "3rd.value");
    g_value_unset (&val);

    g_assert_true (wp_iterator_next (iter, &val));
    wp_metadata_iterator_item_extract (&val, &subject, &key, &type, &value);
    g_assert_cmpint (subject, ==, 0);
    g_assert_cmpstr (key, ==, "4th.key");
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "4th.value");
    g_value_unset (&val);

    g_assert_false (wp_iterator_next (iter, &val));
  }
  {
    const gchar *value = NULL, *type = NULL;
    value = wp_metadata_find (metadata, 0, "3rd.key", &type);
    g_assert_cmpstr (type, ==, "string");
    g_assert_cmpstr (value, ==, "3rd.value");
  }

  /* destroy impl metadata */
  fixture->n_events = 0;
  g_clear_object (&metadata);

  /* run until objects are destroyed */
  g_main_loop_run (fixture->base.loop);
  g_assert_cmpint (fixture->n_events, ==, 2);
  g_assert_null (fixture->impl_metadata);
  g_assert_null (fixture->proxy_metadata);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/metadata/basic", TestFixture, NULL,
      test_metadata_setup, test_metadata_basic, test_metadata_teardown);

  return g_test_run ();
}
