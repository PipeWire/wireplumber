/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <spa/utils/defs.h>

#include "object.h"
#include "log.h"
#include "core.h"
#include "error.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-object")

/*! \defgroup wpfeatureactivationtransition WpFeatureActivationTransition */
/*!
 * \struct WpFeatureActivationTransition
 * A WpTransition that is used by WpObject to implement feature activation.
 */
struct _WpFeatureActivationTransition
{
  WpTransition parent;
  WpObjectFeatures missing;
};

G_DEFINE_TYPE (WpFeatureActivationTransition,
               wp_feature_activation_transition,
               WP_TYPE_TRANSITION)

static void
wp_feature_activation_transition_init (
    WpFeatureActivationTransition * transition)
{
}

static WpObjectFeatures
wp_feature_activation_transition_calc_missing_features (
    WpFeatureActivationTransition * self, WpObject * object)
{
  /* missing features = features that have been requested,
     they are supported and they are not active yet;
     note that supported features may change while the transition is ongoing,
     which is why we store the requested features as they were originally
     and keep trying to activate everything that is supported at the time */
  WpObjectFeatures requested =
      wp_feature_activation_transition_get_requested_features (self);
  WpObjectFeatures supported = wp_object_get_supported_features (object);
  WpObjectFeatures active = wp_object_get_active_features (object);
  return (requested & supported & ~active);
}

static guint
wp_feature_activation_transition_get_next_step (
    WpTransition * transition, guint step)
{
  WpFeatureActivationTransition *self =
      WP_FEATURE_ACTIVATION_TRANSITION (transition);
  WpObject *object = wp_transition_get_source_object (transition);

  self->missing =
      wp_feature_activation_transition_calc_missing_features (self, object);
  wp_trace_object (object, "missing features to activate: 0x%x",
      self->missing);

  /* nothing to activate, we are done */
  if (self->missing == 0)
    return WP_TRANSITION_STEP_NONE;

  g_return_val_if_fail (WP_OBJECT_GET_CLASS (object)->activate_get_next_step,
      WP_TRANSITION_STEP_ERROR);

  step = WP_OBJECT_GET_CLASS (object)->activate_get_next_step (object, self,
      step, self->missing);

  g_return_val_if_fail (step == WP_TRANSITION_STEP_NONE ||
          WP_OBJECT_GET_CLASS (object)->activate_execute_step,
      WP_TRANSITION_STEP_ERROR);
  return step;
}

static void
wp_feature_activation_transition_execute_step (
    WpTransition * transition, guint step)
{
  WpFeatureActivationTransition *self =
      WP_FEATURE_ACTIVATION_TRANSITION (transition);
  WpObject *object = wp_transition_get_source_object (transition);

  WP_OBJECT_GET_CLASS (object)->activate_execute_step (object, self, step,
      self->missing);
}

static void
wp_feature_activation_transition_class_init (
    WpFeatureActivationTransitionClass * klass)
{
  WpTransitionClass *transition_class = (WpTransitionClass *) klass;

  transition_class->get_next_step =
      wp_feature_activation_transition_get_next_step;
  transition_class->execute_step =
      wp_feature_activation_transition_execute_step;
}

/*!
 * \brief Gets the features requested to be activated in this transition.
 * \ingroup wpfeatureactivationtransition
 * \param self the transition
 * \returns the features that were requested to be activated in this transition;
 *   this contains the features as they were passed in wp_object_activate() and
 *   therefore it may contain unsupported or already active features
 */
WpObjectFeatures
wp_feature_activation_transition_get_requested_features (
    WpFeatureActivationTransition * self)
{
  return GPOINTER_TO_UINT (wp_transition_get_data (WP_TRANSITION (self)));
}

/*! \defgroup wpobject WpObject */
/*!
 * \struct WpObject
 *
 * Base class for objects that have activatable features.
 *
 * \gproperties
 *
 * \gproperty{core, WpCore *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The WpCore associated with this object}
 *
 * \gproperty{active-features, guint (WpObjectFeatures), G_PARAM_READABLE,
 *   The active WpObjectFeatures on this proxy}
 *
 * \gproperty{supported-features, guint (WpObjectFeatures), G_PARAM_READABLE,
 *   The supported WpObjectFeatures on this proxy}
 */
typedef struct _WpObjectPrivate WpObjectPrivate;
struct _WpObjectPrivate
{
  /* properties */
  guint id;
  GWeakRef core;

  /* features state */
  WpObjectFeatures ft_active;
  GQueue *transitions; // element-type: WpFeatureActivationTransition*
  GSource *idle_advnc_source;
  GWeakRef ongoing_transition;
};

