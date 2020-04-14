/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

enum {
  STEP_FIRST = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_SECOND,
  STEP_THIRD,
  STEP_FINISH,
};

struct data
{
  gboolean destroyed;

  /* steps that appeared in get_next_step */
  guint sta[10];
  guint sta_i;

  /* steps executed */
  guint ste[10];
  guint ste_i;
};

struct _WpTestTransition
{
  WpTransition parent;
  gboolean step_third_wait;
  gboolean step_third_error;
};

G_DECLARE_FINAL_TYPE (WpTestTransition, wp_test_transition, WP, TEST_TRANSITION, WpTransition)
G_DEFINE_TYPE (WpTestTransition, wp_test_transition, WP_TYPE_TRANSITION)

static void
data_destroy (gpointer data)
{
  struct data *d = data;
  d->destroyed = TRUE;
}

static gboolean
advance_on_idle (gpointer data)
{
  WpTransition * self = WP_TRANSITION (data);
  wp_transition_advance (self);
  return G_SOURCE_REMOVE;
}

static void
wp_test_transition_init (WpTestTransition *self)
{
  self->step_third_wait = TRUE;
  self->step_third_error = FALSE;
}

static guint
wp_test_transition_get_next_step (WpTransition * transition, guint step)
{
  WpTestTransition * self = WP_TEST_TRANSITION (transition);
  struct data *d = wp_transition_get_data (transition);

  g_assert_nonnull (d);
  g_assert_cmpint (d->sta_i, <, 10);
  d->sta[d->sta_i++] = step;

  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      return STEP_FIRST;

    case STEP_FIRST:
    case STEP_SECOND:
      return step + 1;

    case STEP_THIRD:
      if (self->step_third_wait) {
        self->step_third_wait = FALSE;
        g_idle_add (advance_on_idle, transition);
        return STEP_THIRD;
      }
      return STEP_FINISH;

    case STEP_FINISH:
      return WP_TRANSITION_STEP_NONE;

    default:
      g_assert_not_reached ();
  }

  return WP_TRANSITION_STEP_ERROR;
}

static void
wp_test_transition_execute_step (WpTransition * transition, guint step)
{
  WpTestTransition * self = WP_TEST_TRANSITION (transition);
  struct data *d = wp_transition_get_data (transition);

  if (step != WP_TRANSITION_STEP_ERROR) {
    g_assert_cmpint (step, >=, STEP_FIRST);
    g_assert_cmpint (step, <=, STEP_FINISH);
  }

  g_assert_nonnull (d);
  g_assert_cmpint (d->ste_i, <, 10);
  d->ste[d->ste_i++] = step;

  if (step == STEP_THIRD && self->step_third_error) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, 100, "error"));
    return;
  }

  if (step != WP_TRANSITION_STEP_ERROR)
    g_idle_add (advance_on_idle, transition);
}

static void
wp_test_transition_class_init (WpTestTransitionClass *klass)
{
  WpTransitionClass *transition_class = (WpTransitionClass *) klass;

  transition_class->get_next_step = wp_test_transition_get_next_step;
  transition_class->execute_step = wp_test_transition_execute_step;
}

static void test_transition_basic (void);

static void
test_transition_basic_done (GObject *source, GAsyncResult *res, gpointer data)
{
  g_autoptr (GError) error = NULL;

  g_assert_true (WP_IS_TEST_TRANSITION (res));
  g_assert_true (g_async_result_is_tagged (res, test_transition_basic));
  g_assert_true (wp_transition_get_completed (WP_TRANSITION (res)));
  g_assert_false (wp_transition_had_error (WP_TRANSITION (res)));
  g_assert_true (wp_transition_finish (res, &error));
  g_assert_no_error (error);

  g_main_loop_quit ((GMainLoop *) data);
}

