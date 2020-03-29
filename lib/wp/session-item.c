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
#include "private.h"
#include "error.h"
#include "wpenums.h"

struct _WpSiTransition
{
  WpTransition parent;
  guint (*get_next_step) (WpSessionItem * self, WpTransition * transition,
      guint step);
  void (*execute_step) (WpSessionItem * self, WpTransition * transition,
      guint step);
};

G_DECLARE_FINAL_TYPE (WpSiTransition, wp_si_transition,
                      WP, SI_TRANSITION, WpTransition);
G_DEFINE_TYPE (WpSiTransition, wp_si_transition, WP_TYPE_TRANSITION)

static void
wp_si_transition_init (WpSiTransition * transition) {}

static guint
wp_si_transition_get_next_step (WpTransition * transition, guint step)
{
  WpSiTransition *self = WP_SI_TRANSITION (transition);
  WpSessionItem *item = wp_transition_get_source_object (transition);

  g_return_val_if_fail (self->get_next_step, WP_TRANSITION_STEP_ERROR);

  step = self->get_next_step (item, transition, step);

  g_return_val_if_fail (step == WP_TRANSITION_STEP_NONE || self->execute_step,
      WP_TRANSITION_STEP_ERROR);
  return step;
}

static void
wp_si_transition_execute_step (WpTransition * transition, guint step)
{
  WpSiTransition *self = WP_SI_TRANSITION (transition);
  WpSessionItem *item = wp_transition_get_source_object (transition);
  self->execute_step (item, transition, step);
}

static void
wp_si_transition_class_init (WpSiTransitionClass * klass)
{
  WpTransitionClass *transition_class = (WpTransitionClass *) klass;

  transition_class->get_next_step = wp_si_transition_get_next_step;
  transition_class->execute_step = wp_si_transition_execute_step;
}


typedef struct _WpSessionItemPrivate WpSessionItemPrivate;
struct _WpSessionItemPrivate
{
  GWeakRef session;
  guint32 flags;

  WpImplEndpoint *impl_endpoint;
  GHashTable *impl_streams;
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
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);

  g_weak_ref_clear (&priv->session);

  G_OBJECT_CLASS (wp_session_item_parent_class)->finalize (object);
}

static guint
wp_session_item_default_get_next_step (WpSessionItem * self,
    WpTransition * transition, guint step)
{
  /* the default implementation just activates instantly,
     without taking any action */
  return WP_TRANSITION_STEP_NONE;
}

static void
wp_session_item_default_execute_step (WpSessionItem * self,
    WpTransition * transition, guint step)
{
  switch (step) {
  case WP_TRANSITION_STEP_NONE:
    break;
  case WP_TRANSITION_STEP_ERROR:
    wp_session_item_reset (self);
    break;
  default:
    g_return_if_reached ();
  }
}

static void
wp_session_item_default_reset (WpSessionItem * self)
{
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);

  wp_session_item_unexport (self);

  priv->flags &= ~(WP_SI_FLAG_ACTIVE | WP_SI_FLAG_IN_ERROR);
  g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
}

enum {
  EXPORT_STEP_ENDPOINT = WP_TRANSITION_STEP_CUSTOM_START,
  EXPORT_STEP_STREAMS,
  EXPORT_STEP_LINK,
  EXPORT_STEP_FINISH,
};

static guint
default_export_get_next_step (WpSessionItem * self, WpTransition * transition,
    guint step)
{
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);

  switch (step) {
  case WP_TRANSITION_STEP_NONE:
    if (WP_IS_SI_ENDPOINT (self)) {
      priv->flags |= WP_SI_FLAG_EXPORTING;
      g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
      return EXPORT_STEP_ENDPOINT;
    }
    else if (WP_IS_SI_LINK (self)) {
      priv->flags |= WP_SI_FLAG_EXPORTING;
      g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
      return EXPORT_STEP_LINK;
    }
    else {
      wp_transition_return_error (transition, g_error_new (
              WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
              "Cannot export WpSessionItem of unknown type (%s:%p)",
              G_OBJECT_TYPE_NAME (self), self));
      return WP_TRANSITION_STEP_ERROR;
    }

  case EXPORT_STEP_ENDPOINT:
    return EXPORT_STEP_STREAMS;

  case EXPORT_STEP_STREAMS:
    g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), WP_TRANSITION_STEP_ERROR);
    g_return_val_if_fail (priv->impl_streams, WP_TRANSITION_STEP_ERROR);

    /* go to next step only when all impl proxies are augmented */
    if (g_hash_table_size (priv->impl_streams) ==
        wp_si_endpoint_get_n_streams (WP_SI_ENDPOINT (self)))
      return EXPORT_STEP_FINISH;
    else
      return step;

  case EXPORT_STEP_FINISH:
    return WP_TRANSITION_STEP_NONE;

  default:
    return WP_TRANSITION_STEP_ERROR;
  }
}

