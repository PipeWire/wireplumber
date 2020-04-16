/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

G_DEFINE_QUARK (test-domain, test_domain)

enum {
  STEP_1 = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_2,
  STEP_EXPORT,
};

struct _TestSiDummy
{
  WpSessionItem parent;
  gboolean fail;
  gboolean step_1_done;
  gboolean step_2_done;
  gboolean step_export_done;
  gboolean activate_rollback_done;
  gboolean export_rollback_done;
};

G_DECLARE_FINAL_TYPE (TestSiDummy, si_dummy, TEST, SI_DUMMY, WpSessionItem)
G_DEFINE_TYPE (TestSiDummy, si_dummy, WP_TYPE_SESSION_ITEM)

static void
si_dummy_init (TestSiDummy * self)
{
}

static GVariant *
si_dummy_get_configuration (WpSessionItem * item)
{
  TestSiDummy *self = TEST_SI_DUMMY (item);
  GVariantBuilder b;
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "fail", g_variant_new_boolean (self->fail));
  return g_variant_builder_end (&b);
}

static gboolean
si_dummy_configure (WpSessionItem * item, GVariant * args)
{
  TestSiDummy *self = TEST_SI_DUMMY (item);

  if (wp_session_item_get_flags (item) & (WP_SI_FLAG_ACTIVATING | WP_SI_FLAG_ACTIVE))
    return FALSE;

  g_variant_lookup (args, "fail", "b", &self->fail);
  wp_session_item_set_flag (item, WP_SI_FLAG_CONFIGURED);

  return TRUE;
}

static guint
si_dummy_activate_get_next_step (WpSessionItem * item,
     WpTransition * transition, guint step)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      return STEP_1;
    case STEP_1:
      return STEP_2;
    case STEP_2:
      return WP_TRANSITION_STEP_NONE;
    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static gboolean
si_dummy_step_1 (gpointer data)
{
  WpTransition *transition = data;
  g_assert_true (WP_IS_TRANSITION (transition));

  TestSiDummy *self = wp_transition_get_source_object (transition);
  g_assert_true (TEST_IS_SI_DUMMY (self));

  self->step_1_done = TRUE;

  if (self->fail)
    wp_transition_return_error (transition,
        g_error_new (test_domain_quark (), 0, "error"));
  else
    wp_transition_advance (transition);

  return G_SOURCE_REMOVE;
}

static void
si_dummy_activate_execute_step (WpSessionItem * item, WpTransition * transition,
    guint step)
{
  TestSiDummy *self = TEST_SI_DUMMY (item);

  switch (step) {
    case STEP_1:
      /* execute async */
      g_idle_add (si_dummy_step_1, transition);
      break;

    case STEP_2:
      /* execute sync */
      self->step_2_done = TRUE;
      wp_transition_advance (transition);
      break;

    default:
      g_assert_not_reached ();
  }
}

static void
si_dummy_activate_rollback (WpSessionItem * item)
{
  TestSiDummy *self = TEST_SI_DUMMY (item);

  self->activate_rollback_done = TRUE;
  self->step_1_done = FALSE;
  self->step_2_done = FALSE;
}

static guint
si_dummy_export_get_next_step (WpSessionItem * item,
     WpTransition * transition, guint step)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      return STEP_EXPORT;
    case STEP_EXPORT:
      return WP_TRANSITION_STEP_NONE;
    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static gboolean
si_dummy_step_export (gpointer data)
{
  WpTransition *transition = data;
  g_assert_true (WP_IS_TRANSITION (transition));

  TestSiDummy *self = wp_transition_get_source_object (transition);
  g_assert_true (TEST_IS_SI_DUMMY (self));

  self->step_export_done = TRUE;

  if (self->fail)
    wp_transition_return_error (transition,
        g_error_new (test_domain_quark (), 1, "error"));
  else
    wp_transition_advance (transition);

  return G_SOURCE_REMOVE;
}

static void
si_dummy_export_execute_step (WpSessionItem * item, WpTransition * transition,
    guint step)
{
  switch (step) {
    case STEP_EXPORT:
      g_idle_add (si_dummy_step_export, transition);
      break;

    default:
      g_assert_not_reached ();
  }
}

static void
si_dummy_export_rollback (WpSessionItem * item)
{
  TestSiDummy *self = TEST_SI_DUMMY (item);

  self->export_rollback_done = TRUE;
  self->step_export_done = FALSE;
}

