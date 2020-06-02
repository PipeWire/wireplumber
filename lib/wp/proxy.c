/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpProxy
 *
 */

#define G_LOG_DOMAIN "wp-proxy"

#include "proxy.h"
#include "debug.h"
#include "core.h"
#include "error.h"
#include "wpenums.h"
#include "private.h"

#include <pipewire/pipewire.h>
#include <pipewire/impl.h>
#include <pipewire/extensions/metadata.h>
#include <pipewire/extensions/client-node.h>
#include <pipewire/extensions/session-manager.h>

#include <spa/debug/types.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>

typedef struct _WpProxyPrivate WpProxyPrivate;
struct _WpProxyPrivate
{
  /* properties */
  GWeakRef core;
  WpGlobal *global;
  struct pw_proxy *pw_proxy;

  /* The proxy listener */
  struct spa_hook listener;

  /* augment state */
  WpProxyFeatures ft_ready;
  GPtrArray *augment_tasks; // element-type: GTask*

  GHashTable *async_tasks; // <int seq, GTask*>

  /* props cache */
  WpProps *props;
};

enum {
  PROP_0,
  PROP_CORE,
  PROP_GLOBAL,
  PROP_GLOBAL_PERMISSIONS,
  PROP_GLOBAL_PROPERTIES,
  PROP_FEATURES,
  PROP_PW_PROXY,
  PROP_INFO,
  PROP_PROPERTIES,
  PROP_PARAM_INFO,
  PROP_BOUND_ID,
};

enum
{
  SIGNAL_PW_PROXY_CREATED,
  SIGNAL_PW_PROXY_DESTROYED,
  SIGNAL_BOUND,
  SIGNAL_PARAM,
  SIGNAL_PROP_CHANGED,
  LAST_SIGNAL,
};

static guint wp_proxy_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (WpProxy, wp_proxy, G_TYPE_OBJECT)

static void
proxy_event_destroy (void *data)
{
  /* hold a reference to the proxy because unref-ing the tasks might
    destroy the proxy, in case the registry is no longer holding a reference */
  g_autoptr (WpProxy) self = g_object_ref (WP_PROXY (data));
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  GHashTableIter iter;
  GTask *task;

  wp_trace_object (self, "destroyed pw_proxy %p (%u)", priv->pw_proxy,
      priv->global ? priv->global->id : pw_proxy_get_bound_id (priv->pw_proxy));

  spa_hook_remove (&priv->listener);
  priv->pw_proxy = NULL;
  g_signal_emit (self, wp_proxy_signals[SIGNAL_PW_PROXY_DESTROYED], 0);

  /* Return error if the pw_proxy destruction happened while the async
   * init or augment of this proxy object was in progress */
  if (priv->augment_tasks->len > 0) {
    GError *err = g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED,
        "pipewire proxy destroyed before finishing");
    wp_proxy_augment_error (self, err);
  }

  g_hash_table_iter_init (&iter, priv->async_tasks);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &task)) {
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED,
        "pipewire proxy destroyed before finishing");
    g_hash_table_iter_remove (&iter);
  }
}

static void
proxy_event_bound (void *data, uint32_t global_id)
{
  WpProxy *self = WP_PROXY (data);
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  /* we generally make the assumption here that the bound id is the
     same as the global id, but while this **is** it's intended use,
     the truth is that the bound id **can** be changed anytime with
     pw_proxy_set_bound_id() and this can be very bad... */
  g_warn_if_fail (!priv->global || priv->global->id == global_id);

  wp_proxy_set_feature_ready (self, WP_PROXY_FEATURE_BOUND);

  /* construct a WpGlobal if it was not already there */
  if (!priv->global) {
    g_autoptr (WpCore) core = g_weak_ref_get (&priv->core);

    wp_registry_prepare_new_global (wp_core_get_registry (core),
        global_id, PW_PERM_RWX, WP_GLOBAL_FLAG_OWNED_BY_PROXY,
        G_TYPE_FROM_INSTANCE (self), self, NULL, &priv->global);
  }

  g_signal_emit (self, wp_proxy_signals[SIGNAL_BOUND], 0, global_id);
}

static void
proxy_event_removed (void *data)
{
  wp_trace_object (data, "removed");
}

static const struct pw_proxy_events proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_event_destroy,
  .bound = proxy_event_bound,
  .removed = proxy_event_removed,
};