static void
on_export_proxy_augmented (WpProxy * proxy, GAsyncResult * res, gpointer data)
{
  WpTransition *transition = WP_TRANSITION (data);
  WpSessionItem *self = wp_transition_get_source_object (transition);
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);
  g_autoptr (GError) error = NULL;

  if (!wp_proxy_augment_finish (proxy, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  if (WP_IS_IMPL_ENDPOINT_STREAM (proxy)) {
    g_autoptr (WpSiStream) si_stream = NULL;

    g_object_get (proxy, "item", &si_stream, NULL);
    g_return_if_fail (si_stream != NULL);

    g_hash_table_insert (priv->impl_streams, si_stream, g_object_ref (proxy));
  }

  wp_transition_advance (transition);
}

static void
default_export_execute_step (WpSessionItem * self, WpTransition * transition,
    guint step)
{
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);
  g_autoptr (WpSession) session = g_weak_ref_get (&priv->session);
  g_autoptr (WpCore) core = wp_proxy_get_core (WP_PROXY (session));

  switch (step) {
  case EXPORT_STEP_ENDPOINT:
    priv->impl_endpoint = wp_impl_endpoint_new (core, WP_SI_ENDPOINT (self));

    wp_proxy_augment (WP_PROXY (priv->impl_endpoint),
        WP_PROXY_FEATURES_STANDARD, NULL,
        (GAsyncReadyCallback) on_export_proxy_augmented,
        transition);
    break;

  case EXPORT_STEP_STREAMS: {
    guint i, n_streams;

    priv->impl_streams = g_hash_table_new_full (g_direct_hash, g_direct_equal,
        NULL, g_object_unref);

    n_streams = wp_si_endpoint_get_n_streams (WP_SI_ENDPOINT (self));
    for (i = 0; i < n_streams; i++) {
      WpSiStream *stream = wp_si_endpoint_get_stream (WP_SI_ENDPOINT (self), i);
      WpImplEndpointStream *impl_stream =
          wp_impl_endpoint_stream_new (core, stream);

      wp_proxy_augment (WP_PROXY (impl_stream),
          WP_PROXY_FEATURES_STANDARD, NULL,
          (GAsyncReadyCallback) on_export_proxy_augmented,
          transition);

      /* the augment task holds a ref; object will be added to
         priv->impl_streams when augmented */
      g_object_unref (impl_stream);
    }
    break;
  }
  case EXPORT_STEP_LINK:
    /* TODO implement me */
    break;

  case EXPORT_STEP_FINISH:
    priv->flags &= ~WP_SI_FLAG_EXPORTING;
    priv->flags |= WP_SI_FLAG_EXPORTED;
    g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
    wp_transition_advance (transition);
    break;

  case WP_TRANSITION_STEP_ERROR:
    g_clear_pointer (&priv->impl_streams, g_hash_table_unref);
    g_clear_object (&priv->impl_endpoint);
    g_weak_ref_set (&priv->session, NULL);

    if (priv->flags & WP_SI_FLAG_EXPORTING) {
      priv->flags &= ~WP_SI_FLAG_EXPORTING;
      g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
    }
    break;
  }
}

static void
wp_session_item_default_export (WpSessionItem * self,
      WpSession * session, GCancellable * cancellable,
      GAsyncReadyCallback callback, gpointer callback_data)
{
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);
  WpTransition *transition;

  g_return_if_fail (priv->flags & WP_SI_FLAG_ACTIVE);
  g_return_if_fail (!(priv->flags & (WP_SI_FLAG_EXPORTING | WP_SI_FLAG_EXPORTED)));

  g_weak_ref_set (&priv->session, session);

  transition = wp_transition_new (wp_si_transition_get_type (),
      self, cancellable, callback, callback_data);
  wp_transition_set_source_tag (transition, wp_session_item_default_export);

  WP_SI_TRANSITION (transition)->get_next_step = default_export_get_next_step;
  WP_SI_TRANSITION (transition)->execute_step = default_export_execute_step;
  wp_transition_advance (transition);
}