static void
si_dummy_class_init (TestSiDummyClass * klass)
{
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  si_class->configure = si_dummy_configure;
  si_class->get_configuration = si_dummy_get_configuration;
  si_class->activate_get_next_step = si_dummy_activate_get_next_step;
  si_class->activate_execute_step = si_dummy_activate_execute_step;
  si_class->activate_rollback = si_dummy_activate_rollback;
  si_class->export_get_next_step = si_dummy_export_get_next_step;
  si_class->export_execute_step = si_dummy_export_execute_step;
  si_class->export_rollback = si_dummy_export_rollback;
}

static void
expect_flags (WpSessionItem * item, WpSiFlags flags, WpSiFlags *signalled_flags)
{
  *signalled_flags = flags;
}

static void
test_flags (void)
{
  g_autoptr (WpSessionItem) item = NULL;
  WpSiFlags signalled_flags = 0;

  item = g_object_new (si_dummy_get_type (), NULL);
  g_assert_cmpint (wp_session_item_get_flags (item), ==, 0);

  g_signal_connect (item, "flags-changed", G_CALLBACK (expect_flags),
      &signalled_flags);
  wp_session_item_set_flag (item, WP_SI_FLAG_CUSTOM_START);
  g_assert_cmpint (wp_session_item_get_flags (item), ==, WP_SI_FLAG_CUSTOM_START);
  g_assert_cmpint (signalled_flags, ==, WP_SI_FLAG_CUSTOM_START);

  /* internal flags cannot be set */
  for (gint i = 0; i < 8; i++) {
    signalled_flags = 0;
    wp_session_item_set_flag (item, 1 << i);
    g_assert_cmpint (wp_session_item_get_flags (item), ==, WP_SI_FLAG_CUSTOM_START);
    g_assert_cmpint (signalled_flags, ==, 0);
  }

  signalled_flags = WP_SI_FLAG_CUSTOM_START;
  wp_session_item_clear_flag (item, WP_SI_FLAG_CUSTOM_START);
  g_assert_cmpint (wp_session_item_get_flags (item), ==, 0);
  g_assert_cmpint (signalled_flags, ==, 0);
}

static void
test_configuration (void)
{
  g_autoptr (WpSessionItem) item = NULL;
  g_autoptr (GVariant) v = NULL;
  WpSiFlags signalled_flags = 0;
  gboolean fail = FALSE;
  GVariantBuilder b;

  item = g_object_new (si_dummy_get_type (), NULL);
  g_signal_connect (item, "flags-changed", G_CALLBACK (expect_flags),
      &signalled_flags);

  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "fail", g_variant_new_boolean (TRUE));
  g_assert_true (wp_session_item_configure (item, g_variant_builder_end (&b)));

  g_assert_cmpint (wp_session_item_get_flags (item), ==, WP_SI_FLAG_CONFIGURED);
  g_assert_cmpint (signalled_flags, ==, WP_SI_FLAG_CONFIGURED);

  v = wp_session_item_get_configuration (item);
  g_assert_nonnull (v);
  g_assert_true (g_variant_is_of_type (v, G_VARIANT_TYPE_VARDICT));
  g_assert_true (g_variant_lookup (v, "fail", "b", &fail));
  g_assert_true (fail);
}

static void
expect_activate_success (WpSessionItem * item, GAsyncResult * res, gpointer data)
{
  GMainLoop *loop = data;
  g_autoptr (GError) error = NULL;

  g_assert_true (TEST_IS_SI_DUMMY (item));
  g_assert_true (g_async_result_is_tagged (res, wp_session_item_activate));
  g_assert_true (wp_session_item_activate_finish (item, res, &error));
  g_assert_no_error (error);

  g_main_loop_quit (loop);
}

static void
test_activation (void)
{
  g_autoptr (WpSessionItem) item = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  WpSiFlags signalled_flags = 0;
  TestSiDummy *dummy;

  loop = g_main_loop_new (NULL, FALSE);
  item = g_object_new (si_dummy_get_type (), NULL);
  dummy = TEST_SI_DUMMY (item);
  g_signal_connect (item, "flags-changed", G_CALLBACK (expect_flags),
      &signalled_flags);

  wp_session_item_activate (item,
      (GAsyncReadyCallback) expect_activate_success, loop);

  g_assert_cmpint (wp_session_item_get_flags (item), ==, WP_SI_FLAG_ACTIVATING);
  g_assert_cmpint (signalled_flags, ==, WP_SI_FLAG_ACTIVATING);

  g_main_loop_run (loop);

  g_assert_cmpint (wp_session_item_get_flags (item), ==, WP_SI_FLAG_ACTIVE);
  g_assert_cmpint (signalled_flags, ==, WP_SI_FLAG_ACTIVE);
  g_assert_true (dummy->step_1_done);
  g_assert_true (dummy->step_2_done);
  g_assert_false (dummy->activate_rollback_done);

  wp_session_item_deactivate (item);

  g_assert_cmpint (wp_session_item_get_flags (item), ==, 0);
  g_assert_cmpint (signalled_flags, ==, 0);
  g_assert_false (dummy->step_1_done);
  g_assert_false (dummy->step_2_done);
  g_assert_true (dummy->activate_rollback_done);
}

