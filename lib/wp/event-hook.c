/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-event-hook"

#include "event-hook.h"
#include "event-dispatcher.h"
#include "transition.h"
#include "log.h"
#include "wpenums.h"

typedef struct _WpEventHookPrivate WpEventHookPrivate;
struct _WpEventHookPrivate
{
  gint priority;
  WpEventHookExecType exec_type;
  GWeakRef dispatcher;
  gchar *name;
  /* event triggering the hook, useful for after-events. */
  WpEvent *event_trigger;
};

enum {
  PROP_0,
  PROP_NAME,
  PROP_PRIORITY,
  PROP_EXEC_TYPE,
  PROP_DISPATCHER,
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpEventHook, wp_event_hook, G_TYPE_OBJECT)

static void
wp_event_hook_init (WpEventHook * self)
{
  WpEventHookPrivate *priv = wp_event_hook_get_instance_private (self);
  g_weak_ref_init (&priv->dispatcher, NULL);
}

static void
wp_event_hook_finalize (GObject * object)
{
  WpEventHook *self = WP_EVENT_HOOK (object);
  WpEventHookPrivate *priv = wp_event_hook_get_instance_private (self);

  g_weak_ref_clear (&priv->dispatcher);
  g_clear_pointer (&priv->event_trigger, wp_event_unref);
  g_free (priv->name);

  G_OBJECT_CLASS (wp_event_hook_parent_class)->finalize (object);
}