static void
test_transition_basic (void)
{
  struct data data = {0};
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (GObject) source_object = g_object_new (G_TYPE_OBJECT, NULL);

  WpTransition *t = wp_transition_new (wp_test_transition_get_type (),
      source_object, NULL, test_transition_basic_done, loop);
  g_assert_nonnull (t);
  g_assert_true (WP_IS_TRANSITION (t));
  g_assert_true (WP_IS_TEST_TRANSITION (t));
  g_assert_true (G_IS_ASYNC_RESULT (t));
  g_assert_true (wp_transition_get_source_object (t) == source_object);
  {
    g_autoptr (GObject) so = g_async_result_get_source_object (G_ASYNC_RESULT (t));
    g_assert_true (so == source_object);
  }
  g_assert_cmpint (source_object->ref_count, ==, 2);

  g_assert_null (wp_transition_get_data (t));
  wp_transition_set_data (t, &data, data_destroy);
  g_assert_true (wp_transition_get_data (t) == &data);
  g_assert_true (g_async_result_get_user_data (G_ASYNC_RESULT (t)) == &data);

  g_assert_null (wp_transition_get_source_tag (t));
  wp_transition_set_source_tag (t, test_transition_basic);
  g_assert_true (wp_transition_get_source_tag (t) == test_transition_basic);
  g_assert_true (wp_transition_is_tagged (t, test_transition_basic));

  wp_transition_advance (t);
  g_assert_false (wp_transition_get_completed (t));
  g_assert_false (wp_transition_had_error (t));

  g_main_loop_run (loop);

  g_assert_cmpint (source_object->ref_count, ==, 1);
  g_assert_cmpint (data.sta[0], ==, WP_TRANSITION_STEP_NONE);
  g_assert_cmpint (data.sta[1], ==, STEP_FIRST);
  g_assert_cmpint (data.sta[2], ==, STEP_SECOND);
  g_assert_cmpint (data.sta[3], ==, STEP_THIRD);
  g_assert_cmpint (data.sta[4], ==, STEP_THIRD);
  g_assert_cmpint (data.sta[5], ==, STEP_FINISH);
  g_assert_cmpint (data.sta_i, ==, 6);
  g_assert_cmpint (data.ste[0], ==, STEP_FIRST);
  g_assert_cmpint (data.ste[1], ==, STEP_SECOND);
  g_assert_cmpint (data.ste[2], ==, STEP_THIRD);
  g_assert_cmpint (data.ste[3], ==, STEP_FINISH);
  g_assert_cmpint (data.ste_i, ==, 4);
  g_assert_true (data.destroyed);
}

static void test_transition_error (void);

static void
test_transition_error_done (GObject *source, GAsyncResult *res, gpointer data)
{
  g_autoptr (GError) error = NULL;

  g_assert_true (WP_IS_TEST_TRANSITION (res));
  g_assert_true (g_async_result_is_tagged (res, test_transition_error));
  g_assert_true (wp_transition_get_completed (WP_TRANSITION (res)));
  g_assert_true (wp_transition_had_error (WP_TRANSITION (res)));
  g_assert_false (wp_transition_finish (res, &error));
  g_assert_error (error, WP_DOMAIN_LIBRARY, 100);

  g_main_loop_quit ((GMainLoop *) data);
}

static void
test_transition_error (void)
{
  struct data data = {0};
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (GObject) source_object = g_object_new (G_TYPE_OBJECT, NULL);

  WpTransition *t = wp_transition_new (wp_test_transition_get_type (),
      source_object, NULL, test_transition_error_done, loop);
  g_assert_nonnull (t);
  g_assert_true (WP_IS_TRANSITION (t));
  g_assert_true (WP_IS_TEST_TRANSITION (t));
  g_assert_true (G_IS_ASYNC_RESULT (t));
  g_assert_true (wp_transition_get_source_object (t) == source_object);
  {
    g_autoptr (GObject) so = g_async_result_get_source_object (G_ASYNC_RESULT (t));
    g_assert_true (so == source_object);
  }
  g_assert_cmpint (source_object->ref_count, ==, 2);

  g_assert_null (wp_transition_get_data (t));
  wp_transition_set_data (t, &data, data_destroy);
  g_assert_true (wp_transition_get_data (t) == &data);
  g_assert_true (g_async_result_get_user_data (G_ASYNC_RESULT (t)) == &data);

  g_assert_null (wp_transition_get_source_tag (t));
  wp_transition_set_source_tag (t, test_transition_error);
  g_assert_true (wp_transition_get_source_tag (t) == test_transition_error);
  g_assert_true (wp_transition_is_tagged (t, test_transition_error));

  /* enable error condition */
  ((WpTestTransition *) t)->step_third_error = TRUE;

  wp_transition_advance (t);
  g_assert_false (wp_transition_get_completed (t));
  g_assert_false (wp_transition_had_error (t));

  g_main_loop_run (loop);

  g_assert_cmpint (source_object->ref_count, ==, 1);
  g_assert_cmpint (data.sta[0], ==, WP_TRANSITION_STEP_NONE);
  g_assert_cmpint (data.sta[1], ==, STEP_FIRST);
  g_assert_cmpint (data.sta[2], ==, STEP_SECOND);
  g_assert_cmpint (data.sta_i, ==, 3);
  g_assert_cmpint (data.ste[0], ==, STEP_FIRST);
  g_assert_cmpint (data.ste[1], ==, STEP_SECOND);
  g_assert_cmpint (data.ste[2], ==, STEP_THIRD);
  g_assert_cmpint (data.ste[3], ==, WP_TRANSITION_STEP_ERROR);
  g_assert_cmpint (data.ste_i, ==, 4);
  g_assert_true (data.destroyed);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add_func ("/wp/transition/basic", test_transition_basic);
  g_test_add_func ("/wp/transition/error", test_transition_error);

  return g_test_run ();
}