static void
expect_activate_failure (WpSessionItem * item, GAsyncResult * res, gpointer data)
{
  GMainLoop *loop = data;
  g_autoptr (GError) error = NULL;

  g_assert_true (TEST_IS_SI_DUMMY (item));
  g_assert_true (g_async_result_is_tagged (res, wp_session_item_activate));
  g_assert_false (wp_session_item_activate_finish (item, res, &error));
  g_assert_error (error, test_domain_quark (), 0);

  g_main_loop_quit (loop);
}

static void
test_activation_error (void)
{
  g_autoptr (WpSessionItem) item = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  WpSiFlags signalled_flags = 0;
  TestSiDummy *dummy;
  GVariantBuilder b;

  loop = g_main_loop_new (NULL, FALSE);
  item = g_object_new (si_dummy_get_type (), NULL);
  dummy = TEST_SI_DUMMY (item);
  g_signal_connect (item, "flags-changed", G_CALLBACK (expect_flags),
      &signalled_flags);

  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "fail", g_variant_new_boolean (TRUE));
  g_assert_true (wp_session_item_configure (item, g_variant_builder_end (&b)));

  g_assert_cmpint (wp_session_item_get_flags (item), ==, WP_SI_FLAG_CONFIGURED);
  g_assert_cmpint (signalled_flags, ==, WP_SI_FLAG_CONFIGURED);

  wp_session_item_activate (item,
      (GAsyncReadyCallback) expect_activate_failure, loop);

  g_assert_cmpint (wp_session_item_get_flags (item), ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_ACTIVATING);
  g_assert_cmpint (signalled_flags, ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_ACTIVATING);

  g_main_loop_run (loop);

  g_assert_cmpint (wp_session_item_get_flags (item), ==,
      WP_SI_FLAG_ACTIVATE_ERROR | WP_SI_FLAG_CONFIGURED);
  g_assert_cmpint (signalled_flags, ==,
      WP_SI_FLAG_ACTIVATE_ERROR | WP_SI_FLAG_CONFIGURED);
  g_assert_false (dummy->step_1_done);
  g_assert_false (dummy->step_2_done);
  g_assert_true (dummy->activate_rollback_done);

  /* deactivate should not call activate_rollback,
     it should only clear the error flag */
  dummy->activate_rollback_done = FALSE;
  wp_session_item_deactivate (item);

  g_assert_cmpint (wp_session_item_get_flags (item), ==, WP_SI_FLAG_CONFIGURED);
  g_assert_cmpint (signalled_flags, ==, WP_SI_FLAG_CONFIGURED);
  g_assert_false (dummy->step_1_done);
  g_assert_false (dummy->step_2_done);
  g_assert_false (dummy->activate_rollback_done);
}

static void
expect_export_success (WpSessionItem * item, GAsyncResult * res, gpointer data)
{
  GMainLoop *loop = data;
  g_autoptr (GError) error = NULL;

  g_assert_true (TEST_IS_SI_DUMMY (item));
  g_assert_true (g_async_result_is_tagged (res, wp_session_item_export));
  g_assert_true (wp_session_item_export_finish (item, res, &error));
  g_assert_no_error (error);

  g_main_loop_quit (loop);
}

