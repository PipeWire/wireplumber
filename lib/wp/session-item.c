/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: session-item
 * @title: Session Items
 */

#define G_LOG_DOMAIN "wp-si"

#include "session-item.h"
#include "core.h"
#include "debug.h"
#include "error.h"
#include "wpenums.h"
#include "private/impl-endpoint.h"

#include <spa/utils/defs.h>

struct _WpSiTransition
{
  WpTransition parent;
  guint (*get_next_step) (WpSessionItem * self, WpTransition * transition,
      guint step);
  void (*execute_step) (WpSessionItem * self, WpTransition * transition,
      guint step);
  void (*rollback) (WpSessionItem * self);
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

  if (step != WP_TRANSITION_STEP_ERROR)
    self->execute_step (item, transition, step);
  else if (self->rollback)
    self->rollback (item);
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
  GWeakRef parent;

  union {
    WpProxy *impl_proxy;
    WpImplEndpoint *impl_endpoint;
    WpImplEndpointLink *impl_link;
  };
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
  g_weak_ref_init (&priv->parent, NULL);
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
  g_weak_ref_clear (&priv->parent);

  G_OBJECT_CLASS (wp_session_item_parent_class)->finalize (object);
}

static void
wp_session_item_default_reset (WpSessionItem * self)
{
  wp_session_item_unexport (self);
  wp_session_item_deactivate (self);
}

static gpointer
wp_session_item_default_get_associated_proxy (WpSessionItem * self,
    GType proxy_type)
{
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);
  gpointer ret = NULL;

  if (proxy_type == WP_TYPE_SESSION) {
    ret = g_weak_ref_get (&priv->session);
  }
  else if (proxy_type == WP_TYPE_ENDPOINT) {
    if (priv->impl_proxy && WP_IS_ENDPOINT (priv->impl_proxy))
      ret = g_object_ref (priv->impl_proxy);
  }
  else if (proxy_type == WP_TYPE_ENDPOINT_LINK) {
    if (priv->impl_proxy && WP_IS_ENDPOINT_LINK (priv->impl_proxy))
      ret = g_object_ref (priv->impl_proxy);
  }

  wp_trace_object (self, "associated %s: " WP_OBJECT_FORMAT,
      g_type_name (proxy_type), WP_OBJECT_ARGS (ret));

  return ret;
}

static guint
wp_session_item_default_activate_get_next_step (WpSessionItem * self,
    WpTransition * transition, guint step)
{
  /* the default implementation just activates instantly,
     without taking any action */
  return WP_TRANSITION_STEP_NONE;
}

enum {
  EXPORT_STEP_ENDPOINT = WP_TRANSITION_STEP_CUSTOM_START,
  EXPORT_STEP_LINK,
  EXPORT_STEP_CONNECT_DESTROYED,
};

static guint
wp_session_item_default_export_get_next_step (WpSessionItem * self,
    WpTransition * transition, guint step)
{
  switch (step) {
  case WP_TRANSITION_STEP_NONE:
    if (WP_IS_SI_ENDPOINT (self))
      return EXPORT_STEP_ENDPOINT;
    else if (WP_IS_SI_LINK (self))
      return EXPORT_STEP_LINK;
    else {
      wp_transition_return_error (transition, g_error_new (
              WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
              "Cannot export WpSessionItem of unknown type " WP_OBJECT_FORMAT,
              WP_OBJECT_ARGS (self)));
      return WP_TRANSITION_STEP_ERROR;
    }

  case EXPORT_STEP_ENDPOINT:
    g_return_val_if_fail (WP_IS_SI_ENDPOINT (self), WP_TRANSITION_STEP_ERROR);
    return WP_TRANSITION_STEP_NONE;

  case EXPORT_STEP_LINK:
    g_return_val_if_fail (WP_IS_SI_LINK (self), WP_TRANSITION_STEP_ERROR);
    return EXPORT_STEP_CONNECT_DESTROYED;

  case EXPORT_STEP_CONNECT_DESTROYED:
    return WP_TRANSITION_STEP_NONE;

  default:
    return WP_TRANSITION_STEP_ERROR;
  }
}

