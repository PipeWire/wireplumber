/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpSessionItem
 * @title: Session Items
 */

#include "session-item.h"
#include "wpenums.h"

typedef struct _WpSessionItemPrivate WpSessionItemPrivate;
struct _WpSessionItemPrivate
{
  GWeakRef session;
  guint32 flags;
};

enum {
  SIGNAL_FLAGS_CHANGED,
  N_SIGNALS
};

guint32 signals[N_SIGNALS] = {0};

/**
 * WpSessionItem:
 */
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpSessionItem, wp_session_item, G_TYPE_OBJECT)

static void
wp_session_item_init (WpSessionItem * self)
{
  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  g_weak_ref_init (&priv->session, NULL);
}

static void
wp_session_item_dispose (GObject * object)
{
  WpSessionItem * self = WP_SESSION_ITEM (object);

  wp_session_item_reset (self);

  G_OBJECT_CLASS (wp_session_item_parent_class)->dispose (object);
}

static void
wp_session_item_finalize (GObject * object)
{
  WpSessionItem * self = WP_SESSION_ITEM (object);
  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  g_weak_ref_clear (&priv->session);

  G_OBJECT_CLASS (wp_session_item_parent_class)->finalize (object);
}

static void
wp_session_item_default_reset (WpSessionItem * self)
{
  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  priv->flags &= ~(WP_SI_FLAG_ACTIVE | WP_SI_FLAG_IN_ERROR);
  g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
}

static void
wp_session_item_class_init (WpSessionItemClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->dispose = wp_session_item_dispose;
  object_class->finalize = wp_session_item_finalize;

  klass->reset = wp_session_item_default_reset;

  /**
   * WpSessionItem::flags-changed:
   * @self: the session item
   * @flags: the current flags
   */
  signals[SIGNAL_FLAGS_CHANGED] = g_signal_new (
      "flags-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_SI_FLAGS);
}

/**
 * wp_session_item_get_session:
 * @self: the session item
 *
 * Returns: (nullable) (transfer full): the session that owns this item, or
 *   %NULL if this item is not part of a session
 */
WpSession *
wp_session_item_get_session (WpSessionItem * self)
{
  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), NULL);

  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);
  return g_weak_ref_get (&priv->session);
}

/**
 * wp_session_item_get_flags:
 * @self: the session item
 *
 * Returns: the item's flags
 */
WpSiFlags
wp_session_item_get_flags (WpSessionItem * self)
{
  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), 0);

  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);
  return priv->flags;
}

/**
 * wp_session_item_set_flag:
 * @self: the session item
 * @flag: the flag to set
 *
 * Sets the specified @flag on this item.
 *
 * Note that bits 1-8 cannot be set using this function, they can only
 * be changed internally.
 */
void
wp_session_item_set_flag (WpSessionItem * self, WpSiFlags flag)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  /* mask to make sure we are not changing an immutable flag */
  flag &= ~((1<<8) - 1);
  if (flag != 0) {
    priv->flags |= flag;
    g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
  }
}

/**
 * wp_session_item_clear_flag:
 * @self: the session item
 * @flag: the flag to clear
 *
 * Clears the specified @flag from this item.
 *
 * Note that bits 1-8 cannot be cleared using this function, they can only
 * be changed internally.
 */
void
wp_session_item_clear_flag (WpSessionItem * self, WpSiFlags flag)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  /* mask to make sure we are not changing an immutable flag */
  flag &= ~((1<<8) - 1);
  if (flag != 0) {
    priv->flags &= ~flag;
    g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
  }
}

/**
 * wp_session_item_configure: (virtual configure)
 * @self: the session item
 * @args: (transfer none): the configuration options to set
 *   (`a{sv}` dictionary, mapping option names to values)
 *
 * Returns: %TRUE on success, %FALSE if the options could not be set
 */
gboolean
wp_session_item_configure (WpSessionItem * self, GVariant * args)
{
  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), FALSE);
  g_return_val_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->configure,
      FALSE);
  g_return_val_if_fail (g_variant_is_of_type (args, G_VARIANT_TYPE ("a{sv}")),
      FALSE);

  return WP_SESSION_ITEM_GET_CLASS (self)->configure (self, args);
}

