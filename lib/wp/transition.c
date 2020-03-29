/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpTransition
 *
 * A transition is an asynchronous operation, like #GTask, that contains
 * an internal state machine, where a series of 'steps' are executed in order
 * to complete the operation.
 *
 * For every step, #WpTransitionClass.get_next_step() is called in order to
 * determine the next step to execute. Afterwards,
 * #WpTransitionClass.execute_step() is called
 * to perform any actions necessary to complete this step. When execution
 * of the step is done, the operation's code must call wp_transition_advance()
 * in order to continue to the next step. If an error occurs, the operation's
 * code must call wp_transition_return_error() instead, in which case the
 * transition completes immediately and wp_transition_had_error() returns %TRUE.
 *
 * Typically, every step will start an asynchronous operation. Although is is
 * possible, the #WpTransition base class does not expect
 * #WpTransitionClass.execute_step() to call wp_transition_advance() directly.
 * Instead, it is expected that wp_transition_advance() will be called from
 * the callback that the step's asyncrhonous operation will call when it is
 * completed.
 */

#include "transition.h"
#include "error.h"

typedef struct _WpTransitionPrivate WpTransitionPrivate;
struct _WpTransitionPrivate
{
  /* source obj & callback */
  GObject *source_object;
  GCancellable *cancellable;
  GAsyncReadyCallback callback;
  gpointer callback_data;

  /* GAsyncResult tag */
  gpointer tag;

  /* task data */
  gpointer data;
  GDestroyNotify data_destroy;

  /* state machine */
  guint step;
  GError *error;
};

enum {
  PROP_0,
  PROP_COMPLETED,
};

static void wp_transition_async_result_init (GAsyncResultIface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (WpTransition, wp_transition, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_RESULT, wp_transition_async_result_init)
    G_ADD_PRIVATE (WpTransition))

static void
wp_transition_init (WpTransition * self)
{
}

static void
wp_transition_finalize (GObject * object)
{
  WpTransition * self = WP_TRANSITION (object);
  WpTransitionPrivate *priv = wp_transition_get_instance_private (self);

  if (priv->data && priv->data_destroy)
    priv->data_destroy (priv->data);

  g_clear_error (&priv->error);
  g_clear_object (&priv->cancellable);
  g_clear_object (&priv->source_object);

  G_OBJECT_CLASS (wp_transition_parent_class)->finalize (object);
}