static void
on_export_proxy_activated (WpObject * proxy, GAsyncResult * res, gpointer data)
{
  WpTransition *transition = WP_TRANSITION (data);
  WpSessionItem *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (proxy, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_debug_object (self, "export proxy " WP_OBJECT_FORMAT " activated",
      WP_OBJECT_ARGS (proxy));

  wp_transition_advance (transition);
}

static gboolean
on_export_proxy_destroyed_deferred (WpSessionItem * self)
{
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);

  g_return_val_if_fail (priv->impl_proxy, G_SOURCE_REMOVE);
  g_return_val_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->export_rollback,
      G_SOURCE_REMOVE);

  wp_info_object (self, "destroying " WP_OBJECT_FORMAT
      " upon request by the server", WP_OBJECT_ARGS (priv->impl_proxy));

  WP_SESSION_ITEM_GET_CLASS (self)->export_rollback (self);

  priv->flags |= WP_SI_FLAG_EXPORT_ERROR;
  g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);

  return G_SOURCE_REMOVE;
}

static void
on_export_proxy_destroyed (WpObject * proxy, gpointer data)
{
  WpSessionItem *self = WP_SESSION_ITEM (data);
  g_autoptr (WpCore) core = wp_object_get_core (proxy);

  if (core)
    wp_core_idle_add_closure (core, NULL, g_cclosure_new_object (
          G_CALLBACK (on_export_proxy_destroyed_deferred), G_OBJECT (self)));
}

static void
wp_session_item_default_export_execute_step (WpSessionItem * self,
    WpTransition * transition, guint step)
{
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);
  g_autoptr (WpSession) session = g_weak_ref_get (&priv->session);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (session));

  switch (step) {
  case EXPORT_STEP_ENDPOINT:
    priv->impl_endpoint = wp_impl_endpoint_new (core, WP_SI_ENDPOINT (self));

    wp_object_activate (WP_OBJECT (priv->impl_endpoint),
        WP_PIPEWIRE_OBJECT_FEATURES_ALL, NULL,
        (GAsyncReadyCallback) on_export_proxy_activated,
        transition);
    break;

  case EXPORT_STEP_LINK:
    priv->impl_link = wp_impl_endpoint_link_new (core, WP_SI_LINK (self));

    wp_object_activate (WP_OBJECT (priv->impl_link),
        WP_OBJECT_FEATURES_ALL, NULL,
        (GAsyncReadyCallback) on_export_proxy_activated,
        transition);
    break;

  case EXPORT_STEP_CONNECT_DESTROYED:
    g_signal_connect_object (priv->impl_proxy, "pw-proxy-destroyed",
        G_CALLBACK (on_export_proxy_destroyed), self, 0);
    wp_transition_advance (transition);
    break;

  default:
    g_return_if_reached ();
  }
}

static void
wp_session_item_default_export_rollback (WpSessionItem * self)
{
  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);
  if (priv->impl_proxy)
    g_signal_handlers_disconnect_by_data (priv->impl_proxy, self);
  g_clear_object (&priv->impl_proxy);
  g_weak_ref_set (&priv->session, NULL);
}

static void
wp_session_item_class_init (WpSessionItemClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->dispose = wp_session_item_dispose;
  object_class->finalize = wp_session_item_finalize;

  klass->reset = wp_session_item_default_reset;
  klass->get_associated_proxy = wp_session_item_default_get_associated_proxy;
  klass->activate_get_next_step = wp_session_item_default_activate_get_next_step;
  klass->export_get_next_step = wp_session_item_default_export_get_next_step;
  klass->export_execute_step = wp_session_item_default_export_execute_step;
  klass->export_rollback = wp_session_item_default_export_rollback;

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
 * wp_session_item_reset: (virtual reset)
 * @self: the session item
 *
 * Resets the state of the item, deactivating it, unexporting it and
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
 * wp_session_item_get_parent:
 * @self: the session item
 *
 * Gets the item's parent, which is the #WpSessionBin this item has been added
 * to, or NULL if the item does not belong to a session bin.
 *
 * Returns: (nullable) (transfer full): the item's parent.
 */
WpSessionItem *
wp_session_item_get_parent (WpSessionItem * self)
{
  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), NULL);

  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);
  return g_weak_ref_get (&priv->parent);
}

/**
 * wp_session_item_set_parent:
 * @self: the session item
 * @parent: (transfer none): the parent item
 *
 * Private API.
 * Sets the item's parent; used internally by #WpSessionBin.
 */