/**
 * wp_session_item_get_configuration: (virtual get_configuration)
 * @self: the session item
 *
 * Returns: (transfer floating): the active configuration, as a `a{sv}` dictionary
 */
GVariant *
wp_session_item_get_configuration (WpSessionItem * self)
{
  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), NULL);
  g_return_val_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->get_configuration,
      NULL);

  return WP_SESSION_ITEM_GET_CLASS (self)->get_configuration (self);
}

typedef WpTransition WpSiTransition;
typedef WpTransitionClass WpSiTransitionClass;

G_DEFINE_TYPE (WpSiTransition, wp_si_transition, WP_TYPE_TRANSITION)

static void
wp_si_transition_init (WpSiTransition * transition) {}

static guint
wp_si_transition_get_next_step (WpTransition * transition, guint step)
{
  WpSessionItem *item = wp_transition_get_source_object (transition);
  g_return_val_if_fail (
      WP_SESSION_ITEM_GET_CLASS (item)->get_next_step,
      WP_TRANSITION_STEP_ERROR);
  g_return_val_if_fail (
      WP_SESSION_ITEM_GET_CLASS (item)->execute_step,
      WP_TRANSITION_STEP_ERROR);

  return WP_SESSION_ITEM_GET_CLASS (item)->get_next_step (item,
      transition, step);
}

static void
wp_si_transition_execute_step (WpTransition * transition, guint step)
{
  WpSessionItem *item = wp_transition_get_source_object (transition);
  WP_SESSION_ITEM_GET_CLASS (item)->execute_step (item, transition, step);
}

static void
wp_si_transition_class_init (WpSiTransitionClass * klass)
{
  WpTransitionClass *transition_class = (WpTransitionClass *) klass;

  transition_class->get_next_step = wp_si_transition_get_next_step;
  transition_class->execute_step = wp_si_transition_execute_step;
}

static void
on_transition_completed (WpTransition * transition, GParamSpec * pspec,
    WpSessionItem * self)
{
  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  if (wp_transition_had_error (transition))
    priv->flags |= WP_SI_FLAG_IN_ERROR;
  else
    priv->flags |= WP_SI_FLAG_ACTIVE;

  priv->flags &= ~WP_SI_FLAG_ACTIVATING;
  g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
}

/**
 * wp_session_item_activate:
 * @self: the session item
 * @callback: (scope async): a callback to call when activation is finished
 * @callback_data: (closure): data passed to @callback
 *
 * Activates the item asynchronously. This internally starts a #WpTransition
 * that calls into #WpSessionItemClass.get_next_step() and
 * #WpSessionItemClass.execute_step() to advance.
 *
 * You can use wp_transition_finish() in the @callback to figure out the
 * result of this operation.
 *
 * Normally this function is called internally by the session; there is no need
 * to activate an item externally, except for unit testing purposes.
 */
void
wp_session_item_activate (WpSessionItem * self,
    GAsyncReadyCallback callback,
    gpointer callback_data)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  g_return_if_fail (!(priv->flags & (WP_SI_FLAG_ACTIVATING | WP_SI_FLAG_ACTIVE)));

  /* TODO: add a way to cancel the transition if reset() is called in the meantime */
  WpTransition *transition = wp_transition_new (wp_si_transition_get_type (),
      self, NULL, callback, callback_data);
  wp_transition_set_source_tag (transition, wp_session_item_activate);
  g_signal_connect (transition, "notify::completed",
      (GCallback) on_transition_completed, self);

  priv->flags |= WP_SI_FLAG_ACTIVATING;
  g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);

  wp_transition_advance (transition);
}

/**
 * wp_session_item_reset: (virtual reset)
 * @self: the session item
 *
 * Resets the state of the item, deactivating it, and possibly
 * resetting configuration options as well.
 */
void
wp_session_item_reset (WpSessionItem * self)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));
  g_return_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->reset);

  WP_SESSION_ITEM_GET_CLASS (self)->reset (self);
}