void
wp_proxy_set_pw_proxy (WpProxy * self, struct pw_proxy * proxy)
{
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  if (!proxy)
    return;

  g_return_if_fail (priv->pw_proxy == NULL);
  priv->pw_proxy = proxy;

  pw_proxy_add_listener (priv->pw_proxy, &priv->listener, &proxy_events,
      self);

  /* inform subclasses and listeners */
  g_signal_emit (self, wp_proxy_signals[SIGNAL_PW_PROXY_CREATED], 0,
      priv->pw_proxy);

  /* declare the feature as ready */
  wp_proxy_set_feature_ready (self, WP_PROXY_FEATURE_PW_PROXY);
}

static void
wp_proxy_init (WpProxy * self)
{
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  g_weak_ref_init (&priv->core, NULL);
  priv->augment_tasks = g_ptr_array_new_with_free_func (g_object_unref);
  priv->async_tasks = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
}

static void
wp_proxy_dispose (GObject * object)
{
  WpProxyPrivate *priv = wp_proxy_get_instance_private (WP_PROXY(object));

  wp_trace_object (object, "dispose (global %u; pw_proxy %p)",
      priv->global ? priv->global->id : 0, priv->pw_proxy);

  if (priv->global)
    wp_global_rm_flag (priv->global, WP_GLOBAL_FLAG_OWNED_BY_PROXY);

  /* this will trigger proxy_event_destroy() if the pw_proxy exists */
  if (priv->pw_proxy)
    pw_proxy_destroy (priv->pw_proxy);

  G_OBJECT_CLASS (wp_proxy_parent_class)->dispose (object);
}

static void
wp_proxy_finalize (GObject * object)
{
  WpProxy *self = WP_PROXY (object);
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  g_clear_object (&priv->props);
  g_clear_pointer (&priv->augment_tasks, g_ptr_array_unref);
  g_clear_pointer (&priv->global, wp_global_unref);
  g_weak_ref_clear (&priv->core);
  g_clear_pointer (&priv->async_tasks, g_hash_table_unref);

  G_OBJECT_CLASS (wp_proxy_parent_class)->finalize (object);
}