void
wp_session_item_set_parent (WpSessionItem *self, WpSessionItem *parent)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  g_weak_ref_set (&priv->parent, parent);
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
 * wp_session_item_get_associated_proxy: (virtual get_associated_proxy)
 * @self: the session item
 * @proxy_type: a #WpProxy subclass #GType
 *
 * An associated proxy is a #WpProxy subclass instance that is somehow related
 * to this item. For example:
 *  - An exported #WpSiEndpoint should have at least:
 *      - an associated #WpEndpoint
 *      - an associated #WpSession
 *  - In cases where the item wraps a single PipeWire node, it should also
 *    have an associated #WpNode
 *
 * Returns: (nullable) (transfer full) (type WpProxy): the associated proxy
 *   of the specified @proxy_type, or %NULL if there is no association to
 *   such a proxy
 */
gpointer
wp_session_item_get_associated_proxy (WpSessionItem * self, GType proxy_type)
{
  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), NULL);
  g_return_val_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->get_associated_proxy,
      NULL);
  g_return_val_if_fail (g_type_is_a (proxy_type, WP_TYPE_PROXY), NULL);

  return WP_SESSION_ITEM_GET_CLASS (self)->get_associated_proxy (self, proxy_type);
}

/**
 * wp_session_item_get_associated_proxy_id:
 * @self: the session item
 * @proxy_type: a #WpProxy subclass #GType
 *
 * Returns: the bound id of the associated proxy of the specified @proxy_type,
 *   or `SPA_ID_INVALID` if there is no association to such a proxy
 */
guint32
wp_session_item_get_associated_proxy_id (WpSessionItem * self, GType proxy_type)
{
  g_autoptr (WpProxy) proxy = wp_session_item_get_associated_proxy (self,
      proxy_type);
  if (!proxy)
    return SPA_ID_INVALID;

  return wp_proxy_get_bound_id (proxy);
}

/**
 * wp_session_item_configure: (virtual configure)
 * @self: the session item
 * @args: (transfer floating): the configuration options to set
 *   (`a{sv}` dictionary, mapping option names to values)
 *
 * Returns: %TRUE on success, %FALSE if the options could not be set
 */
gboolean
wp_session_item_configure (WpSessionItem * self, GVariant * args)
{
  g_autoptr (GVariant) args_ref = g_variant_ref_sink (args);

  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), FALSE);
  g_return_val_if_fail (WP_SESSION_ITEM_GET_CLASS (self)->configure,
      FALSE);
  g_return_val_if_fail (g_variant_is_of_type (args, G_VARIANT_TYPE_VARDICT),
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

/* clear the in progress flag before calling the callback, so that
   it's possible to call wp_session_item_export from within the callback */
static void
on_activate_transition_pre_completed (gpointer data, GClosure *closure)
{
  WpTransition *transition = WP_TRANSITION (data);
  WpSessionItem *self = wp_transition_get_source_object (transition);
  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  priv->flags &= ~WP_SI_FLAG_ACTIVATING;
}

static void
on_activate_transition_post_completed (gpointer data, GClosure *closure)
{
  WpTransition *transition = WP_TRANSITION (data);
  WpSessionItem *self = wp_transition_get_source_object (transition);
  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  priv->flags |= wp_transition_had_error (transition) ?
      WP_SI_FLAG_ACTIVATE_ERROR : WP_SI_FLAG_ACTIVE;
  g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
}

/**
 * wp_session_item_activate_closure:
 * @self: the session item
 * @closure: (transfer full): the closure to use when activation is completed
 *
 * Activates the item asynchronously.
 * You can use wp_session_item_activate_finish() in the closure callback to get
 * the result of this operation.
 *
 * This internally starts a #WpTransition that calls into
 * #WpSessionItemClass.activate_get_next_step() and
 * #WpSessionItemClass.activate_execute_step() to advance.
 * If the transition fails, #WpSessionItemClass.activate_rollback() is called
 * to reverse previous actions.
 *
 * The default implementation of the above virtual functions activates the
 * item successfully without doing anything. In order to implement a meaningful
 * session item, you should override all 3 of them.
 *
 * When this method is called, the %WP_SI_FLAG_ACTIVATING flag is set. When
 * the operation finishes successfully, that flag is cleared and replaced with
 * either %WP_SI_FLAG_ACTIVE or %WP_SI_FLAG_ACTIVATE_ERROR, depending on the
 * success outcome of the operation. In order to clear
 * %WP_SI_FLAG_ACTIVATE_ERROR, you can either call wp_session_item_deactivate()
 * or wp_session_item_activate() to try activating again.
 *
 * This method cannot be called if another operation (activation or export) is
 * in progress (%WP_SI_FLAGS_MASK_OPERATION_IN_PROGRESS) or if the item is
 * already activated.
 */
void
wp_session_item_activate_closure (WpSessionItem * self, GClosure *closure)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  g_return_if_fail (!(priv->flags &
      (WP_SI_FLAGS_MASK_OPERATION_IN_PROGRESS | WP_SI_FLAG_ACTIVE)));

  /* TODO: add a way to cancel the transition if deactivate() is called in the meantime */
  WpTransition *transition = wp_transition_new_closure (
      wp_si_transition_get_type (), self, NULL, closure);
  wp_transition_set_source_tag (transition, wp_session_item_activate);

  g_closure_add_marshal_guards (closure,
      transition, on_activate_transition_pre_completed,
      transition, on_activate_transition_post_completed);

  wp_debug_object (self, "activating item");

  priv->flags &= ~WP_SI_FLAG_ACTIVATE_ERROR;
  priv->flags |= WP_SI_FLAG_ACTIVATING;
  g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);

  WP_SI_TRANSITION (transition)->get_next_step =
      WP_SESSION_ITEM_GET_CLASS (self)->activate_get_next_step;
  WP_SI_TRANSITION (transition)->execute_step =
      WP_SESSION_ITEM_GET_CLASS (self)->activate_execute_step;
  WP_SI_TRANSITION (transition)->rollback =
      WP_SESSION_ITEM_GET_CLASS (self)->activate_rollback;
  wp_transition_advance (transition);
}

