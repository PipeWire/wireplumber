/* WirePlumber
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

enum {
  TEST_FEATURE_A = (1 << 0),
  TEST_FEATURE_B = (1 << 1),
};

enum {
  STEP_A = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_B,
};

/* An object with two features. A step only starts the activation of its
 * feature; the test completes it with wp_object_update_features() */
struct _WpTestObject
{
  WpObject parent;
  gboolean fail_b;
  GCancellable *cancel_on_fail;
  gboolean abort_on_deactivate;
  guint n_started;
};

G_DECLARE_FINAL_TYPE (WpTestObject, wp_test_object, WP, TEST_OBJECT, WpObject)
G_DEFINE_TYPE (WpTestObject, wp_test_object, WP_TYPE_OBJECT)

static void
wp_test_object_init (WpTestObject * self)
{
}

static WpObjectFeatures
wp_test_object_get_supported_features (WpObject * object)
{
  return TEST_FEATURE_A | TEST_FEATURE_B;
}

static guint
wp_test_object_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  return (missing & TEST_FEATURE_A) ? STEP_A : STEP_B;
}

static void
wp_test_object_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpTestObject *self = WP_TEST_OBJECT (object);

  switch (step) {
  case STEP_A:
    self->n_started++;
    break;
  case STEP_B:
    self->n_started++;
    if (self->fail_b && self->cancel_on_fail)
      g_cancellable_cancel (self->cancel_on_fail);
    if (self->fail_b)
      wp_transition_return_error (WP_TRANSITION (transition),
          g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
              "test failure"));
    break;
  case WP_TRANSITION_STEP_ERROR:
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_test_object_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpTestObject *self = WP_TEST_OBJECT (object);

  /* like WpStreamLink, which fails a pending activation when deactivated */
  if (self->abort_on_deactivate)
    wp_object_abort_activation (object, "test object deactivated");

  wp_object_update_features (object, 0, features);
}

static void
wp_test_object_class_init (WpTestObjectClass * klass)
{
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;

  wpobject_class->get_supported_features =
      wp_test_object_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_test_object_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_test_object_activate_execute_step;
  wpobject_class->deactivate = wp_test_object_deactivate;
}

typedef struct {
  WpBaseTestFixture base;
  WpTestObject *object;
} TestFixture;

typedef struct {
  guint n_calls;
  GError *error;
} Result;

static void
test_setup (TestFixture * self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, 0);
  self->object = g_object_new (wp_test_object_get_type (),
      "core", self->base.core, NULL);
}

static void
test_teardown (TestFixture * self, gconstpointer user_data)
{
  g_clear_object (&self->object);
  wp_base_test_fixture_teardown (&self->base);
}

static void
on_activated (WpObject * object, GAsyncResult * res, Result * result)
{
  result->n_calls++;
  wp_object_activate_finish (object, res, &result->error);
}

static void
result_clear (Result * result)
{
  g_clear_error (&result->error);
}

/* runs everything that is ready, up to the point where the object waits for
   the test to complete a step */
static void
iterate (TestFixture * self)
{
  while (g_main_context_iteration (self->base.context, FALSE));
}

static void
complete (TestFixture * self, WpObjectFeatures feature)
{
  wp_object_update_features (WP_OBJECT (self->object), feature, 0);
  iterate (self);
}

static void
test_activate (TestFixture * self, gconstpointer user_data)
{
  WpObject *object = WP_OBJECT (self->object);
  Result result = { 0 };

  wp_object_activate (object, TEST_FEATURE_A | TEST_FEATURE_B, NULL,
      (GAsyncReadyCallback) on_activated, &result);
  iterate (self);
  g_assert_cmpuint (self->object->n_started, ==, 1);
  complete (self, TEST_FEATURE_A);
  g_assert_cmpuint (self->object->n_started, ==, 2);
  g_assert_cmpuint (result.n_calls, ==, 0);
  complete (self, TEST_FEATURE_B);

  g_assert_cmpuint (result.n_calls, ==, 1);
  g_assert_no_error (result.error);
  g_assert_cmpuint (wp_object_get_active_features (object), ==,
      TEST_FEATURE_A | TEST_FEATURE_B);
}

/* the step that is running when the activation is cancelled still finishes;
   what the cancelled activation has activated must not stay active */