enum {
  PROP_0,
  PROP_ID,
  PROP_CORE,
  PROP_ACTIVE_FEATURES,
  PROP_SUPPORTED_FEATURES,
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpObject, wp_object, G_TYPE_OBJECT)

static guint
get_next_id ()
{
  static guint next_id = 0;
  g_atomic_int_inc (&next_id);
  return next_id;
}

static void
wp_object_init (WpObject * self)
{
  WpObjectPrivate *priv = wp_object_get_instance_private (self);

  priv->id = get_next_id ();
  g_weak_ref_init (&priv->core, NULL);
  priv->transitions = g_queue_new ();
  g_weak_ref_init (&priv->ongoing_transition, NULL);
}

static void
wp_object_dispose (GObject * object)
{
  WpObject *self = WP_OBJECT (object);
  WpObjectPrivate *priv = wp_object_get_instance_private (self);

  wp_trace_object (self, "dispose");

  wp_object_deactivate (self, WP_OBJECT_FEATURES_ALL);

  if (priv->idle_advnc_source)
    g_source_destroy (priv->idle_advnc_source);

  G_OBJECT_CLASS (wp_object_parent_class)->dispose (object);
}

static void
wp_object_finalize (GObject * object)
{
  WpObject *self = WP_OBJECT (object);
  WpObjectPrivate *priv = wp_object_get_instance_private (self);

  /* there should be no transitions, since transitions hold a ref on WpObject */
  g_warn_if_fail (g_queue_is_empty (priv->transitions));
  g_clear_pointer (&priv->transitions, g_queue_free);
  g_clear_pointer (&priv->idle_advnc_source, g_source_unref);
  g_weak_ref_clear (&priv->ongoing_transition);
  g_weak_ref_clear (&priv->core);

  /* everything must have been deactivated in dispose() */
  g_warn_if_fail (priv->ft_active == 0);

  G_OBJECT_CLASS (wp_object_parent_class)->finalize (object);
}