static void
test_export (void)
{
  g_autoptr (WpSessionItem) item = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpSession) session = NULL;
  g_autoptr (WpSession) assoc_session = NULL;
  WpSiFlags signalled_flags = 0;
  TestSiDummy *dummy;

  loop = g_main_loop_new (NULL, FALSE);
  core = wp_core_new (NULL, NULL);
  session = (WpSession *) wp_impl_session_new (core);
  item = g_object_new (si_dummy_get_type (), NULL);
  dummy = TEST_SI_DUMMY (item);
  g_signal_connect (item, "flags-changed", G_CALLBACK (expect_flags),
      &signalled_flags);

  wp_session_item_export (item, session,
      (GAsyncReadyCallback) expect_export_success, loop);

  g_assert_cmpint (wp_session_item_get_flags (item), ==, WP_SI_FLAG_EXPORTING);
  g_assert_cmpint (signalled_flags, ==, WP_SI_FLAG_EXPORTING);

  g_main_loop_run (loop);

  g_assert_cmpint (wp_session_item_get_flags (item), ==, WP_SI_FLAG_EXPORTED);
  g_assert_cmpint (signalled_flags, ==, WP_SI_FLAG_EXPORTED);
  g_assert_true (dummy->step_export_done);
  g_assert_false (dummy->export_rollback_done);

  assoc_session = wp_session_item_get_associated_proxy (item, WP_TYPE_SESSION);
  g_assert_nonnull (assoc_session);
  g_assert_true (assoc_session == session);

  wp_session_item_unexport (item);

  g_assert_cmpint (wp_session_item_get_flags (item), ==, 0);
  g_assert_cmpint (signalled_flags, ==, 0);
  g_assert_false (dummy->step_export_done);
  g_assert_true (dummy->export_rollback_done);
}

static void
expect_export_failure (WpSessionItem * item, GAsyncResult * res, gpointer data)
{
  GMainLoop *loop = data;
  g_autoptr (GError) error = NULL;

  g_assert_true (TEST_IS_SI_DUMMY (item));
  g_assert_true (g_async_result_is_tagged (res, wp_session_item_export));
  g_assert_false (wp_session_item_export_finish (item, res, &error));
  g_assert_error (error, test_domain_quark (), 1);

  g_main_loop_quit (loop);
}

static void
test_export_error (void)
{
  g_autoptr (WpSessionItem) item = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpSession) session = NULL;
  WpSiFlags signalled_flags = 0;
  TestSiDummy *dummy;
  GVariantBuilder b;

  loop = g_main_loop_new (NULL, FALSE);
  core = wp_core_new (NULL, NULL);
  session = (WpSession *) wp_impl_session_new (core);
  item = g_object_new (si_dummy_get_type (), NULL);
  dummy = TEST_SI_DUMMY (item);
  g_signal_connect (item, "flags-changed", G_CALLBACK (expect_flags),
      &signalled_flags);

  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "fail", g_variant_new_boolean (TRUE));
  g_assert_true (wp_session_item_configure (item, g_variant_builder_end (&b)));

  g_assert_cmpint (wp_session_item_get_flags (item), ==, WP_SI_FLAG_CONFIGURED);
  g_assert_cmpint (signalled_flags, ==, WP_SI_FLAG_CONFIGURED);

  wp_session_item_export (item, session,
      (GAsyncReadyCallback) expect_export_failure, loop);

  g_assert_cmpint (wp_session_item_get_flags (item), ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_EXPORTING);
  g_assert_cmpint (signalled_flags, ==,
      WP_SI_FLAG_CONFIGURED | WP_SI_FLAG_EXPORTING);

  g_main_loop_run (loop);

  g_assert_cmpint (wp_session_item_get_flags (item), ==,
      WP_SI_FLAG_EXPORT_ERROR | WP_SI_FLAG_CONFIGURED);
  g_assert_cmpint (signalled_flags, ==,
      WP_SI_FLAG_EXPORT_ERROR | WP_SI_FLAG_CONFIGURED);
  g_assert_false (dummy->step_export_done);
  g_assert_true (dummy->export_rollback_done);

  /* unexport should not call export_rollback,
     it should only clear the error flag */
  dummy->export_rollback_done = FALSE;
  wp_session_item_unexport (item);

  g_assert_cmpint (wp_session_item_get_flags (item), ==, WP_SI_FLAG_CONFIGURED);
  g_assert_cmpint (signalled_flags, ==, WP_SI_FLAG_CONFIGURED);
  g_assert_false (dummy->step_export_done);
  g_assert_false (dummy->export_rollback_done);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add_func ("/wp/session-item/flags", test_flags);
  g_test_add_func ("/wp/session-item/configuration", test_configuration);
  g_test_add_func ("/wp/session-item/activation", test_activation);
  g_test_add_func ("/wp/session-item/activation-error", test_activation_error);
  g_test_add_func ("/wp/session-item/export", test_export);
  g_test_add_func ("/wp/session-item/export-error", test_export_error);

  return g_test_run ();
}