static void
wp_event_hook_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpEventHook *self = WP_EVENT_HOOK (object);
  WpEventHookPrivate *priv = wp_event_hook_get_instance_private (self);

  switch (property_id) {
  case PROP_NAME:
    priv->name = g_value_dup_string (value);
    break;
  case PROP_PRIORITY:
    priv->priority = g_value_get_int (value);
    break;
  case PROP_EXEC_TYPE:
    priv->exec_type = g_value_get_enum (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_event_hook_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpEventHook *self = WP_EVENT_HOOK (object);
  WpEventHookPrivate *priv = wp_event_hook_get_instance_private (self);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, priv->name);
    break;
  case PROP_PRIORITY:
    g_value_set_int (value, priv->priority);
    break;
  case PROP_EXEC_TYPE:
    g_value_set_enum (value, priv->exec_type);
    break;
  case PROP_DISPATCHER:
    g_value_take_object (value, wp_event_hook_get_dispatcher (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_event_hook_class_init (WpEventHookClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_event_hook_finalize;
  object_class->set_property = wp_event_hook_set_property;
  object_class->get_property = wp_event_hook_get_property;

  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "The hook name", "",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PRIORITY,
      g_param_spec_int ("priority", "priority",
          "The priority of the hook", -G_MAXINT, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_EXEC_TYPE,
      g_param_spec_enum ("exec-type", "exec-type", "The exec type of the hook",
          WP_TYPE_EVENT_HOOK_EXEC_TYPE, WP_EVENT_HOOK_EXEC_TYPE_ON_EVENT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_DISPATCHER,
      g_param_spec_object ("dispatcher", "dispatcher",
          "The associated event dispatcher", WP_TYPE_EVENT_DISPATCHER,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Returns the event triggering the hook, this data is useful for
 * after-events.
 *
 * \ingroup wpeventhook
 * \param self the event hook
 * \returns (transfer none) (nullable): the event
 */
WpEvent *
wp_event_hook_get_event (WpEventHook *self)
{
  g_return_val_if_fail (WP_IS_EVENT_HOOK (self), 0);
  WpEventHookPrivate *priv = wp_event_hook_get_instance_private (self);
  return priv->event_trigger;
}

/*!
 * \brief Sets the event triggering the hook
 *
 * \ingroup wpeventhook
 * \param self the event hook
 * \param event (transfer none) (nullable):the event associated with the hook
 */
void
wp_event_hook_set_event (WpEventHook *self, WpEvent *event)
{
  WpEventHookPrivate *priv = wp_event_hook_get_instance_private (self);
  g_clear_pointer (&priv->event_trigger, wp_event_unref);
  if (event)
    priv->event_trigger = wp_event_ref (event);
}

/*!
 * \brief Returns the priority of the hook
 *
 * \ingroup wpeventhook
 * \param self the event hook
 * \return the event hook priority
 */
gint
wp_event_hook_get_priority (WpEventHook * self)
{
  g_return_val_if_fail (WP_IS_EVENT_HOOK (self), 0);
  WpEventHookPrivate *priv = wp_event_hook_get_instance_private (self);
  return priv->priority;
}

/*!
 * \brief Returns the name of the hook
 *
 * \ingroup wpeventhook
 * \param self the event hook
 * \return the event hook name
 */
gchar *
wp_event_hook_get_name (WpEventHook * self)
{
  g_return_val_if_fail (WP_IS_EVENT_HOOK (self), 0);
  WpEventHookPrivate *priv = wp_event_hook_get_instance_private (self);
  return priv->name;
}

/*!
 * \brief Returns the execution type of the hook
 *
 * \ingroup wpeventhook
 * \param self the event hook
 * \return the event hook execution type
 */
WpEventHookExecType
wp_event_hook_get_exec_type (WpEventHook * self)
{
  g_return_val_if_fail (WP_IS_EVENT_HOOK (self), 0);
  WpEventHookPrivate *priv = wp_event_hook_get_instance_private (self);
  return priv->exec_type;
}

/*!
 * \brief Returns the associated event dispatcher
 *
 * \ingroup wpeventhook
 * \param self the event hook
 * \return (transfer full)(nullable): the event dispatcher on which this hook
 *   is registered, or NULL if the hook is not registered
 */
WpEventDispatcher *
wp_event_hook_get_dispatcher (WpEventHook * self)
{
  g_return_val_if_fail (WP_IS_EVENT_HOOK (self), NULL);
  WpEventHookPrivate *priv = wp_event_hook_get_instance_private (self);
  return g_weak_ref_get (&priv->dispatcher);
}

/*!
 * \brief Sets the dispatcher to the hook
 *
 * \ingroup wpeventhook
 * \param self the event hook
 * \param dispatcher (transfer none) (nullable) dispatcher to which the hook is
 *  to be registered.
 */
void
wp_event_hook_set_dispatcher (WpEventHook * self, WpEventDispatcher * dispatcher)
{
  WpEventHookPrivate *priv = wp_event_hook_get_instance_private (self);
  g_weak_ref_set (&priv->dispatcher, dispatcher);
}

/*!
 * \brief Checks if the hook should be executed for a given event
 *
 * \ingroup wpeventhook
 * \param self the event hook
 * \param event the event
 * \return TRUE if the hook should be executed for the given event,
 *    FALSE otherwise
 */
gboolean
wp_event_hook_runs_for_event (WpEventHook * self, WpEvent * event)
{
  g_return_val_if_fail (WP_IS_EVENT_HOOK (self), FALSE);
  g_return_val_if_fail (WP_EVENT_HOOK_GET_CLASS (self)->runs_for_event, FALSE);
  return WP_EVENT_HOOK_GET_CLASS (self)->runs_for_event (self, event);
}

/*!
 * \brief Runs the hook on the given event
 *
 * \ingroup wpeventhook
 * \param self the event hook
 * \param event the event that triggered the hook
 * \param cancellable (nullable): a GCancellable to cancel the async operation
 * \param callback (scope async): a callback to fire after execution of the hook
 *    has completed
 * \param callback_data (closure): data for the callback
 */
void
wp_event_hook_run (WpEventHook * self,
    WpEvent * event, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer callback_data)
{
  g_return_if_fail (WP_IS_EVENT_HOOK (self));
  g_return_if_fail (WP_EVENT_HOOK_GET_CLASS (self)->run);
  WP_EVENT_HOOK_GET_CLASS (self)->run (self, event, cancellable, callback,
      callback_data);
}

/*!
 * \brief Finishes the async operation that was started by wp_event_hook_run()
 *
 * \ingroup wpeventhook
 * \param self the event hook
 * \param res the async operation result
 * \param error (out) (optional): the error of the operation, if any
 * \returns FALSE if there was an error, TRUE otherwise
 */
gboolean
wp_event_hook_finish (WpEventHook * self, GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_EVENT_HOOK (self), FALSE);
  g_return_val_if_fail (WP_EVENT_HOOK_GET_CLASS (self)->finish, FALSE);
  return WP_EVENT_HOOK_GET_CLASS (self)->finish (self, res, error);
}



/*!
 * \struct WpInterestEventHook
 *
 * An event hook that declares interest in specific events. This subclass
 * implements the WpEventHook.runs_for_event() vmethod and returns TRUE from
 * that method if the given event has properties that match one of the declared
 * interests.
 */

typedef struct _WpInterestEventHookPrivate WpInterestEventHookPrivate;
struct _WpInterestEventHookPrivate
{
  /* element-type: WpObjectInterest* */
  GPtrArray *interests;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpInterestEventHook,
                                     wp_interest_event_hook, WP_TYPE_EVENT_HOOK)

static void
wp_interest_event_hook_init (WpInterestEventHook * self)
{
  WpInterestEventHookPrivate *priv =
      wp_interest_event_hook_get_instance_private (self);
  priv->interests = g_ptr_array_new_with_free_func (
      (GDestroyNotify) wp_object_interest_unref);
}

static void
wp_interest_event_hook_finalize (GObject * object)
{
  WpInterestEventHook *self = WP_INTEREST_EVENT_HOOK (object);
  WpInterestEventHookPrivate *priv =
      wp_interest_event_hook_get_instance_private (self);

  g_clear_pointer (&priv->interests, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_interest_event_hook_parent_class)->finalize (object);
}

static gboolean
wp_interest_event_hook_runs_for_event (WpEventHook * hook, WpEvent * event)
{
  WpInterestEventHook *self = WP_INTEREST_EVENT_HOOK (hook);
  WpInterestEventHookPrivate *priv =
      wp_interest_event_hook_get_instance_private (self);
  g_autoptr (WpProperties) properties = wp_event_get_properties (event);
  g_autoptr (GObject) subject = wp_event_get_subject (event);
  GType gtype = subject ? G_OBJECT_TYPE (subject) : WP_TYPE_EVENT;
  guint i;
  WpObjectInterest *interest = NULL;
  WpInterestMatch match;

  const int MATCH_ALL_PROPS = (WP_INTEREST_MATCH_PW_GLOBAL_PROPERTIES |
                               WP_INTEREST_MATCH_PW_PROPERTIES |
                               WP_INTEREST_MATCH_G_PROPERTIES);

  for (i = 0; i < priv->interests->len; i++) {
    interest = g_ptr_array_index (priv->interests, i);
    match = wp_object_interest_matches_full (interest,
        WP_INTEREST_MATCH_FLAGS_CHECK_ALL,
        gtype, subject, properties, properties);

    /* the interest may have a GType that matches the GType of the subject
       or it may have WP_TYPE_EVENT as its GType, in which case it will
       match any type of subject */
    if (match == WP_INTEREST_MATCH_ALL)
      return TRUE;
    else if (subject && (match & MATCH_ALL_PROPS) == MATCH_ALL_PROPS) {
      match = wp_object_interest_matches_full (interest, 0,
          WP_TYPE_EVENT, NULL, NULL, NULL);
      if (match & WP_INTEREST_MATCH_GTYPE)
        return TRUE;
    }
  }
  return FALSE;
}

static void
wp_interest_event_hook_class_init (WpInterestEventHookClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpEventHookClass *hook_class = (WpEventHookClass *) klass;

  object_class->finalize = wp_interest_event_hook_finalize;
  hook_class->runs_for_event = wp_interest_event_hook_runs_for_event;
}

/*!
 * \brief Equivalent to:
 * \code
 * WpObjectInterest *i = wp_object_interest_new (WP_TYPE_EVENT, ...);
 * wp_interest_event_hook_add_interest_full (self, i);
 * \endcode
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * \ingroup wpeventhook
 * \param self the event hook
 * \param ... a list of constraints, terminated by NULL
 */
void
wp_interest_event_hook_add_interest (WpInterestEventHook * self, ...)
{
  WpObjectInterest *interest;
  va_list args;

  g_return_if_fail (WP_IS_INTEREST_EVENT_HOOK (self));

  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_EVENT, &args);
  wp_interest_event_hook_add_interest_full (self, interest);
  va_end (args);
}

/*!
 * \brief Declares interest on events. The interest must be constructed to
 * match WP_TYPE_EVENT objects and it is going to be matched against
 * the WpEvent's properties.
 *
 * \param self the event hook
 * \param interest (transfer full): the event interest
 */
void
wp_interest_event_hook_add_interest_full (WpInterestEventHook * self,
    WpObjectInterest * interest)
{
  g_autoptr (GError) error = NULL;

  g_return_if_fail (WP_IS_INTEREST_EVENT_HOOK (self));

  if (G_UNLIKELY (!wp_object_interest_validate (interest, &error))) {
    wp_critical_object (self, "interest validation failed: %s",
        error->message);
    wp_object_interest_unref (interest);
    return;
  }

  WpInterestEventHookPrivate *priv =
      wp_interest_event_hook_get_instance_private (self);
  g_ptr_array_add (priv->interests, interest);
}



/*!
 * \struct WpSimpleEventHook
 *
 * An event hook that runs a GClosure, synchronously.
 */

struct _WpSimpleEventHook
{
  WpInterestEventHook parent;
  GClosure *closure;
};

enum {
  PROP_SIMPLE_0,
  PROP_SIMPLE_CLOSURE,
};

G_DEFINE_TYPE (WpSimpleEventHook, wp_simple_event_hook,
               WP_TYPE_INTEREST_EVENT_HOOK)

static void
wp_simple_event_hook_init (WpSimpleEventHook * self)
{
}

static void
wp_simple_event_hook_finalize (GObject * object)
{
  WpSimpleEventHook *self = WP_SIMPLE_EVENT_HOOK (object);

  g_clear_pointer (&self->closure, g_closure_unref);

  G_OBJECT_CLASS (wp_simple_event_hook_parent_class)->finalize (object);
}

static void
wp_simple_event_hook_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpSimpleEventHook *self = WP_SIMPLE_EVENT_HOOK (object);

  switch (property_id) {
  case PROP_SIMPLE_CLOSURE:
    self->closure = g_value_dup_boxed (value);
    g_closure_sink (self->closure);
    if (G_CLOSURE_NEEDS_MARSHAL (self->closure))
      g_closure_set_marshal (self->closure, g_cclosure_marshal_generic);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_simple_event_hook_run (WpEventHook * hook,
    WpEvent * event, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer callback_data)
{
  WpSimpleEventHook *self = WP_SIMPLE_EVENT_HOOK (hook);

  GValue values[2] = { G_VALUE_INIT };
  g_value_init (&values[0], WP_TYPE_EVENT);
  g_value_set_boxed (&values[0], event);
  g_closure_invoke (self->closure, NULL, 1, values, NULL);
  g_value_unset (&values[0]);

  callback ((GObject *) self, NULL, callback_data);
}

static gboolean
wp_simple_event_hook_finish (WpEventHook * hook, GAsyncResult * res,
    GError ** error)
{
  return TRUE;
}

static void
wp_simple_event_hook_class_init (WpSimpleEventHookClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpEventHookClass *hook_class = (WpEventHookClass *) klass;

  object_class->finalize = wp_simple_event_hook_finalize;
  object_class->set_property = wp_simple_event_hook_set_property;
  hook_class->run = wp_simple_event_hook_run;
  hook_class->finish = wp_simple_event_hook_finish;

  g_object_class_install_property (object_class, PROP_SIMPLE_CLOSURE,
      g_param_spec_boxed ("closure", "closure", "The closure", G_TYPE_CLOSURE,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Constructs a new simple event hook
 *
 * \param name the name of the hook
 * \param priority the priority of the hook
 * \param type the execution type of the hook
 * \param closure the closure to invoke when the hook is executed; the closure
 *   should accept two parameters: the event dispatcher and the event, returning
 *   nothing
 * \return a new simple event hook
 */
WpEventHook *
wp_simple_event_hook_new (const gchar *name, gint priority,
    WpEventHookExecType type, GClosure * closure)
{
  g_return_val_if_fail (closure != NULL, NULL);

  return g_object_new (WP_TYPE_SIMPLE_EVENT_HOOK,
      "name", name,
      "priority", priority,
      "exec-type", type,
      "closure", closure,
      NULL);
}


/* WpAsyncEventHookTransition */

#define WP_TYPE_ASYNC_EVENT_HOOK_TRANSITION \
    (wp_async_event_hook_transition_get_type ())
G_DECLARE_FINAL_TYPE (WpAsyncEventHookTransition,
                      wp_async_event_hook_transition,
                      WP, ASYNC_EVENT_HOOK_TRANSITION, WpTransition)


/*!
 * \struct WpAsyncEventHook
 *
 * An event hook that runs a WpTransition, implemented with closures.
 */

struct _WpAsyncEventHook
{
  WpInterestEventHook parent;
  GClosure *get_next_step;
  GClosure *execute_step;
};

enum {
  PROP_ASYNC_0,
  PROP_ASYNC_GET_NEXT_STEP,
  PROP_ASYNC_EXECUTE_STEP,
};

G_DEFINE_TYPE (WpAsyncEventHook, wp_async_event_hook,
               WP_TYPE_INTEREST_EVENT_HOOK)

static void
wp_async_event_hook_init (WpAsyncEventHook * self)
{
}

static void
wp_async_event_hook_finalize (GObject * object)
{
  WpAsyncEventHook *self = WP_ASYNC_EVENT_HOOK (object);

  g_clear_pointer (&self->get_next_step, g_closure_unref);
  g_clear_pointer (&self->execute_step, g_closure_unref);

  G_OBJECT_CLASS (wp_async_event_hook_parent_class)->finalize (object);
}

static void
wp_async_event_hook_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpAsyncEventHook *self = WP_ASYNC_EVENT_HOOK (object);

  switch (property_id) {
  case PROP_ASYNC_GET_NEXT_STEP:
    self->get_next_step = g_value_dup_boxed (value);
    g_closure_sink (self->get_next_step);
    if (G_CLOSURE_NEEDS_MARSHAL (self->get_next_step))
      g_closure_set_marshal (self->get_next_step, g_cclosure_marshal_generic);
    break;
  case PROP_ASYNC_EXECUTE_STEP:
    self->execute_step = g_value_dup_boxed (value);
    g_closure_sink (self->execute_step);
    if (G_CLOSURE_NEEDS_MARSHAL (self->execute_step))
      g_closure_set_marshal (self->execute_step, g_cclosure_marshal_VOID__UINT);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_async_event_hook_run (WpEventHook * hook,
    WpEvent * event, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer callback_data)
{
  WpTransition *transition = wp_transition_new (
      WP_TYPE_ASYNC_EVENT_HOOK_TRANSITION,
      hook, cancellable, callback, callback_data);
  wp_transition_set_data (transition, wp_event_ref (event),
      (GDestroyNotify) wp_event_unref);
  wp_transition_set_source_tag (transition, wp_async_event_hook_run);
  wp_transition_advance (transition);
}

static gboolean
wp_async_event_hook_finish (WpEventHook * hook, GAsyncResult * res,
    GError ** error)
{
  g_return_val_if_fail (g_async_result_is_tagged (res, wp_async_event_hook_run),
      FALSE);
  return wp_transition_finish (res, error);
}

static void
wp_async_event_hook_class_init (WpAsyncEventHookClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpEventHookClass *hook_class = (WpEventHookClass *) klass;

  object_class->finalize = wp_async_event_hook_finalize;
  object_class->set_property = wp_async_event_hook_set_property;
  hook_class->run = wp_async_event_hook_run;
  hook_class->finish = wp_async_event_hook_finish;

  g_object_class_install_property (object_class, PROP_ASYNC_GET_NEXT_STEP,
      g_param_spec_boxed ("get-next-step", "get-next-step",
          "The get-next-step closure", G_TYPE_CLOSURE,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ASYNC_EXECUTE_STEP,
      g_param_spec_boxed ("execute-step", "execute-step",
          "The execute-step closure", G_TYPE_CLOSURE,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Constructs a new async event hook
 *
 * \param name the name of the hook
 * \param priority the priority of the hook
 * \param type the execution type of the hook
 * \param get_next_step the closure to invoke to get the next step
 * \param execute_step the closure to invoke to execute the step
 * \return a new async event hook
 */
WpEventHook *
wp_async_event_hook_new (const gchar *name, gint priority,
    WpEventHookExecType type, GClosure * get_next_step,
    GClosure * execute_step)
{
  g_return_val_if_fail (get_next_step != NULL, NULL);
  g_return_val_if_fail (execute_step != NULL, NULL);

  return g_object_new (WP_TYPE_ASYNC_EVENT_HOOK,
      "name", name,
      "priority", priority,
      "exec-type", type,
      "get-next-step", get_next_step,
      "execute-step", execute_step,
      NULL);
}


/* WpAsyncEventHookTransition - implementation */

struct _WpAsyncEventHookTransition
{
  WpTransition parent;
};

G_DEFINE_TYPE (WpAsyncEventHookTransition,
               wp_async_event_hook_transition,
               WP_TYPE_TRANSITION)

static void
wp_async_event_hook_transition_init (
    WpAsyncEventHookTransition * transition)
{
}

static guint
wp_async_event_hook_transition_get_next_step (
    WpTransition * transition, guint step)
{
  WpAsyncEventHook *hook =
      WP_ASYNC_EVENT_HOOK (wp_transition_get_source_object (transition));
  guint next_step = WP_TRANSITION_STEP_ERROR;
  GValue ret = G_VALUE_INIT;
  GValue values[2] = { G_VALUE_INIT, G_VALUE_INIT };

  g_value_init (&ret, G_TYPE_UINT);
  g_value_init (&values[0], G_TYPE_OBJECT);
  g_value_init (&values[1], G_TYPE_UINT);
  g_value_set_object (&values[0], transition);
  g_value_set_uint (&values[1], step);
  g_closure_invoke (hook->get_next_step, &ret, 2, values, NULL);
  g_value_unset (&values[0]);
  g_value_unset (&values[1]);
  next_step = g_value_get_uint (&ret);
  g_value_unset (&ret);
  return next_step;
}

static void
wp_async_event_hook_transition_execute_step (
    WpTransition * transition, guint step)
{
  WpAsyncEventHook *hook =
      WP_ASYNC_EVENT_HOOK (wp_transition_get_source_object (transition));
  GValue values[2] = { G_VALUE_INIT, G_VALUE_INIT };

  g_value_init (&values[0], G_TYPE_OBJECT);
  g_value_init (&values[1], G_TYPE_UINT);
  g_value_set_object (&values[0], transition);
  g_value_set_uint (&values[1], step);
  g_closure_invoke (hook->execute_step, NULL, 2, values, NULL);
  g_value_unset (&values[0]);
  g_value_unset (&values[1]);
}

static void
wp_async_event_hook_transition_class_init (
    WpAsyncEventHookTransitionClass * klass)
{
  WpTransitionClass *transition_class = (WpTransitionClass *) klass;

  transition_class->get_next_step =
      wp_async_event_hook_transition_get_next_step;
  transition_class->execute_step =
      wp_async_event_hook_transition_execute_step;
}