static void
wp_proxy_set_gobj_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpProxy *self = WP_PROXY (object);
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&priv->core, g_value_get_object (value));
    break;
  case PROP_GLOBAL:
    priv->global = g_value_dup_boxed (value);
    break;
  case PROP_PW_PROXY:
    wp_proxy_set_pw_proxy (self, g_value_get_pointer (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_get_gobj_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpProxy *self = WP_PROXY (object);
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&priv->core));
    break;
  case PROP_GLOBAL_PERMISSIONS:
    g_value_set_uint (value, priv->global ? priv->global->permissions : 0);
    break;
  case PROP_GLOBAL_PROPERTIES:
    g_value_set_boxed (value, priv->global ? priv->global->properties : NULL);
    break;
  case PROP_FEATURES:
    g_value_set_flags (value, priv->ft_ready);
    break;
  case PROP_PW_PROXY:
    g_value_set_pointer (value, priv->pw_proxy);
    break;
  case PROP_INFO:
    g_value_set_pointer (value, (gpointer) wp_proxy_get_info (self));
    break;
  case PROP_PROPERTIES:
    g_value_take_boxed (value, wp_proxy_get_properties (self));
    break;
  case PROP_PARAM_INFO:
    g_value_take_variant (value, wp_proxy_get_param_info (self));
    break;
  case PROP_BOUND_ID:
    g_value_set_uint (value, wp_proxy_get_bound_id (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_enable_feature_props (WpProxy * self)
{
  WpProxyClass *klass = WP_PROXY_GET_CLASS (self);
  struct spa_param_info *param_info;
  guint n_params;
  guint have_propinfo = FALSE, have_props = FALSE;
  uint32_t ids[] = { SPA_PARAM_Props };

  /* check if we actually have props */
  param_info = klass->get_param_info (self, &n_params);
  for (guint i = 0; i < n_params; i++) {
    if (param_info[i].id == SPA_PARAM_PropInfo)
      have_propinfo = TRUE;
    else if (param_info[i].id == SPA_PARAM_Props)
      have_props = TRUE;
  }

  if (have_propinfo && have_props) {
    if (!klass->enum_params || !klass->subscribe_params) {
      wp_proxy_augment_error (self, g_error_new (WP_DOMAIN_LIBRARY,
            WP_LIBRARY_ERROR_INVARIANT,
            "Proxy does not support enum/subscribe params API"));
      return;
    }

    klass->enum_params (self, SPA_PARAM_PropInfo, 0, -1, NULL);
    klass->subscribe_params (self, ids, SPA_N_ELEMENTS (ids));
  } else {
    /* declare as ready with no props */
    wp_proxy_set_feature_ready (self, WP_PROXY_FEATURE_PROPS);
  }

  g_signal_handlers_disconnect_by_func (self,
      wp_proxy_enable_feature_props, self);
}

static void
wp_proxy_default_augment (WpProxy * self, WpProxyFeatures features)
{
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  /* ensure we have a pw_proxy, as we can't have
   * any other feature without first having that */
  if (!priv->pw_proxy && features != 0)
    features |= WP_PROXY_FEATURE_PW_PROXY;

  /* if we don't have a pw_proxy, we have to assume that this WpProxy
   * represents a global object from the registry; we have no other way
   * to get a pw_proxy */
  if (features & WP_PROXY_FEATURE_PW_PROXY) {
    if (priv->global == NULL) {
      wp_proxy_augment_error (self, g_error_new (WP_DOMAIN_LIBRARY,
            WP_LIBRARY_ERROR_INVALID_ARGUMENT,
            "No global specified; cannot bind pw_proxy"));
      return;
    }

    /* bind */
    wp_proxy_set_pw_proxy (self, wp_global_bind (priv->global));
  }

  if (features & WP_PROXY_FEATURE_PROPS && !priv->props) {
    wp_proxy_set_props (self, wp_props_new (WP_PROPS_MODE_CACHE, self));

    if (priv->ft_ready & WP_PROXY_FEATURE_INFO)
      wp_proxy_enable_feature_props (self);
    else
      g_signal_connect (self, "notify::param-info",
          G_CALLBACK (wp_proxy_enable_feature_props), self);
  }
}

static void
wp_proxy_class_init (WpProxyClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->dispose = wp_proxy_dispose;
  object_class->finalize = wp_proxy_finalize;
  object_class->get_property = wp_proxy_get_gobj_property;
  object_class->set_property = wp_proxy_set_gobj_property;

  klass->augment = wp_proxy_default_augment;

  /* Install the properties */

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The WpCore", WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_GLOBAL,
      g_param_spec_boxed ("global", "global", "Internal WpGlobal object",
          wp_global_get_type (),
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_GLOBAL_PERMISSIONS,
      g_param_spec_uint ("global-permissions", "global-permissions",
          "The pipewire global permissions", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_GLOBAL_PROPERTIES,
      g_param_spec_boxed ("global-properties", "global-properties",
          "The pipewire global properties", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FEATURES,
      g_param_spec_flags ("features", "features",
          "The ready WpProxyFeatures on this proxy", WP_TYPE_PROXY_FEATURES, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_PROXY,
      g_param_spec_pointer ("pw-proxy", "pw-proxy", "The struct pw_proxy *",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_INFO,
      g_param_spec_pointer ("info", "info", "The native info structure",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties",
          "The pipewire properties of the object", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PARAM_INFO,
      g_param_spec_variant ("param-info", "param-info",
          "The param info of the object", G_VARIANT_TYPE ("a{ss}"), NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_BOUND_ID,
      g_param_spec_uint ("bound-id", "bound-id",
          "The id that this object has on the registry", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /* Signals */
  wp_proxy_signals[SIGNAL_PW_PROXY_CREATED] = g_signal_new (
      "pw-proxy-created", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpProxyClass, pw_proxy_created), NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_POINTER);

  wp_proxy_signals[SIGNAL_PW_PROXY_DESTROYED] = g_signal_new (
      "pw-proxy-destroyed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpProxyClass, pw_proxy_destroyed), NULL, NULL, NULL,
      G_TYPE_NONE, 0);

  wp_proxy_signals[SIGNAL_BOUND] = g_signal_new (
      "bound", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpProxyClass, bound), NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_UINT);

  wp_proxy_signals[SIGNAL_PROP_CHANGED] = g_signal_new (
      "prop-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (WpProxyClass, prop_changed), NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_STRING);
}

/* private */
void
wp_proxy_destroy (WpProxy *self)
{
  WpProxyPrivate *priv;

  g_return_if_fail (WP_IS_PROXY (self));

  priv = wp_proxy_get_instance_private (self);
  if (priv->pw_proxy)
    pw_proxy_destroy (priv->pw_proxy);
}

/**
 * wp_proxy_request_destroy:
 * @self: the proxy
 *
 * Requests the PipeWire server to destroy the object represented by this proxy.
 * If the server allows it, the object will be destroyed and the
 * WpProxy::pw-proxy-destroyed signal will be emitted. If the server does
 * not allow it, nothing will happen.
 *
 * This is mostly useful for destroying #WpLink and #WpEndpointLink objects.
 */
void
wp_proxy_request_destroy (WpProxy * self)
{
  WpProxyPrivate *priv;
  g_autoptr (WpCore) core = NULL;
  WpRegistry *reg;

  g_return_if_fail (WP_IS_PROXY (self));

  priv = wp_proxy_get_instance_private (self);
  core = wp_proxy_get_core (self);

  if (priv->pw_proxy && core) {
    reg = wp_core_get_registry (core);
    pw_registry_destroy (reg->pw_registry,
        pw_proxy_get_bound_id (priv->pw_proxy));
  }
}

void
wp_proxy_augment (WpProxy * self,
    WpProxyFeatures ft_wanted, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  WpProxyPrivate *priv;
  WpProxyFeatures missing = 0;
  g_autoptr (GTask) task = NULL;

  g_return_if_fail (WP_IS_PROXY (self));
  g_return_if_fail (WP_PROXY_GET_CLASS (self)->augment);

  priv = wp_proxy_get_instance_private (self);

  task = g_task_new (self, cancellable, callback, user_data);

  /* find which features are wanted but missing from the "ready" set */
  missing = (priv->ft_ready ^ ft_wanted) & ft_wanted;

  /* if the features are not ready, call augment(),
   * otherwise signal the callback directly */
  if (missing != 0) {
    g_task_set_task_data (task, GUINT_TO_POINTER (missing), NULL);
    g_ptr_array_add (priv->augment_tasks, g_steal_pointer (&task));
    WP_PROXY_GET_CLASS (self)->augment (self, missing);
  } else {
    g_task_return_boolean (task, TRUE);
  }
}

gboolean
wp_proxy_augment_finish (WpProxy * self, GAsyncResult * res,
    GError ** error)
{
  g_return_val_if_fail (WP_IS_PROXY (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);

  return g_task_propagate_boolean (G_TASK (res), error);
}

void
wp_proxy_set_feature_ready (WpProxy * self, WpProxyFeatures feature)
{
  WpProxyPrivate *priv;
  g_autoptr (GPtrArray) ready_tasks = NULL;
  guint i;

  g_return_if_fail (WP_IS_PROXY (self));

  priv = wp_proxy_get_instance_private (self);

  /* feature already marked as ready */
  if (priv->ft_ready & feature)
    return;

  priv->ft_ready |= feature;

  if (wp_log_level_is_enabled (G_LOG_LEVEL_DEBUG)) {
    g_autofree gchar *str = g_flags_to_string (WP_TYPE_PROXY_FEATURES,
        priv->ft_ready);
    wp_debug_object (self, "features changed: %s", str);
  }

  g_object_notify (G_OBJECT (self), "features");

  /* hold a reference to the proxy because unref-ing the tasks might
    destroy the proxy, in case the registry is no longer holding a reference */
  g_object_ref (self);

  /* move the ready tasks to another array to avoid recursion issues */
  ready_tasks = g_ptr_array_new_with_free_func (g_object_unref);

  /* return from the task if all the wanted features are now ready */
  for (i = priv->augment_tasks->len; i > 0; i--) {
    GTask *task = g_ptr_array_index (priv->augment_tasks, i - 1);
    WpProxyFeatures wanted = GPOINTER_TO_UINT (g_task_get_task_data (task));

    if ((priv->ft_ready & wanted) == wanted) {
      /* this is safe as long as we are traversing the array backwards */
      g_ptr_array_add (ready_tasks,
          g_ptr_array_steal_index_fast (priv->augment_tasks, i - 1));
    }
  }

  for (i = 0; i < ready_tasks->len; i++) {
    GTask *task = g_ptr_array_index (ready_tasks, i);
    g_task_return_boolean (task, TRUE);
  }

  g_object_unref (self);
}

/**
 * wp_proxy_augment_error:
 * @self: the proxy
 * @error: (transfer full): the error
 *
 * Reports an error that occured during the augment process
 */
void
wp_proxy_augment_error (WpProxy * self, GError * error)
{
  WpProxyPrivate *priv;

  g_return_if_fail (WP_IS_PROXY (self));

  priv = wp_proxy_get_instance_private (self);

  /* steal the array to avoid recursion here */
  if (priv->augment_tasks->len > 0) {
    guint i;
    g_autoptr (GPtrArray) augment_tasks =
        g_steal_pointer (&priv->augment_tasks);
    priv->augment_tasks = g_ptr_array_new_with_free_func (g_object_unref);

    for (i = 0; i < augment_tasks->len; i++) {
      GTask *task = g_ptr_array_index (augment_tasks, i);
      g_task_return_error (task, g_error_copy (error));
    }
  }

  g_error_free (error);
}

WpProxyFeatures
wp_proxy_get_features (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), 0);

  priv = wp_proxy_get_instance_private (self);
  return priv->ft_ready;
}

/**
 * wp_proxy_get_core:
 * @self: the proxy
 *
 * Returns: (transfer full): the core that created this proxy
 */
WpCore *
wp_proxy_get_core (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  priv = wp_proxy_get_instance_private (self);
  return g_weak_ref_get (&priv->core);
}

guint32
wp_proxy_get_global_permissions (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), 0);

  priv = wp_proxy_get_instance_private (self);
  return priv->global ? priv->global->permissions : 0;
}

/**
 * wp_proxy_get_global_properties:
 *
 * Returns: (transfer full): the global properties of the proxy
 */
WpProperties *
wp_proxy_get_global_properties (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  priv = wp_proxy_get_instance_private (self);
  if (!priv->global || !priv->global->properties)
    return NULL;
  return wp_properties_ref (priv->global->properties);
}

struct pw_proxy *
wp_proxy_get_pw_proxy (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  priv = wp_proxy_get_instance_private (self);
  return priv->pw_proxy;
}

/**
 * wp_proxy_get_info:
 * @self: the proxy
 *
 * Requires %WP_PROXY_FEATURE_INFO
 *
 * Returns: the pipewire info structure of this object
 *    (pw_node_info, pw_port_info, etc...)
 */
gconstpointer
wp_proxy_get_info (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  g_warn_if_fail (priv->ft_ready & WP_PROXY_FEATURE_INFO);

  return (WP_PROXY_GET_CLASS (self)->get_info) ?
      WP_PROXY_GET_CLASS (self)->get_info (self) : NULL;
}

/**
 * wp_proxy_get_properties:
 * @self: the proxy
 *
 * Requires %WP_PROXY_FEATURE_INFO
 *
 * Returns: (transfer full): the pipewire properties of this object;
 *   normally these are the properties that are part of the info structure
 */
WpProperties *
wp_proxy_get_properties (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  g_warn_if_fail (priv->ft_ready & WP_PROXY_FEATURE_INFO);

  return (WP_PROXY_GET_CLASS (self)->get_properties) ?
      WP_PROXY_GET_CLASS (self)->get_properties (self) : NULL;
}

/**
 * wp_proxy_get_property:
 * @self: the proxy
 * @key: the property name
 *
 * Returns the value of a single pipewire property. This is the same as getting
 * the whole properties structure with wp_proxy_get_properties() and accessing
 * a single property with wp_properties_get(), but saves one call
 * and having to clean up the #WpProperties reference count afterwards.
 *
 * The value is owned by the proxy, but it is guaranteed to stay alive
 * until execution returns back to the event loop.
 *
 * Requires %WP_PROXY_FEATURE_INFO
 *
 * Returns: (transfer none) (nullable): the value of the pipewire property @key
 *   or %NULL if the property doesn't exist
 */
const gchar *
wp_proxy_get_property (WpProxy * self, const gchar * key)
{
  /* the proxy always keeps a ref to the data, so it's safe
     to discard the ref count of the WpProperties */
  g_autoptr (WpProperties) props = NULL;
  props = wp_proxy_get_properties (self);
  return props ? wp_properties_get (props, key) : NULL;
}

/**
 * wp_proxy_get_param_info:
 * @self: the proxy
 *
 * Returns the available parameters of this proxy. The return value is
 * a variant of type `a{ss}`, where the key of each map entry is a spa param
 * type id (the same ids that you can pass in wp_proxy_enum_params())
 * and the value is a string that can contain the following letters,
 * each of them representing a flag:
 *   - `r`: the param is readable (`SPA_PARAM_INFO_READ`)
 *   - `w`: the param is writable (`SPA_PARAM_INFO_WRITE`)
 *   - `s`: the param was updated (`SPA_PARAM_INFO_SERIAL`)
 *
 * For params that are readable, you can query them with wp_proxy_enum_params()
 *
 * Params that are writable can be set with wp_proxy_set_param()
 *
 * Requires %WP_PROXY_FEATURE_INFO
 *
 * Returns: (transfer full) (nullable): a variant of type `a{ss}` or %NULL
 *   if the proxy does not support params at all
 */
GVariant *
wp_proxy_get_param_info (WpProxy * self)
{
  g_auto (GVariantBuilder) b =
      G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_DICTIONARY);
  guint n_params = 0;
  struct spa_param_info *info;

  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  g_warn_if_fail (priv->ft_ready & WP_PROXY_FEATURE_INFO);

  info = (WP_PROXY_GET_CLASS (self)->get_param_info) ?
      WP_PROXY_GET_CLASS (self)->get_param_info (self, &n_params) : NULL;
  if (!info || n_params == 0)
    return NULL;

  for (guint i = 0; i < n_params; i++) {
    const gchar *nick = NULL;
    gchar flags[4];
    guint flags_idx = 0;

    wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_PARAM, info[i].id, NULL, &nick,
        NULL);
    g_return_val_if_fail (nick != NULL, NULL);

    if (info[i].flags & SPA_PARAM_INFO_READ)
      flags[flags_idx++] = 'r';
    if (info[i].flags & SPA_PARAM_INFO_WRITE)
      flags[flags_idx++] = 'w';
    if (info[i].flags & SPA_PARAM_INFO_SERIAL)
      flags[flags_idx++] = 's';
    flags[flags_idx] = '\0';

    g_variant_builder_add (&b, "{ss}", nick, flags);
  }

  return g_variant_builder_end (&b);
}

/**
 * wp_proxy_get_bound_id:
 * @self: the proxy
 *
 * Returns the bound id, which is the id that this object has on the
 * pipewire registry (a.k.a. the global id). The object must have the
 * %WP_PROXY_FEATURE_BOUND feature before this method can be called.
 *
 * Returns: the bound id of this object
 */
guint32
wp_proxy_get_bound_id (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), 0);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  g_warn_if_fail (priv->ft_ready & WP_PROXY_FEATURE_BOUND);

  return priv->pw_proxy ? pw_proxy_get_bound_id (priv->pw_proxy) : SPA_ID_INVALID;
}

static void
wp_proxy_register_async_task (WpProxy * self, int seq, GTask * task)
{
  WpProxyPrivate *priv;

  g_return_if_fail (WP_IS_PROXY (self));
  g_return_if_fail (g_task_is_valid (task, self));

  priv = wp_proxy_get_instance_private (self);
  g_hash_table_insert (priv->async_tasks, GINT_TO_POINTER (seq), task);
}

static GTask *
wp_proxy_find_async_task (WpProxy * self, int seq, gboolean steal)
{
  WpProxyPrivate *priv;
  GTask *task = NULL;

  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  priv = wp_proxy_get_instance_private (self);
  if (steal)
    g_hash_table_steal_extended (priv->async_tasks, GINT_TO_POINTER (seq),
        NULL, (gpointer *) &task);
  else
    task = g_hash_table_lookup (priv->async_tasks, GINT_TO_POINTER (seq));

  return task;
}

static void
enum_params_done (WpCore * core, GAsyncResult * res, gpointer data)
{
  int seq = GPOINTER_TO_INT (g_task_get_source_tag (G_TASK (data)));
  WpProxy *proxy = g_task_get_source_object (G_TASK (data));
  g_autoptr (GTask) task = NULL;
  g_autoptr (GError) error = NULL;

  /* finish the sync task */
  wp_core_sync_finish (core, res, &error);

  /* find the enum params task in the hash table to steal the reference */
  task = wp_proxy_find_async_task (proxy, seq, TRUE);
  g_return_if_fail (task != NULL);

  if (error)
    g_task_return_error (task, g_steal_pointer (&error));
  else {
    GPtrArray *params = g_task_get_task_data (task);
    g_task_return_pointer (task, g_ptr_array_ref (params),
        (GDestroyNotify) g_ptr_array_unref);
  }
}

/**
 * wp_proxy_enum_params:
 * @self: the proxy
 * @id: (nullable): the parameter id to enumerate or %NULL for all parameters
 * @filter: (nullable): a param filter or %NULL
 * @cancellable: (nullable): a cancellable for the async operation
 * @callback: (scope async): a callback to call with the result
 * @user_data: (closure): data to pass to @callback
 *
 * Enumerate object parameters. This will asynchronously return the result,
 * or an error, by calling the given @callback. The result is going to
 * be a #WpIterator containing #WpSpaPod objects, which can be retrieved
 * with wp_proxy_enum_params_finish().
 */
void
wp_proxy_enum_params (WpProxy * self, const gchar * id,
    const WpSpaPod *filter, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  g_autoptr (GTask) task = NULL;
  guint32 id_num = 0;
  int seq;
  GPtrArray *params;

  g_return_if_fail (WP_IS_PROXY (self));

  /* create task for enum_params */
  task = g_task_new (self, cancellable, callback, user_data);
  params = g_ptr_array_new_with_free_func ((GDestroyNotify) wp_spa_pod_unref);
  g_task_set_task_data (task, params, (GDestroyNotify) g_ptr_array_unref);

  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_PARAM, id, &id_num,
          NULL, NULL)) {
    wp_critical_object (self, "invalid param id: %s", id);
    return;
  }

  /* call enum_params */
  seq = (WP_PROXY_GET_CLASS (self)->enum_params) ?
      WP_PROXY_GET_CLASS (self)->enum_params (self, id_num, 0, -1, filter) :
      -ENOTSUP;
  if (G_UNLIKELY (seq < 0)) {
    wp_message_object (self, "enum_params failed: %s", spa_strerror (seq));
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED, "enum_params failed: %s",
        spa_strerror (seq));
    return;
  }
  g_task_set_source_tag (task, GINT_TO_POINTER (seq));
  wp_proxy_register_async_task (self, seq, g_object_ref (task));

  /* call sync */
  g_autoptr (WpCore) core = wp_proxy_get_core (self);
  wp_core_sync (core, cancellable, (GAsyncReadyCallback) enum_params_done,
      task);
}