static gboolean
wp_session_item_default_export_finish (WpSessionItem * self,
    GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (
      g_async_result_is_tagged (res, wp_session_item_default_export), FALSE);

  return wp_transition_finish (res, error);
}

static void
wp_session_item_default_unexport (WpSessionItem * self)
{
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);

  //TODO cancel job if EXPORTING

  g_clear_pointer (&priv->impl_streams, g_hash_table_unref);
  g_clear_object (&priv->impl_endpoint);
  g_weak_ref_set (&priv->session, NULL);

  priv->flags &= ~(WP_SI_FLAG_EXPORTING | WP_SI_FLAG_EXPORTED);
  g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
}

static void
wp_session_item_class_init (WpSessionItemClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->dispose = wp_session_item_dispose;
  object_class->finalize = wp_session_item_finalize;

  klass->get_next_step = wp_session_item_default_get_next_step;
  klass->execute_step = wp_session_item_default_execute_step;
  klass->reset = wp_session_item_default_reset;
  klass->export = wp_session_item_default_export;
  klass->export_finish = wp_session_item_default_export_finish;
  klass->unexport = wp_session_item_default_unexport;

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
 * You can use wp_session_item_activate_finish() in the @callback to figure out
 * the result of this operation.
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

  WP_SI_TRANSITION (transition)->get_next_step =
      WP_SESSION_ITEM_GET_CLASS (self)->get_next_step;
  WP_SI_TRANSITION (transition)->execute_step =
      WP_SESSION_ITEM_GET_CLASS (self)->execute_step;
  wp_transition_advance (transition);
}

/**
 * wp_session_item_activate_finish:
 * @self: the session item
 * @res: the async operation result
 * @error: (out) (optional): the error of the operation, if any
 *
 * Returns: %TRUE if the item is now activateed, %FALSE if there was an error
 */
gboolean
wp_session_item_activate_finish (WpSessionItem * self, GAsyncResult * res,
    GError ** error)
{
  g_return_val_if_fail (
      g_async_result_is_tagged (res, wp_session_item_activate), FALSE);
  return wp_transition_finish (res, error);
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

/**
 * wp_session_item_export: (virtual export)
 * @self: the session item
 * @session: the session on which to export this item
 * @callback: (scope async): a callback to call when exporting is finished
 * @callback_data: (closure): data passed to @callback
 *
 * Exports this item asynchronously on PipeWire, making it part of the
 * specified @session.
 *
 * Exporting only makes sense for endpoints (items that implement #WpSiEndpoint)
 * and endpoint links (items that implement #WpSiLink). On other items the
 * default implementation will immediately call the @callback, reporting error.
 */
void
wp_session_item_export (WpSessionItem * self, WpSession * session,
    GAsyncReadyCallback callback, gpointer callback_data)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));
  g_return_if_fail (WP_IS_SESSION (session));
  g_return_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->export);

  WP_SESSION_ITEM_GET_CLASS (self)->export (self, session, NULL,
      callback, callback_data);
}

/**
 * wp_session_item_export_finish: (virtual export_finish)
 * @self: the session item
 * @res: the async operation result
 * @error: (out) (optional): the error of the operation, if any
 *
 * Returns: %TRUE if the item is now exported, %FALSE if there was an error
 */
gboolean
wp_session_item_export_finish (WpSessionItem * self, GAsyncResult * res,
    GError ** error)
{
  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), FALSE);
  g_return_val_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->export_finish, FALSE);

  return WP_SESSION_ITEM_GET_CLASS (self)->export_finish (self, res, error);
}

/**
 * wp_session_item_unexport: (virtual unexport)
 * @self: the session item
 *
 * Reverses the effects of a previous call to wp_session_item_export().
 * This means that after this method is called:
 *  - The item is no longer exported on PipeWire
 *  - The item is no longer associated with a session
 *  - If an export operation was in progress, it is cancelled.
 *
 * If the item was not exported, this method does nothing.
 */
void
wp_session_item_unexport (WpSessionItem * self)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));
  g_return_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->unexport);

  WP_SESSION_ITEM_GET_CLASS (self)->unexport (self);
}