/**
 * wp_session_item_activate:
 * @self: the session item
 * @callback: (scope async): a callback to call when activation is finished
 * @callback_data: (closure): data passed to @callback
 *
 * @callback and @callback_data version of wp_session_item_activate_closure()
 */
void
wp_session_item_activate (WpSessionItem * self,
    GAsyncReadyCallback callback,
    gpointer callback_data)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  g_return_if_fail (!(priv->flags &
      (WP_SI_FLAGS_MASK_OPERATION_IN_PROGRESS | WP_SI_FLAG_ACTIVE)));

  GClosure *closure =
      g_cclosure_new (G_CALLBACK (callback), callback_data, NULL);

  wp_session_item_activate_closure (self, closure);
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
  g_return_val_if_fail (WP_IS_SESSION_ITEM (self), FALSE);
  g_return_val_if_fail (
      g_async_result_is_tagged (res, wp_session_item_activate), FALSE);
  return wp_transition_finish (res, error);
}

/**
 * wp_session_item_deactivate:
 * @self: the session item
 *
 * De-activates the item and/or cancels any ongoing activation operation.
 *
 * If the item was not activated, this method does nothing.
 */
void
wp_session_item_deactivate (WpSessionItem * self)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));

  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);
  static const guint flags = 0xf; /* all activation flags */

  //TODO cancel job if ACTIVATING

  if (priv->flags & WP_SI_FLAG_ACTIVE &&
      WP_SESSION_ITEM_GET_CLASS (self)->activate_rollback)
    WP_SESSION_ITEM_GET_CLASS (self)->activate_rollback (self);

  if (priv->flags & flags) {
    priv->flags &= ~flags;
    g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
  }
}

static void
on_export_transition_pre_completed (gpointer data, GClosure *closure)
{
  WpTransition *transition = WP_TRANSITION (data);
  WpSessionItem *self = wp_transition_get_source_object (transition);
  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  priv->flags &= ~WP_SI_FLAG_EXPORTING;
}

static void
on_export_transition_post_completed (gpointer data, GClosure *closure)
{
  WpTransition *transition = WP_TRANSITION (data);
  WpSessionItem *self = wp_transition_get_source_object (transition);
  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  priv->flags |= wp_transition_had_error (transition) ?
      WP_SI_FLAG_EXPORT_ERROR : WP_SI_FLAG_EXPORTED;
  g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
}

