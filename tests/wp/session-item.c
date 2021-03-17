/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include <wp/wp.h>

G_DEFINE_QUARK (test-domain, test_domain)

struct _TestSiDummy
{
  WpSessionItem parent;
  gboolean fail;
  WpSession *session;
  gboolean activate_done;
  gboolean export_done;
};

G_DECLARE_FINAL_TYPE (TestSiDummy, si_dummy, TEST, SI_DUMMY, WpSessionItem)
G_DEFINE_TYPE (TestSiDummy, si_dummy, WP_TYPE_SESSION_ITEM)

static void
si_dummy_init (TestSiDummy * self)
{
}

static void
si_dummy_reset (WpSessionItem * item)
{
  TestSiDummy *self = TEST_SI_DUMMY (item);

  /* deactivate first */
  wp_object_deactivate (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED);

  /* reset */
  self->fail = FALSE;
  g_clear_object (&self->session);

  WP_SESSION_ITEM_CLASS (si_dummy_parent_class)->reset (item);
}

static gboolean
si_dummy_configure (WpSessionItem * item, WpProperties * props)
{
  TestSiDummy *self = TEST_SI_DUMMY (item);
  WpSession *session = NULL;
  const gchar *str = NULL;

  /* reset previous config */
  si_dummy_reset (item);

  str = wp_properties_get (props, "fail");
  if (!str || sscanf(str, "%u", &self->fail) != 1)
    return FALSE;

  /* session is optional (only needed if we want to export) */
  str = wp_properties_get (props, "session");
  if (str && (sscanf(str, "%p", &session) != 1 || !WP_IS_SESSION (session)))
    return FALSE;

  if (session)
    self->session = g_object_ref (session);

  wp_session_item_set_properties (WP_SESSION_ITEM (self), props);
  return TRUE;
}

static gpointer
si_dummy_get_associated_proxy (WpSessionItem * item, GType proxy_type)
{
  TestSiDummy *self = TEST_SI_DUMMY (item);

  if (proxy_type == WP_TYPE_SESSION)
    return self->session ? g_object_ref (self->session) : NULL;

  return NULL;
}

static void
si_dummy_disable_active (WpSessionItem *si)
{
  TestSiDummy *self = TEST_SI_DUMMY (si);

  self->activate_done = FALSE;
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
}

static void
si_dummy_disable_exported (WpSessionItem *si)
{
  TestSiDummy *self = TEST_SI_DUMMY (si);

  self->export_done = FALSE;
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_EXPORTED);
}