/**
 * wp_proxy_enum_params_finish:
 * @self: the proxy
 * @res: the async result
 * @error: (out) (optional): the reported error of the operation, if any
 *
 * Returns: (transfer full) (nullable): an iterator to iterate over the
 *   collected params, or %NULL if the operation resulted in error;
 *   the items in the iterator are #WpSpaPod
 */
WpIterator *
wp_proxy_enum_params_finish (WpProxy * self, GAsyncResult * res,
    GError ** error)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);
  g_return_val_if_fail (g_task_is_valid (res, self), NULL);

  GPtrArray *array = g_task_propagate_pointer (G_TASK (res), error);
  if (!array)
    return NULL;

  return wp_iterator_new_ptr_array (array, WP_TYPE_SPA_POD);
}

/**
 * wp_proxy_set_param:
 * @self: the proxy
 * @id: the parameter id to set
 * @param: the parameter to set
 *
 * Sets a parameter on the object.
 */
void
wp_proxy_set_param (WpProxy * self, const gchar * id, const WpSpaPod *param)
{
  guint32 id_num = 0;
  gint ret;

  g_return_if_fail (WP_IS_PROXY (self));

  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_PARAM, id, &id_num,
          NULL, NULL)) {
    wp_critical_object (self, "invalid param id: %s", id);
    return;
  }

  ret = (WP_PROXY_GET_CLASS (self)->set_param) ?
      WP_PROXY_GET_CLASS (self)->set_param (self, id_num, 0, param) :
      -ENOTSUP;
  if (G_UNLIKELY (ret < 0)) {
    wp_message_object (self, "set_param failed: %s", spa_strerror (ret));
  }
}