static void
wp_transition_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpTransition * self = WP_TRANSITION (object);

  switch (property_id) {
  case PROP_COMPLETED:
    g_value_set_boolean (value, wp_transition_get_completed (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_transition_class_init (WpTransitionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_transition_finalize;
  object_class->get_property = wp_transition_get_property;

  /**
   * WpTransition::completed:
   *
   * Whether the transition has completed, meaning its callback (if set)
   * has been invoked. This can only happen after the final step has been
   * reached or wp_transition_return_error() has been called.
   *
   * This property is guaranteed to change from %FALSE to %TRUE exactly once.
   *
   * The “notify” signal for this change is emitted in the same context
   * as the transition’s callback, immediately after that callback is invoked.
   */
  g_object_class_install_property (object_class, PROP_COMPLETED,
      g_param_spec_boolean ("completed", "completed",
          "Whether the transition has completed", FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static GObject *
get_source_object (GAsyncResult * res)
{
  WpTransitionPrivate *priv =
      wp_transition_get_instance_private (WP_TRANSITION (res));
  return priv->source_object ? g_object_ref (priv->source_object) : NULL;
}

static void
wp_transition_async_result_init (GAsyncResultIface * iface)
{
  iface->get_source_object = get_source_object;
  iface->get_user_data =
      (gpointer (*)(GAsyncResult *)) wp_transition_get_data;
  iface->is_tagged =
      (gboolean (*)(GAsyncResult *, gpointer)) wp_transition_is_tagged;
}

/**
 * wp_transition_new:
 * @type: the #GType of the #WpTransition subclass to instantiate
 * @source_object: (nullable) (type GObject): the #GObject that owns this task,
 *   or %NULL
 * @cancellable: (nullable): optional #GCancellable
 * @callback: (scope async): a #GAsyncReadyCallback
 * @callback_data: (closure): user data passed to @callback
 *
 * Creates a #WpTransition acting on @source_object. When the transition is
 * done, @callback will be invoked.
 *
 * The transition does not automatically start executing steps. You must
 * call wp_transition_advance() after creating it in order to start it.
 *
 * Note that the transition is automatically unref'ed after the @callback
 * has been executed. If you wish to keep an additional reference on it,
 * you need to ref it explicitly.
 *
 * Returns: (transfer none): the new transition
 */
WpTransition *
wp_transition_new (GType type,
    gpointer source_object, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer callback_data)
{
  g_return_val_if_fail (G_IS_OBJECT (source_object), NULL);

  WpTransition *self = g_object_new (type, NULL);
  WpTransitionPrivate *priv = wp_transition_get_instance_private (self);

  priv->source_object = source_object ? g_object_ref (source_object) : NULL;
  priv->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
  priv->callback = callback;
  priv->callback_data = callback_data;

  return self;
}

/**
 * wp_transition_get_source_object:
 * @self: the transition
 *
 * Gets the source object from the transition.
 * Like g_async_result_get_source_object(), but does not ref the object.
 *
 * Returns: (transfer none) (type GObject): the source object
 */
gpointer
wp_transition_get_source_object (WpTransition * self)
{
  g_return_val_if_fail (WP_IS_TRANSITION (self), NULL);

  WpTransitionPrivate *priv = wp_transition_get_instance_private (self);
  return priv->source_object;
}

/**
 * wp_transition_is_tagged:
 * @self: the transition
 * @tag: a tag
 *
 * Checks if @self has the given @tag (generally a function pointer
 * indicating the function @self was created by).
 *
 * Returns: TRUE if @self has the indicated @tag , FALSE if not.
 */
gboolean
wp_transition_is_tagged (WpTransition * self, gpointer tag)
{
  g_return_val_if_fail (WP_IS_TRANSITION (self), FALSE);

  WpTransitionPrivate *priv = wp_transition_get_instance_private (self);
  return (priv->tag == tag);
}

/**
 * wp_transition_get_source_tag:
 * @self: the transition
 *
 * Gets @self 's source tag. See wp_transition_set_source_tag().
 *
 * Returns: (transfer none): the transition's source tag
 */
gpointer
wp_transition_get_source_tag (WpTransition * self)
{
  g_return_val_if_fail (WP_IS_TRANSITION (self), NULL);

  WpTransitionPrivate *priv = wp_transition_get_instance_private (self);
  return priv->tag;
}

/**
 * wp_transition_set_source_tag:
 * @self: the transition
 * @tag: an opaque pointer indicating the source of this transition
 *
 * Sets @self 's source tag. You can use this to tag a transition's return
 * value with a particular pointer (usually a pointer to the function doing
 * the tagging) and then later check it using wp_transition_get_source_tag()
 * (or g_async_result_is_tagged()) in the transition's "finish" function,
 * to figure out if the response came from a particular place.
 */
void
wp_transition_set_source_tag (WpTransition * self, gpointer tag)
{
  g_return_if_fail (WP_IS_TRANSITION (self));

  WpTransitionPrivate *priv = wp_transition_get_instance_private (self);
  priv->tag = tag;
}

/**
 * wp_transition_get_data:
 * @self: the transition
 *
 * Gets @self 's data. See wp_transition_set_data().
 *
 * Returns: (transfer none): the transition's data
 */
gpointer
wp_transition_get_data (WpTransition * self)
{
  g_return_val_if_fail (WP_IS_TRANSITION (self), NULL);

  WpTransitionPrivate *priv = wp_transition_get_instance_private (self);
  return priv->data;
}

/**
 * wp_transition_set_data:
 * @self: the transition
 * @data: (nullable): transition-specific user data
 * @data_destroy: (nullable): #GDestroyNotify for @data
 *
 * Sets @self 's data (freeing the existing data, if any). This can be an
 * arbitrary user structure that holds data associated with this transition.
 */
void
wp_transition_set_data (WpTransition * self, gpointer data,
    GDestroyNotify data_destroy)
{
  g_return_if_fail (WP_IS_TRANSITION (self));

  WpTransitionPrivate *priv = wp_transition_get_instance_private (self);
  if (priv->data && priv->data_destroy)
    priv->data_destroy (priv->data);
  priv->data = data;
  priv->data_destroy = data_destroy;
}

/**
 * wp_transition_get_completed:
 * @self: the transition
 *
 * Returns: %TRUE if the transition has completed (with or without an error),
 *   %FALSE otherwise
 */
gboolean
wp_transition_get_completed (WpTransition * self)
{
  g_return_val_if_fail (WP_IS_TRANSITION (self), FALSE);

  WpTransitionPrivate *priv = wp_transition_get_instance_private (self);
  return priv->step == WP_TRANSITION_STEP_NONE ||
         priv->step == WP_TRANSITION_STEP_ERROR;
}

/**
 * wp_transition_had_error:
 * @self: the transition
 *
 * Returns: %TRUE if the transition completed with an error, %FALSE otherwise
 */
gboolean
wp_transition_had_error (WpTransition * self)
{
  g_return_val_if_fail (WP_IS_TRANSITION (self), FALSE);

  WpTransitionPrivate *priv = wp_transition_get_instance_private (self);
  return priv->step == WP_TRANSITION_STEP_ERROR;
}

static void
wp_transition_return (WpTransition * self, WpTransitionPrivate *priv)
{
  if (priv->callback) {
      priv->callback (
          priv->source_object ? G_OBJECT (priv->source_object) : NULL,
          G_ASYNC_RESULT (self),
          priv->callback_data);
  }

  g_object_notify (G_OBJECT (self), "completed");

  /* WARNING */
  g_object_unref (self);
}

/**
 * wp_transition_advance:
 * @self: the transition
 *
 * Advances the transition to the next step.
 *
 * This initially calls #WpTransitionClass.get_next_step() in order to determine
 * what the next step is. If #WpTransitionClass.get_next_step() returns a step
 * different than the previous one, it calls #WpTransitionClass.execute_step()
 * to execute it.
 *
 * The very first time that #WpTransitionClass.get_next_step() is called, its
 * @step parameter equals %WP_TRANSITION_STEP_NONE.
 *
 * When #WpTransitionClass.get_next_step() returns %WP_TRANSITION_STEP_NONE,
 * this function completes the transition, calling the transition's callback
 * and then unref-ing the transition.
 *
 * When #WpTransitionClass.get_next_step() returns %WP_TRANSITION_STEP_ERROR,
 * this function calls wp_transition_return_error(), unless it has already been
 * called directly by #WpTransitionClass.get_next_step().
 *
 * In error conditions, #WpTransitionClass.execute_step() is called once with
 * @step being %WP_TRANSITION_STEP_ERROR, allowing the implementation to
 * rollback any changes or cancel underlying jobs, if necessary.
 */
void
wp_transition_advance (WpTransition * self)
{
  g_return_if_fail (WP_IS_TRANSITION (self));

  /* keep a reference to avoid issues when wp_transition_return_error() is
     called from within get_next_step() */
  g_autoptr (WpTransition) self_ref = g_object_ref (self);
  WpTransitionPrivate *priv = wp_transition_get_instance_private (self);
  guint next_step;

  /* find the next step */
  next_step = WP_TRANSITION_GET_CLASS (self)->get_next_step (self, priv->step);

  if (next_step == WP_TRANSITION_STEP_ERROR) {
    /* return error if the callback didn't do it already */
    if (G_UNLIKELY (!priv->error)) {
        wp_transition_return_error (self, g_error_new (WP_DOMAIN_LIBRARY,
                WP_LIBRARY_ERROR_INVARIANT, "state machine error"));
    }
    return;
  }

  /* if we reached STEP_NONE again, that means we reached the next state */
  if (next_step == WP_TRANSITION_STEP_NONE) {
    /* complete the transition */
    priv->step = next_step;
    wp_transition_return (self, priv);
    return;
  }

  /* still at the same step, this means we are waiting for something */
  if (next_step == priv->step)
    return;

  /* execute the next step */
  priv->step = next_step;
  WP_TRANSITION_GET_CLASS (self)->execute_step (self, priv->step);
}

/**
 * wp_transition_return_error:
 * @self: the transition
 * @error: (transfer full): a #GError
 *
 * Completes the transition with an error. This can be called anytime
 * from within any virtual function or an async job handler.
 *
 * Note that in most cases this will also unref the transition, so it is
 * not safe to access it after this function has been called.
 */
void
wp_transition_return_error (WpTransition * self, GError * error)
{
  g_return_if_fail (WP_IS_TRANSITION (self));

  WpTransitionPrivate *priv = wp_transition_get_instance_private (self);

  if (G_UNLIKELY (priv->error)) {
    g_warning ("transition bailing out multiple times; old error was: %s",
        priv->error->message);
    g_clear_error (&priv->error);
  }

  priv->step = WP_TRANSITION_STEP_ERROR;
  priv->error = error;

  /* allow the implementation to rollback changes */
  if (WP_TRANSITION_GET_CLASS (self)->execute_step)
    WP_TRANSITION_GET_CLASS (self)->execute_step (self, priv->step);

  wp_transition_return (self, priv);
}

/**
 * wp_transition_finish:
 * @res: a transition, as a #GAsyncResult
 * @error: (out) (optional): a location to return the transition's error, if any
 *
 * This is meant to be called from within the #GAsyncReadyCallback that was
 * specified in wp_transition_new(). It returns the final return status
 * of the transition and its error, if there was one.
 *
 * Returns: %TRUE if the transition completed successfully, %FALSE if there
 *   was an error
 */
gboolean
wp_transition_finish (GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_TRANSITION (res), FALSE);

  WpTransitionPrivate *priv =
      wp_transition_get_instance_private (WP_TRANSITION (res));
  if (priv->error) {
    g_propagate_error (error, priv->error);
    priv->error = NULL;
  }
  return (priv->step == WP_TRANSITION_STEP_NONE);
}