/**
 * wp_session_item_export_closure:
 * @self: the session item
 * @session: the session on which to export this item
 * @closure: (transfer full): the closure to use when export is completed
 *
 * Exports this item asynchronously on PipeWire, making it part of the
 * specified @session. You can use wp_session_item_export_finish() in the
 * @callback to get the result of this operation.
 *
 * This internally starts a #WpTransition that calls into
 * #WpSessionItemClass.export_get_next_step() and
 * #WpSessionItemClass.export_execute_step() to advance.
 * If the transition fails, #WpSessionItemClass.export_rollback() is called
 * to reverse previous actions.
 *
 * Exporting is internally implemented for endpoints (items that implement
 * #WpSiEndpoint) and endpoint links (items that implement #WpSiLink). On other
 * items the default implementation will immediately call the @callback,
 * reporting error. You can extend this to export custom interfaces by
 * overriding the virtual functions mentioned above.
 *
 * When this method is called, the %WP_SI_FLAG_EXPORTING flag is set. When
 * the operation finishes successfully, that flag is cleared and replaced with
 * either %WP_SI_FLAG_EXPORTED or %WP_SI_FLAG_EXPORT_ERROR, depending on the
 * success outcome of the operation. In order to clear
 * %WP_SI_FLAG_EXPORT_ERROR, you can either call wp_session_item_unexport()
 * or wp_session_item_export() to try exporting again.
 *
 * This method cannot be called if another operation (activation or export) is
 * in progress (%WP_SI_FLAGS_MASK_OPERATION_IN_PROGRESS) or if the item is
 * already exported.
 */
void
wp_session_item_export_closure (WpSessionItem * self, WpSession * session,
    GClosure *closure)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));
  g_return_if_fail (WP_IS_SESSION (session));

  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  g_return_if_fail (!(priv->flags &
      (WP_SI_FLAGS_MASK_OPERATION_IN_PROGRESS | WP_SI_FLAG_EXPORTED)));

  g_weak_ref_set (&priv->session, session);

  /* TODO: add a way to cancel the transition if unexport() is called in the meantime */
  WpTransition *transition = wp_transition_new_closure (
      wp_si_transition_get_type (), self, NULL, closure);
  wp_transition_set_source_tag (transition, wp_session_item_export);

  g_closure_add_marshal_guards (closure,
      transition, on_export_transition_pre_completed,
      transition, on_export_transition_post_completed);

  wp_debug_object (self, "exporting item on session " WP_OBJECT_FORMAT,
      WP_OBJECT_ARGS (session));

  priv->flags &= ~WP_SI_FLAG_EXPORT_ERROR;
  priv->flags |= WP_SI_FLAG_EXPORTING;
  g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);

  WP_SI_TRANSITION (transition)->get_next_step =
      WP_SESSION_ITEM_GET_CLASS (self)->export_get_next_step;
  WP_SI_TRANSITION (transition)->execute_step =
      WP_SESSION_ITEM_GET_CLASS (self)->export_execute_step;
  WP_SI_TRANSITION (transition)->rollback =
      WP_SESSION_ITEM_GET_CLASS (self)->export_rollback;
  wp_transition_advance (transition);
}

/**
 * wp_session_item_export:
 * @self: the session item
 * @session: the session on which to export this item
 * @callback: (scope async): a callback to call when exporting is finished
 * @callback_data: (closure): data passed to @callback
 *
 * @callback and @callback_data version of wp_session_item_export_closure()
 */
void
wp_session_item_export (WpSessionItem * self, WpSession * session,
    GAsyncReadyCallback callback, gpointer callback_data)
{
  g_return_if_fail (WP_IS_SESSION_ITEM (self));
  g_return_if_fail (WP_IS_SESSION (session));

  WpSessionItemPrivate *priv =
      wp_session_item_get_instance_private (self);

  g_return_if_fail (!(priv->flags &
      (WP_SI_FLAGS_MASK_OPERATION_IN_PROGRESS | WP_SI_FLAG_EXPORTED)));

  GClosure *closure =
      g_cclosure_new (G_CALLBACK (callback), callback_data, NULL);

  wp_session_item_export_closure (self, session, closure);
}

/**
 * wp_session_item_export_finish:
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
  g_return_val_if_fail (
      g_async_result_is_tagged (res, wp_session_item_export), FALSE);
  return wp_transition_finish (res, error);
}

/**
 * wp_session_item_unexport:
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

  WpSessionItemPrivate *priv = wp_session_item_get_instance_private (self);
  static const guint flags = 0xf0; /* all export flags */

  //TODO cancel job if EXPORTING

  if (priv->flags & WP_SI_FLAG_EXPORTED &&
      WP_SESSION_ITEM_GET_CLASS (self)->export_rollback)
    WP_SESSION_ITEM_GET_CLASS (self)->export_rollback (self);

  if (priv->flags & flags) {
    priv->flags &= ~flags;
    g_signal_emit (self, signals[SIGNAL_FLAGS_CHANGED], 0, priv->flags);
  }
}