/**
 * wp_proxy_iterate_prop_info:
 * @self: the proxy
 *
 * Requires %WP_PROXY_FEATURE_PROPS
 *
 * Returns: (transfer full) (nullable): an iterator to iterate over the
 *   `SPA_PARAM_PropInfo` params, or %NULL if the object has no props;
 *   the items in the iterator are #WpSpaPod
 */
WpIterator *
wp_proxy_iterate_prop_info (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  g_return_val_if_fail (priv->ft_ready & WP_PROXY_FEATURE_PROPS, NULL);

  return wp_props_iterate_prop_info (priv->props);
}

/**
 * wp_proxy_get_prop:
 * @self: the proxy
 * @prop_name: the prop name
 *
 * Requires %WP_PROXY_FEATURE_PROPS
 *
 * Returns: (transfer full) (nullable): the spa pod containing the value
 *   of this prop, or %NULL if @prop_name does not exist on this proxy
 */
WpSpaPod *
wp_proxy_get_prop (WpProxy * self, const gchar * prop_name)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  g_return_val_if_fail (priv->ft_ready & WP_PROXY_FEATURE_PROPS, NULL);

  return wp_props_get (priv->props, prop_name);
}

/**
 * wp_proxy_set_prop:
 * @self: the proxy
 * @prop_name: the prop name
 * @value: (transfer full): the new value for this prop, as a spa pod
 *
 * Sets a single property in the `SPA_PARAM_Props` param of this object.
 */