static gboolean
si_dummy_step_activate (gpointer data)
{
  WpTransition *transition = data;
  g_assert_true (WP_IS_TRANSITION (transition));

  TestSiDummy *self = wp_transition_get_source_object (transition);
  g_assert_true (TEST_IS_SI_DUMMY (self));

  if (self->fail) {
    wp_transition_return_error (transition,
        g_error_new (test_domain_quark (), 0, "error"));
  } else {
    self->activate_done = TRUE;
    wp_object_update_features (WP_OBJECT (self),
        WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
  }

  return G_SOURCE_REMOVE;
}

static void
si_dummy_enable_active (WpSessionItem *si, WpTransition *transition)
{
  g_idle_add (si_dummy_step_activate, transition);
}

static gboolean
si_dummy_step_export (gpointer data)
{
  WpTransition *transition = data;
  g_assert_true (WP_IS_TRANSITION (transition));

  TestSiDummy *self = wp_transition_get_source_object (transition);
  g_assert_true (TEST_IS_SI_DUMMY (self));

  if (self->fail) {
    wp_transition_return_error (transition,
        g_error_new (test_domain_quark (), 0, "error"));
  } else {
    self->export_done = TRUE;
    wp_object_update_features (WP_OBJECT (self),
        WP_SESSION_ITEM_FEATURE_EXPORTED, 0);
  }

  return G_SOURCE_REMOVE;
}

static void
si_dummy_enable_exported (WpSessionItem *si, WpTransition *transition)
{
  g_idle_add (si_dummy_step_export, transition);
}

static void
si_dummy_class_init (TestSiDummyClass * klass)
{
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  si_class->reset = si_dummy_reset;
  si_class->configure = si_dummy_configure;
  si_class->get_associated_proxy = si_dummy_get_associated_proxy;
  si_class->disable_active = si_dummy_disable_active;
  si_class->disable_exported = si_dummy_disable_exported;
  si_class->enable_active = si_dummy_enable_active;
  si_class->enable_exported = si_dummy_enable_exported;
}

static void
expect_activate_success (WpObject * object, GAsyncResult * res, gpointer data)
{
  GMainLoop *loop = data;
  g_autoptr (GError) error = NULL;
  g_assert_true (TEST_IS_SI_DUMMY (object));
  g_assert_true (wp_object_activate_finish (object, res, &error));
  g_assert_no_error (error);
  g_main_loop_quit (loop);
}

static void
expect_activate_failure (WpObject * object, GAsyncResult * res, gpointer data)
{
  GMainLoop *loop = data;
  g_autoptr (GError) error = NULL;
  g_assert_true (TEST_IS_SI_DUMMY (object));
  g_assert_false (wp_object_activate_finish (object, res, &error));
  g_assert_error (error, test_domain_quark (), 0);
  g_main_loop_quit (loop);
}

static void
test_configuration (void)
{
  g_autoptr (WpCore) core = wp_core_new (NULL, NULL);
  g_autoptr (WpSessionItem) item = NULL;
  TestSiDummy *dummy;

  item = g_object_new (si_dummy_get_type (), "core", core, NULL);
  dummy = TEST_SI_DUMMY (item);

  {
    g_autoptr (WpProperties) p = wp_properties_new_empty ();
    wp_properties_setf (p, "fail", "%u", TRUE);
    g_assert_true (wp_session_item_configure (item, g_steal_pointer (&p)));
    g_assert_true (wp_session_item_is_configured (item));
    g_assert_true (dummy->fail);
  }

  {
    g_autoptr (WpProperties) p = wp_session_item_get_properties (item);
    g_assert_nonnull (p);
    const gchar * str = wp_properties_get (p, "fail");
    gboolean fail = FALSE;
    g_assert_nonnull (str);
    g_assert_true (sscanf(str, "%u", &fail) == 1);
    g_assert_true (fail);
  }
}

static void
test_activation (void)
{
  g_autoptr (WpCore) core = wp_core_new (NULL, NULL);
  g_autoptr (WpSessionItem) item = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  TestSiDummy *dummy;

  loop = g_main_loop_new (NULL, FALSE);
  item = g_object_new (si_dummy_get_type (), "core", core, NULL);
  dummy = TEST_SI_DUMMY (item);

  {
    g_autoptr (WpProperties) p = wp_properties_new_empty ();
    wp_properties_setf (p, "fail", "%u", FALSE);
    g_assert_true (wp_session_item_configure (item, g_steal_pointer (&p)));
    g_assert_true (wp_session_item_is_configured (item));
  }

  wp_object_activate (WP_OBJECT (item), WP_SESSION_ITEM_FEATURE_ACTIVE,
      NULL, (GAsyncReadyCallback) expect_activate_success, loop);
  g_main_loop_run (loop);
  g_assert_cmpint (wp_object_get_active_features (WP_OBJECT (item)), ==,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
  g_assert_true (dummy->activate_done);

  wp_object_deactivate (WP_OBJECT (item), WP_SESSION_ITEM_FEATURE_ACTIVE);
  g_assert_cmpint (wp_object_get_active_features (WP_OBJECT (item)), ==, 0);
  g_assert_false (dummy->activate_done);
}

static void
test_activation_error (void)
{
  g_autoptr (WpSessionItem) item = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  g_autoptr (WpCore) core = NULL;
  TestSiDummy *dummy;

  loop = g_main_loop_new (NULL, FALSE);
  core = wp_core_new (NULL, NULL);
  item = g_object_new (si_dummy_get_type (), "core", core, NULL);
  dummy = TEST_SI_DUMMY (item);

  {
    g_autoptr (WpProperties) p = wp_properties_new_empty ();
    wp_properties_setf (p, "fail", "%u", TRUE);
    g_assert_true (wp_session_item_configure (item, g_steal_pointer (&p)));
    g_assert_true (wp_session_item_is_configured (item));
  }

  wp_object_activate (WP_OBJECT (item), WP_SESSION_ITEM_FEATURE_ACTIVE,
      NULL, (GAsyncReadyCallback) expect_activate_failure, loop);
  g_main_loop_run (loop);
  g_assert_cmpint (wp_object_get_active_features (WP_OBJECT (item)), ==, 0);
  g_assert_false (dummy->activate_done);
  g_assert_true (dummy->fail);

  wp_object_deactivate (WP_OBJECT (item), WP_SESSION_ITEM_FEATURE_ACTIVE);
  g_assert_cmpint (wp_object_get_active_features (WP_OBJECT (item)), ==, 0);
  g_assert_true (dummy->fail);
  g_assert_false (dummy->activate_done);

  wp_session_item_reset (item);
  g_assert_false (dummy->fail);
  g_assert_false (dummy->activate_done);
  g_assert_false (wp_session_item_is_configured (item));
}

static void
test_export (void)
{
  g_autoptr (WpSessionItem) item = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpSession) session = NULL;
  g_autoptr (WpSession) assoc_session = NULL;
  TestSiDummy *dummy;

  loop = g_main_loop_new (NULL, FALSE);
  core = wp_core_new (NULL, NULL);
  session = (WpSession *) wp_impl_session_new (core);
  item = g_object_new (si_dummy_get_type (), "core", core, NULL);
  dummy = TEST_SI_DUMMY (item);

  {
    g_autoptr (WpProperties) p = wp_properties_new_empty ();
    wp_properties_setf (p, "fail", "%u", FALSE);
    wp_properties_setf (p, "session", "%p", session);
    g_assert_true (wp_session_item_configure (item, g_steal_pointer (&p)));
    g_assert_true (wp_session_item_is_configured (item));
  }

  wp_object_activate (WP_OBJECT (item),
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED,
      NULL, (GAsyncReadyCallback) expect_activate_success, loop);
  g_main_loop_run (loop);
  g_assert_cmpint (wp_object_get_active_features (WP_OBJECT (item)), ==,
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED);
  g_assert_true (dummy->activate_done);
  g_assert_true (dummy->export_done);

  assoc_session = wp_session_item_get_associated_proxy (item, WP_TYPE_SESSION);
  g_assert_nonnull (assoc_session);
  g_assert_true (assoc_session == session);

  wp_object_deactivate (WP_OBJECT (item), WP_SESSION_ITEM_FEATURE_EXPORTED);
  g_assert_cmpint (wp_object_get_active_features (WP_OBJECT (item)), ==,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
  g_assert_true (dummy->activate_done);
  g_assert_false (dummy->export_done);

  wp_session_item_reset (item);
  g_assert_false (dummy->activate_done);
  g_assert_false (wp_session_item_is_configured (item));
}

static void
test_export_error (void)
{
  g_autoptr (WpSessionItem) item = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpSession) session = NULL;
  TestSiDummy *dummy;

  loop = g_main_loop_new (NULL, FALSE);
  core = wp_core_new (NULL, NULL);
  session = (WpSession *) wp_impl_session_new (core);
  item = g_object_new (si_dummy_get_type (), "core", core, NULL);
  dummy = TEST_SI_DUMMY (item);

  {
    g_autoptr (WpProperties) p = wp_properties_new_empty ();
    wp_properties_setf (p, "fail", "%u", TRUE);
    wp_properties_setf (p, "session", "%p", session);
    g_assert_true (wp_session_item_configure (item, g_steal_pointer (&p)));
    g_assert_true (wp_session_item_is_configured (item));
  }

  wp_object_activate (WP_OBJECT (item),
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED,
      NULL, (GAsyncReadyCallback) expect_activate_failure, loop);
  g_main_loop_run (loop);
  g_assert_cmpint (wp_object_get_active_features (WP_OBJECT (item)), ==, 0);
  g_assert_false (dummy->activate_done);
  g_assert_false (dummy->export_done);

  wp_object_deactivate (WP_OBJECT (item), WP_SESSION_ITEM_FEATURE_EXPORTED);
  g_assert_cmpint (wp_object_get_active_features (WP_OBJECT (item)), ==, 0);
  g_assert_true (dummy->fail);
  g_assert_false (dummy->activate_done);
  g_assert_false (dummy->export_done);

  wp_session_item_reset (item);
  g_assert_false (dummy->fail);
  g_assert_false (dummy->activate_done);
  g_assert_false (dummy->export_done);
  g_assert_false (wp_session_item_is_configured (item));
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add_func ("/wp/session-item/configuration", test_configuration);
  g_test_add_func ("/wp/session-item/activation", test_activation);
  g_test_add_func ("/wp/session-item/activation-error", test_activation_error);
  g_test_add_func ("/wp/session-item/export", test_export);
  g_test_add_func ("/wp/session-item/export-error", test_export_error);

  return g_test_run ();
}