static void
wp_object_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpObject *self = WP_OBJECT (object);
  WpObjectPrivate *priv = wp_object_get_instance_private (self);

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&priv->core, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_object_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpObject *self = WP_OBJECT (object);
  WpObjectPrivate *priv = wp_object_get_instance_private (self);

  switch (property_id) {
  case PROP_ID:
    g_value_set_uint (value, priv->id);
    break;
  case PROP_CORE:
    g_value_take_object (value, wp_object_get_core (self));
    break;
  case PROP_ACTIVE_FEATURES:
    g_value_set_uint (value, priv->ft_active);
    break;
  case PROP_SUPPORTED_FEATURES:
    g_value_set_uint (value, wp_object_get_supported_features (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_object_class_init (WpObjectClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->dispose = wp_object_dispose;
  object_class->finalize = wp_object_finalize;
  object_class->get_property = wp_object_get_property;
  object_class->set_property = wp_object_set_property;

  g_object_class_install_property (object_class, PROP_ID,
      g_param_spec_uint ("id", "id",
          "The object unique id", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The WpCore", WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ACTIVE_FEATURES,
      g_param_spec_uint ("active-features", "active-features",
          "The active WpObjectFeatures on this proxy", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SUPPORTED_FEATURES,
      g_param_spec_uint ("supported-features", "supported-features",
          "The supported WpObjectFeatures on this proxy", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Gets the unique wireplumber Id of this object
 * \ingroup wpsessionitem
 * \param self the session item
 */
guint
wp_object_get_id (WpObject * self)
{
  g_return_val_if_fail (WP_IS_OBJECT (self), SPA_ID_INVALID);

  WpObjectPrivate *priv = wp_object_get_instance_private (self);
  return priv->id;
}

/*!
 * \brief Gets the core associated with this object.
 *
 * \ingroup wpobject
 * \param self the object
 * \returns (transfer full): the core associated with this object
 */
WpCore *
wp_object_get_core (WpObject * self)
{
  g_return_val_if_fail (WP_IS_OBJECT (self), NULL);

  WpObjectPrivate *priv = wp_object_get_instance_private (self);
  WpCore *core = g_weak_ref_get (&priv->core);
  if (!core && WP_IS_CORE (self))
    core = WP_CORE (g_object_ref (self));
  return core;
}

/*!
 * \brief Gets the active features of this object.
 * \ingroup wpobject
 * \param self the object
 * \returns A bitset containing the active features of this object
 */
WpObjectFeatures
wp_object_get_active_features (WpObject * self)
{
  g_return_val_if_fail (WP_IS_OBJECT (self), 0);

  WpObjectPrivate *priv = wp_object_get_instance_private (self);
  return priv->ft_active;
}

/*!
 * \brief Checks if the given features are active on this object.
 * \param self the object
 * \param features the features to check
 * \returns TRUE if all the given features are active on this object
 * \ingroup wpobject
 * \since 0.5.0
 */
gboolean
wp_object_test_active_features (WpObject * self, WpObjectFeatures features)
{
  g_return_val_if_fail (WP_IS_OBJECT (self), FALSE);

  WpObjectPrivate *priv = wp_object_get_instance_private (self);
  return (priv->ft_active & features) == features;
}

/*!
 * \brief Gets the supported features of this object.
 * \ingroup wpobject
 * \param self the object
 * \returns A bitset containing the supported features of this object;
 *   note that supported features may change at runtime
 */
WpObjectFeatures
wp_object_get_supported_features (WpObject * self)
{
  g_return_val_if_fail (WP_IS_OBJECT (self), 0);
  g_return_val_if_fail (WP_OBJECT_GET_CLASS (self)->get_supported_features, 0);

  return WP_OBJECT_GET_CLASS (self)->get_supported_features (self);
}

/*!
 * \brief Checks if the given features are supported on this object.
 * \param self the object
 * \param features the features to check
 * \returns TRUE if all the given features are supported on this object
 * \ingroup wpobject
 * \since 0.5.0
 */
gboolean
wp_object_test_supported_features (WpObject * self, WpObjectFeatures features)
{
  return (wp_object_get_supported_features (self) & features) == features;
}

static gboolean
wp_object_advance_transitions (WpObject * self)
{
  WpObjectPrivate *priv = wp_object_get_instance_private (self);
  g_autoptr (WpTransition) t = NULL;

  /* clear before advancing; a transition may need to schedule
     a new call to wp_object_advance_transitions() */
  g_clear_pointer (&priv->idle_advnc_source, g_source_unref);

  /* advance ongoing transition if any */
  t = g_weak_ref_get (&priv->ongoing_transition);
  if (t) {
    wp_transition_advance (t);
    if (!wp_transition_get_completed (t))
      return G_SOURCE_REMOVE;
  }

  /* set next transition and advance */
  if (!g_queue_is_empty (priv->transitions)) {
    WpTransition *next = g_queue_pop_head (priv->transitions);
    g_weak_ref_set (&priv->ongoing_transition, next);
    wp_transition_advance (next);
  }

  return G_SOURCE_REMOVE;
}

static void
on_transition_completed (WpTransition * transition, GParamSpec * param,
    WpObject * self)
{
  WpObjectPrivate *priv = wp_object_get_instance_private (self);

  /* abort activation if a transition failed */
  if (wp_transition_had_error (transition)) {
    wp_object_abort_activation (self, "a transition failed");
    return;
  }

  /* advance pending transitions */
  if (!g_queue_is_empty (priv->transitions) && !priv->idle_advnc_source) {
    g_autoptr (WpCore) core = wp_object_get_core (self);
    g_return_if_fail (core != NULL);

    wp_core_idle_add (core, &priv->idle_advnc_source,
        G_SOURCE_FUNC (wp_object_advance_transitions), g_object_ref (self),
        g_object_unref);
  }
}

/*!
 * \brief Callback version of wp_object_activate_closure()
 *
 * \ingroup wpobject
 * \param self the object
 * \param features the features to enable
 * \param cancellable (nullable): a cancellable for the async operation
 * \param callback (scope async): a function to call when activation is complete
 * \param user_data (closure): data for \a callback
 */
void
wp_object_activate (WpObject * self,
    WpObjectFeatures features, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  g_return_if_fail (WP_IS_OBJECT (self));

  GClosure *closure = g_cclosure_new (G_CALLBACK (callback), user_data, NULL);

  wp_object_activate_closure (self, features, cancellable, closure);
}

/*!
 * \brief Activates the requested \a features and invokes \a closure when this
 * is done. \a features may contain unsupported or already active features.
 * The operation will filter them and activate only ones that are supported and
 * inactive.
 *
 * If multiple calls to this method is done, the operations will be executed
 * one after the other to ensure features only get activated once.
 *
 * \note \a closure may be invoked in sync while this method is being called,
 * if there are no features to activate.
 *
 * \ingroup wpobject
 * \param self the object
 * \param features the features to enable
 * \param cancellable (nullable): a cancellable for the async operation
 * \param closure (transfer full): the closure to use when activation is completed
 */
void
wp_object_activate_closure (WpObject * self,
    WpObjectFeatures features, GCancellable * cancellable,
    GClosure *closure)
{
  g_return_if_fail (WP_IS_OBJECT (self));

  WpObjectPrivate *priv = wp_object_get_instance_private (self);
  g_autoptr (WpCore) core = wp_object_get_core (self);

  g_return_if_fail (core != NULL);

  WpTransition *transition = wp_transition_new_closure (
      WP_TYPE_FEATURE_ACTIVATION_TRANSITION, self, cancellable, closure);
  wp_transition_set_source_tag (transition, wp_object_activate);
  wp_transition_set_data (transition, GUINT_TO_POINTER (features), NULL);
  g_signal_connect_object (transition, "notify::completed",
      G_CALLBACK (on_transition_completed), self, 0);

  g_queue_push_tail (priv->transitions, transition);

  if (!priv->idle_advnc_source) {
    wp_core_idle_add (core, &priv->idle_advnc_source,
        G_SOURCE_FUNC (wp_object_advance_transitions), g_object_ref (self),
        g_object_unref);
  }
}

/*!
 * \brief Finishes the async operation that was started with wp_object_activate()
 *
 * \ingroup wpobject
 * \param self the object
 * \param res the async operation result
 * \param error (out) (optional): the error of the operation, if any
 * \returns TRUE if the requested features were activated,
 *   FALSE if there was an error
 */
gboolean
wp_object_activate_finish (WpObject * self, GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_OBJECT (self), FALSE);
  g_return_val_if_fail (
      g_async_result_is_tagged (res, wp_object_activate), FALSE);
  return wp_transition_finish (res, error);
}

/*!
 * \brief Deactivates the given \a features, leaving the object in the state
 * it was before they were enabled.
 *
 * This is seldom needed to call manually, but it can be used to save
 * resources if some features are no longer needed.
 *
 * \ingroup wpobject
 * \param self the object
 * \param features the features to deactivate
 */
void
wp_object_deactivate (WpObject * self, WpObjectFeatures features)
{
  g_return_if_fail (WP_IS_OBJECT (self));
  g_return_if_fail (WP_OBJECT_GET_CLASS (self)->deactivate);

  WpObjectPrivate *priv = wp_object_get_instance_private (self);
  WP_OBJECT_GET_CLASS (self)->deactivate (self, features & priv->ft_active);
}

/*!
 * \brief Aborts the current object activation by returning a transition error
 * if any transitions are pending.
 *
 * This is usually used to stop any pending activation if an error happened.
 *
 * \ingroup wpobject
 * \param self the object
 * \param msg the message used in the transition error
 * \since 0.4.6
 */
void
wp_object_abort_activation (WpObject * self, const gchar *msg)
{
  WpObjectPrivate *priv;
  g_autoptr (WpTransition) t = NULL;

  g_return_if_fail (WP_IS_OBJECT (self));

  priv =  wp_object_get_instance_private (self);

  g_clear_pointer (&priv->idle_advnc_source, g_source_unref);

  /* abort ongoing transition if any */
  t = g_weak_ref_get (&priv->ongoing_transition);
  if (t && !wp_transition_get_completed (t)) {
    wp_transition_return_error (t, g_error_new (WP_DOMAIN_LIBRARY,
            WP_LIBRARY_ERROR_OPERATION_FAILED,
            "Object activation aborted: %s", msg));
    return;
  }

  /* recursively abort the queued transitions if any */
  if (!g_queue_is_empty (priv->transitions)) {
    WpTransition *next = g_queue_pop_head (priv->transitions);
    g_weak_ref_set (&priv->ongoing_transition, next);
    wp_object_abort_activation (self, msg);
  }
}

/*!
 * \brief Allows subclasses to update the currently active features.
 *
 * \a activated should contain new features and \a deactivated
 * should contain features that were just deactivated.
 * Calling this method also advances the activation transitions.
 *
 * \remark Private method to be called by subclasses only.
 *
 * \ingroup wpobject
 * \param self the object
 * \param activated the features that were activated, or 0
 * \param deactivated the features that were deactivated, or 0
 */
void
wp_object_update_features (WpObject * self, WpObjectFeatures activated,
    WpObjectFeatures deactivated)
{
  g_return_if_fail (WP_IS_OBJECT (self));

  WpObjectPrivate *priv = wp_object_get_instance_private (self);
  guint old_ft = priv->ft_active;
  g_autoptr (WpTransition) t = NULL;

  priv->ft_active |= activated;
  priv->ft_active &= ~deactivated;

  if (priv->ft_active != old_ft) {
    wp_debug_object (self, "features changed 0x%x -> 0x%x", old_ft,
        priv->ft_active);
    g_object_notify (G_OBJECT (self), "active-features");
  }

  t = g_weak_ref_get (&priv->ongoing_transition);
  if ((t || !g_queue_is_empty (priv->transitions)) && !priv->idle_advnc_source) {
    g_autoptr (WpCore) core = wp_object_get_core (self);
    g_return_if_fail (core != NULL);

    wp_core_idle_add (core, &priv->idle_advnc_source,
        G_SOURCE_FUNC (wp_object_advance_transitions), g_object_ref (self),
        g_object_unref);
  }
}