static void
test_cancel_running (TestFixture * self, gconstpointer user_data)
{
  WpObject *object = WP_OBJECT (self->object);
  g_autoptr (GCancellable) cancellable = g_cancellable_new ();
  Result result = { 0 };

  self->object->abort_on_deactivate = GPOINTER_TO_INT (user_data);

  /* A is active before the activation that gets cancelled */
  wp_object_activate (object, TEST_FEATURE_A, NULL,
      (GAsyncReadyCallback) on_activated, &result);
  iterate (self);
  complete (self, TEST_FEATURE_A);
  g_assert_cmpuint (result.n_calls, ==, 1);
  g_assert_no_error (result.error);

  wp_object_activate (object, TEST_FEATURE_A | TEST_FEATURE_B, cancellable,
      (GAsyncReadyCallback) on_activated, &result);
  iterate (self);
  g_assert_cmpuint (self->object->n_started, ==, 2);
  g_cancellable_cancel (cancellable);
  complete (self, TEST_FEATURE_B);

  g_assert_cmpuint (result.n_calls, ==, 2);
  g_assert_error (result.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_cmpuint (wp_object_get_active_features (object), ==,
      TEST_FEATURE_A);
  result_clear (&result);
}

/* an activation that is cancelled before it starts has not activated
   anything and must leave the active features alone */
static void
test_cancel_before_start (TestFixture * self, gconstpointer user_data)
{
  WpObject *object = WP_OBJECT (self->object);
  g_autoptr (GCancellable) cancellable = g_cancellable_new ();
  Result result = { 0 };

  wp_object_activate (object, TEST_FEATURE_A, NULL,
      (GAsyncReadyCallback) on_activated, &result);
  iterate (self);
  complete (self, TEST_FEATURE_A);
  g_assert_cmpuint (result.n_calls, ==, 1);
  g_assert_no_error (result.error);

  g_cancellable_cancel (cancellable);
  wp_object_activate (object, TEST_FEATURE_A | TEST_FEATURE_B, cancellable,
      (GAsyncReadyCallback) on_activated, &result);
  iterate (self);

  g_assert_cmpuint (result.n_calls, ==, 2);
  g_assert_error (result.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_cmpuint (self->object->n_started, ==, 1);
  g_assert_cmpuint (wp_object_get_active_features (object), ==,
      TEST_FEATURE_A);
  result_clear (&result);
}

/* same, for an activation that is cancelled while it waits in the queue
   behind the one that activates the feature */
static void
test_cancel_queued (TestFixture * self, gconstpointer user_data)
{
  WpObject *object = WP_OBJECT (self->object);
  g_autoptr (GCancellable) cancellable = g_cancellable_new ();
  Result first = { 0 }, second = { 0 };

  wp_object_activate (object, TEST_FEATURE_A, NULL,
      (GAsyncReadyCallback) on_activated, &first);
  wp_object_activate (object, TEST_FEATURE_A, cancellable,
      (GAsyncReadyCallback) on_activated, &second);
  g_cancellable_cancel (cancellable);
  iterate (self);
  complete (self, TEST_FEATURE_A);

  g_assert_cmpuint (first.n_calls, ==, 1);
  g_assert_no_error (first.error);
  g_assert_cmpuint (second.n_calls, ==, 1);
  g_assert_error (second.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_cmpuint (wp_object_get_active_features (object), ==,
      TEST_FEATURE_A);
  result_clear (&second);
}

/* an object that aborts its activation when it is deactivated: undoing the
   cancelled activation also fails the one queued behind it, once */
static void
test_cancel_running_queued (TestFixture * self, gconstpointer user_data)
{
  WpObject *object = WP_OBJECT (self->object);
  g_autoptr (GCancellable) cancellable = g_cancellable_new ();
  Result first = { 0 }, second = { 0 };

  self->object->abort_on_deactivate = TRUE;

  wp_object_activate (object, TEST_FEATURE_A, cancellable,
      (GAsyncReadyCallback) on_activated, &first);
  wp_object_activate (object, TEST_FEATURE_B, NULL,
      (GAsyncReadyCallback) on_activated, &second);
  iterate (self);
  g_assert_cmpuint (self->object->n_started, ==, 1);
  g_cancellable_cancel (cancellable);
  complete (self, TEST_FEATURE_A);

  g_assert_cmpuint (first.n_calls, ==, 1);
  g_assert_error (first.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_cmpuint (second.n_calls, ==, 1);
  g_assert_error (second.error, WP_DOMAIN_LIBRARY,
      WP_LIBRARY_ERROR_OPERATION_FAILED);
  g_assert_cmpuint (self->object->n_started, ==, 1);
  g_assert_cmpuint (wp_object_get_active_features (object), ==, 0);
  result_clear (&first);
  result_clear (&second);
}

/* what an activation that fails has activated must not stay active */
static void
test_error (TestFixture * self, gconstpointer user_data)
{
  WpObject *object = WP_OBJECT (self->object);
  g_autoptr (GCancellable) cancellable = g_cancellable_new ();
  Result result = { 0 };

  self->object->fail_b = TRUE;

  wp_object_activate (object, TEST_FEATURE_A | TEST_FEATURE_B, cancellable,
      (GAsyncReadyCallback) on_activated, &result);
  iterate (self);
  complete (self, TEST_FEATURE_A);

  g_assert_cmpuint (result.n_calls, ==, 1);
  g_assert_error (result.error, WP_DOMAIN_LIBRARY,
      WP_LIBRARY_ERROR_OPERATION_FAILED);
  g_assert_cmpuint (wp_object_get_active_features (object), ==, 0);
  result_clear (&result);
}

/* a feature that was active before an activation that fails stays active */
static void
test_error_keeps_active (TestFixture * self, gconstpointer user_data)
{
  WpObject *object = WP_OBJECT (self->object);
  Result result = { 0 };

  wp_object_activate (object, TEST_FEATURE_A, NULL,
      (GAsyncReadyCallback) on_activated, &result);
  iterate (self);
  complete (self, TEST_FEATURE_A);
  g_assert_cmpuint (result.n_calls, ==, 1);
  g_assert_no_error (result.error);

  self->object->fail_b = TRUE;
  wp_object_activate (object, TEST_FEATURE_A | TEST_FEATURE_B, NULL,
      (GAsyncReadyCallback) on_activated, &result);
  iterate (self);

  g_assert_cmpuint (result.n_calls, ==, 2);
  g_assert_error (result.error, WP_DOMAIN_LIBRARY,
      WP_LIBRARY_ERROR_OPERATION_FAILED);
  g_assert_cmpuint (wp_object_get_active_features (object), ==,
      TEST_FEATURE_A);
  result_clear (&result);
}

/* same as test_error, when the activation is cancelled while the step that
   fails is running: the caller gets that step's error, not a cancellation */
static void
test_error_cancelled (TestFixture * self, gconstpointer user_data)
{
  WpObject *object = WP_OBJECT (self->object);
  g_autoptr (GCancellable) cancellable = g_cancellable_new ();
  Result result = { 0 };

  self->object->fail_b = TRUE;
  self->object->cancel_on_fail = cancellable;

  wp_object_activate (object, TEST_FEATURE_A | TEST_FEATURE_B, cancellable,
      (GAsyncReadyCallback) on_activated, &result);
  iterate (self);
  complete (self, TEST_FEATURE_A);

  g_assert_true (g_cancellable_is_cancelled (cancellable));
  g_assert_cmpuint (result.n_calls, ==, 1);
  g_assert_error (result.error, WP_DOMAIN_LIBRARY,
      WP_LIBRARY_ERROR_OPERATION_FAILED);
  g_assert_cmpuint (wp_object_get_active_features (object), ==, 0);
  self->object->cancel_on_fail = NULL;
  result_clear (&result);
}

/* an activation can enable a feature that was not requested, as activating
   the info of an unbound proxy also binds it; a cancelled activation must
   not leave that feature active either */
static void
test_cancel_dependency (TestFixture * self, gconstpointer user_data)
{
  WpObject *object = WP_OBJECT (self->object);
  g_autoptr (GCancellable) cancellable = g_cancellable_new ();
  Result result = { 0 };

  wp_object_activate (object, TEST_FEATURE_B, cancellable,
      (GAsyncReadyCallback) on_activated, &result);
  iterate (self);
  g_assert_cmpuint (self->object->n_started, ==, 1);
  g_cancellable_cancel (cancellable);
  complete (self, TEST_FEATURE_A | TEST_FEATURE_B);

  g_assert_cmpuint (result.n_calls, ==, 1);
  g_assert_error (result.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_cmpuint (wp_object_get_active_features (object), ==, 0);
  result_clear (&result);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/object/activate", TestFixture, NULL,
      test_setup, test_activate, test_teardown);
  g_test_add ("/wp/object/cancel-running", TestFixture, GINT_TO_POINTER (FALSE),
      test_setup, test_cancel_running, test_teardown);
  g_test_add ("/wp/object/cancel-running-abort", TestFixture,
      GINT_TO_POINTER (TRUE),
      test_setup, test_cancel_running, test_teardown);
  g_test_add ("/wp/object/cancel-before-start", TestFixture, NULL,
      test_setup, test_cancel_before_start, test_teardown);
  g_test_add ("/wp/object/cancel-queued", TestFixture, NULL,
      test_setup, test_cancel_queued, test_teardown);
  g_test_add ("/wp/object/cancel-running-queued", TestFixture, NULL,
      test_setup, test_cancel_running_queued, test_teardown);
  g_test_add ("/wp/object/error", TestFixture, NULL,
      test_setup, test_error, test_teardown);
  g_test_add ("/wp/object/error-keeps-active", TestFixture, NULL,
      test_setup, test_error_keeps_active, test_teardown);
  g_test_add ("/wp/object/error-cancelled", TestFixture, NULL,
      test_setup, test_error_cancelled, test_teardown);
  g_test_add ("/wp/object/cancel-dependency", TestFixture, NULL,
      test_setup, test_cancel_dependency, test_teardown);

  return g_test_run ();
}