void
wp_proxy_set_prop (WpProxy * self, const gchar * prop_name, WpSpaPod * value)
{
  g_return_if_fail (WP_IS_PROXY (self));
  g_return_if_fail (value != NULL);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  g_return_if_fail (priv->ft_ready & WP_PROXY_FEATURE_PROPS);

  wp_props_set (priv->props, prop_name, value);
}

void
wp_proxy_handle_event_param (void * proxy, int seq, uint32_t id,
    uint32_t index, uint32_t next, const struct spa_pod *param)
{
  WpProxy *self = WP_PROXY (proxy);
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  g_autoptr (WpSpaPod) w_param = wp_spa_pod_new_regular_wrap (param);
  GTask *task;

  /* if this param event was emited because of enum_params(),
   * copy the param in the result array of that API */
  task = wp_proxy_find_async_task (self, seq, FALSE);
  if (task) {
    GPtrArray *array = g_task_get_task_data (task);
    g_ptr_array_add (array, wp_spa_pod_copy (w_param));
  }
  /* else consider this to be a prop update, either triggered from augment()
   * or because we are subscribed to props */
  else if (priv->props) {
    switch (id) {
      case SPA_PARAM_PropInfo:
        wp_trace_boxed (WP_TYPE_SPA_POD, w_param,
            "storing PropInfo on " WP_OBJECT_FORMAT, WP_OBJECT_ARGS (self));
        wp_props_register_from_info (priv->props, g_steal_pointer (&w_param));
        break;
      case SPA_PARAM_Props:
        wp_trace_boxed (WP_TYPE_SPA_POD, w_param,
            "storing Props on " WP_OBJECT_FORMAT, WP_OBJECT_ARGS (self));
        wp_props_store (priv->props, NULL, g_steal_pointer (&w_param));

        /* we receive PropInfo before Props; once we get Props, we know we have
           completed caching of props, so the feature is ready */
        wp_proxy_set_feature_ready (self, WP_PROXY_FEATURE_PROPS);
        break;
      default:
        break;
    }
  }
}

WpProps *
wp_proxy_get_props (WpProxy * self)
{
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  return priv->props;
}

static void
propagate_prop_changed (WpProps * props, const gchar * name, WpProxy * self)
{
  /* only emit if FEATURE_PROPS is enabled, because users might call
     wp_proxy_get_prop() in the handler and it will assert */
  if (wp_proxy_get_features (self) & WP_PROXY_FEATURE_PROPS)
    g_signal_emit (self, wp_proxy_signals[SIGNAL_PROP_CHANGED], 0, name);
}

void
wp_proxy_set_props (WpProxy * self, WpProps * props)
{
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  g_return_if_fail (priv->props == NULL);
  priv->props = props;

  g_signal_connect_object (props, "prop-changed",
      G_CALLBACK (propagate_prop_changed), self, 0);
}
