/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "proxy.h"
#include "core.h"
#include "error.h"
#include "wpenums.h"

#include "proxy-client.h"
#include "proxy-link.h"
#include "proxy-node.h"
#include "proxy-port.h"

#include <pipewire/pipewire.h>
#include <spa/debug/types.h>

typedef struct _WpProxyPrivate WpProxyPrivate;
struct _WpProxyPrivate
{
  /* properties */
  GWeakRef core;

  guint32 global_id;
  guint32 global_perm;
  WpProperties *global_props;

  guint32 iface_type;
  guint32 iface_version;

  struct pw_proxy *pw_proxy;

  /* The proxy listener */
  struct spa_hook listener;

  /* augment state */
  WpProxyFeatures ft_ready;
  WpProxyFeatures ft_wanted;
  GTask *task;

  GHashTable *async_tasks; // <int seq, GTask*>
};

enum {
  PROP_0,
  PROP_CORE,
  PROP_GLOBAL_ID,
  PROP_GLOBAL_PERMISSIONS,
  PROP_GLOBAL_PROPERTIES,
  PROP_INTERFACE_TYPE,
  PROP_INTERFACE_NAME,
  PROP_INTERFACE_QUARK,
  PROP_INTERFACE_VERSION,
  PROP_PW_PROXY,
  PROP_FEATURES,
};

enum
{
  SIGNAL_PW_PROXY_CREATED,
  SIGNAL_PW_PROXY_DESTROYED,
  LAST_SIGNAL,
};

static guint wp_proxy_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (WpProxy, wp_proxy, G_TYPE_OBJECT)

G_DEFINE_QUARK (core, wp_proxy_core)
G_DEFINE_QUARK (registry, wp_proxy_registry)
G_DEFINE_QUARK (node, wp_proxy_node)
G_DEFINE_QUARK (port, wp_proxy_port)
G_DEFINE_QUARK (factory, wp_proxy_factory)
G_DEFINE_QUARK (link, wp_proxy_link)
G_DEFINE_QUARK (client, wp_proxy_client)
G_DEFINE_QUARK (module, wp_proxy_module)
G_DEFINE_QUARK (device, wp_proxy_device)
G_DEFINE_QUARK (client-node, wp_proxy_client_node)

static struct {
  /* the pipewire interface type */
  guint32 pw_type;
  /* the minimum interface version that the remote object must support */
  guint32 req_version;
  /* the _get_type() function of the subclass */
  GType (*get_type) (void);
  /* a function returning a quark that identifies the interface */
  GQuark (*get_quark) (void);
} types_assoc[] = {
  { PW_TYPE_INTERFACE_Core, 0, wp_proxy_get_type, wp_proxy_core_quark },
  { PW_TYPE_INTERFACE_Registry, 0, wp_proxy_get_type, wp_proxy_registry_quark },
  { PW_TYPE_INTERFACE_Node, 0, wp_proxy_node_get_type, wp_proxy_node_quark },
  { PW_TYPE_INTERFACE_Port, 0, wp_proxy_port_get_type, wp_proxy_port_quark },
  { PW_TYPE_INTERFACE_Factory, 0, wp_proxy_get_type, wp_proxy_factory_quark },
  { PW_TYPE_INTERFACE_Link, 0, wp_proxy_link_get_type, wp_proxy_link_quark },
  { PW_TYPE_INTERFACE_Client, 0, wp_proxy_client_get_type, wp_proxy_client_quark },
  { PW_TYPE_INTERFACE_Module, 0, wp_proxy_get_type, wp_proxy_module_quark },
  { PW_TYPE_INTERFACE_Device, 0, wp_proxy_get_type, wp_proxy_device_quark },
  { PW_TYPE_INTERFACE_ClientNode, 0, wp_proxy_get_type, wp_proxy_client_node_quark },
};

static inline GType
wp_proxy_find_instance_type (guint32 type, guint32 version)
{
  for (gint i = 0; i < SPA_N_ELEMENTS (types_assoc); i++) {
    if (types_assoc[i].pw_type == type &&
        types_assoc[i].req_version <= version)
      return types_assoc[i].get_type ();
  }

  return WP_TYPE_PROXY;
}

static inline GQuark
wp_proxy_find_quark_for_type (guint32 type)
{
  for (gint i = 0; i < SPA_N_ELEMENTS (types_assoc); i++) {
    if (types_assoc[i].pw_type == type)
      return types_assoc[i].get_quark ();
  }

  return 0;
}

static void
proxy_event_destroy (void *data)
{
  /* hold a reference to the proxy because unref-ing the tasks might
    destroy the proxy, in case the core is no longer holding a reference */
  g_autoptr (WpProxy) self = g_object_ref (WP_PROXY (data));
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  GHashTableIter iter;
  GTask *task;

  priv->pw_proxy = NULL;

  g_signal_emit (self, wp_proxy_signals[SIGNAL_PW_PROXY_DESTROYED], 0);

  /* Return error if the pw_proxy destruction happened while the async
   * init or augment of this proxy object was in progress */
  if (priv->task) {
    g_task_return_new_error (priv->task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED,
        "pipewire proxy destroyed before finishing");
    g_clear_object (&priv->task);
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
proxy_event_done (void *data, int seq)
{
  WpProxy *self = WP_PROXY (data);
  g_autoptr (GTask) task;

  if ((task = wp_proxy_find_async_task (self, seq, TRUE)))
    g_task_return_boolean (task, TRUE);
}

static const struct pw_proxy_events proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_event_destroy,
  .done = proxy_event_done,
};

static void
wp_proxy_got_pw_proxy (WpProxy * self)
{
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

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
  priv->async_tasks = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
}

static void
wp_proxy_constructed (GObject * object)
{
  WpProxy *self = WP_PROXY (object);
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  /* native proxy was passed in the constructor, declare it as ready */
  if (priv->pw_proxy)
    wp_proxy_got_pw_proxy (self);
}

static void
wp_proxy_dispose (GObject * object)
{
  WpProxyPrivate *priv = wp_proxy_get_instance_private (WP_PROXY(object));

  /* this will trigger proxy_event_destroy() if the pw_proxy exists */
  if (priv->pw_proxy)
    pw_proxy_destroy (priv->pw_proxy);

  G_OBJECT_CLASS (wp_proxy_parent_class)->dispose (object);
}

static void
wp_proxy_finalize (GObject * object)
{
  WpProxyPrivate *priv = wp_proxy_get_instance_private (WP_PROXY(object));

  g_debug ("%s:%p destroyed (global %u; pw_proxy %p)",
      G_OBJECT_TYPE_NAME (object), object, priv->global_id, priv->pw_proxy);

  g_clear_object (&priv->task);
  g_clear_pointer (&priv->global_props, wp_properties_unref);
  g_weak_ref_clear (&priv->core);
  g_clear_pointer (&priv->async_tasks, g_hash_table_unref);

  G_OBJECT_CLASS (wp_proxy_parent_class)->finalize (object);
}

static void
wp_proxy_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpProxyPrivate *priv = wp_proxy_get_instance_private (WP_PROXY(object));

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&priv->core, g_value_get_object (value));
    break;
  case PROP_GLOBAL_ID:
    priv->global_id = g_value_get_uint (value);
    break;
  case PROP_GLOBAL_PERMISSIONS:
    priv->global_perm = g_value_get_uint (value);
    break;
  case PROP_GLOBAL_PROPERTIES:
    priv->global_props = g_value_dup_boxed (value);
    break;
  case PROP_INTERFACE_TYPE:
    priv->iface_type = g_value_get_uint (value);
    break;
  case PROP_INTERFACE_VERSION:
    priv->iface_version = g_value_get_uint (value);
    break;
  case PROP_PW_PROXY:
    priv->pw_proxy = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpProxyPrivate *priv = wp_proxy_get_instance_private (WP_PROXY(object));

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&priv->core));
    break;
  case PROP_GLOBAL_ID:
    g_value_set_uint (value, priv->global_id);
    break;
  case PROP_GLOBAL_PERMISSIONS:
    g_value_set_uint (value, priv->global_perm);
    break;
  case PROP_GLOBAL_PROPERTIES:
    g_value_set_boxed (value, priv->global_props);
    break;
  case PROP_INTERFACE_TYPE:
    g_value_set_uint (value, priv->iface_type);
    break;
  case PROP_INTERFACE_NAME:
    g_value_set_static_string (value,
        spa_debug_type_find_name (pw_type_info(), priv->iface_type));
    break;
  case PROP_INTERFACE_QUARK:
    g_value_set_uint (value, wp_proxy_find_quark_for_type (priv->iface_type));
    break;
  case PROP_INTERFACE_VERSION:
    g_value_set_uint (value, priv->iface_version);
    break;
  case PROP_PW_PROXY:
    g_value_set_pointer (value, priv->pw_proxy);
    break;
  case PROP_FEATURES:
    g_value_set_flags (value, priv->ft_ready);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_default_augment (WpProxy * self, WpProxyFeatures features)
{
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  g_autoptr (WpCore) core = NULL;

  /* ensure we have a pw_proxy, as we can't have
   * any other feature without first having that */
  if (!priv->pw_proxy && features != 0)
    features |= WP_PROXY_FEATURE_PW_PROXY;

  /* if we don't have a pw_proxy, we have to assume that this WpProxy
   * represents a global object from the registry; we have no other way
   * to get a pw_proxy */
  if (features & WP_PROXY_FEATURE_PW_PROXY) {
    if (!wp_proxy_is_global (self)) {
      wp_proxy_augment_error (self, g_error_new (WP_DOMAIN_LIBRARY,
            WP_LIBRARY_ERROR_INVALID_ARGUMENT,
            "No global id specified; cannot bind pw_proxy"));
      return;
    }

    core = g_weak_ref_get (&priv->core);
    g_return_if_fail (core);

    /* bind */
    priv->pw_proxy = pw_registry_proxy_bind (
        wp_core_get_pw_registry_proxy (core), priv->global_id,
        priv->iface_type, priv->iface_version, 0);
    wp_proxy_got_pw_proxy (self);
  }
}

static void
wp_proxy_class_init (WpProxyClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_proxy_constructed;
  object_class->dispose = wp_proxy_dispose;
  object_class->finalize = wp_proxy_finalize;
  object_class->get_property = wp_proxy_get_property;
  object_class->set_property = wp_proxy_set_property;

  klass->augment = wp_proxy_default_augment;

  /* Install the properties */

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The WpCore", WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_GLOBAL_ID,
      g_param_spec_uint ("global-id", "global-id",
          "The pipewire global id", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_GLOBAL_PERMISSIONS,
      g_param_spec_uint ("global-permissions", "global-permissions",
          "The pipewire global permissions", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_GLOBAL_PROPERTIES,
      g_param_spec_boxed ("global-properties", "global-properties",
          "The pipewire global properties", WP_TYPE_PROPERTIES,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_INTERFACE_TYPE,
      g_param_spec_uint ("interface-type", "interface-type",
          "The pipewire interface type", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_INTERFACE_NAME,
      g_param_spec_string ("interface-name", "interface-name",
          "The name of the pipewire interface", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_INTERFACE_QUARK,
      g_param_spec_uint ("interface-quark", "interface-quark",
          "A quark identifying the pipewire interface", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_INTERFACE_VERSION,
      g_param_spec_uint ("interface-version", "interface-version",
          "The pipewire interface version", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_PROXY,
      g_param_spec_pointer ("pw-proxy", "pw-proxy", "The struct pw_proxy *",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FEATURES,
      g_param_spec_flags ("features", "features",
          "The ready WpProxyFeatures on this proxy", WP_TYPE_PROXY_FEATURES, 0,
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
}

WpProxy *
wp_proxy_new_global (WpCore * core,
    guint32 id, guint32 permissions, WpProperties * properties,
    guint32 type, guint32 version)
{
  GType gtype = wp_proxy_find_instance_type (type, version);
  return g_object_new (gtype,
      "core", core,
      "global-id", id,
      "global-permissions", permissions,
      "global-properties", properties,
      "interface-type", type,
      "interface-version", version,
      NULL);
}

WpProxy *
wp_proxy_new_wrap (WpCore * core,
    struct pw_proxy * proxy, guint32 type, guint32 version)
{
  GType gtype = wp_proxy_find_instance_type (type, version);
  return g_object_new (gtype,
      "core", core,
      "pw-proxy", proxy,
      "interface-type", type,
      "interface-version", version,
      NULL);
}

void
wp_proxy_augment (WpProxy * self,
    WpProxyFeatures ft_wanted, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  WpProxyPrivate *priv;
  WpProxyFeatures missing = 0;

  g_return_if_fail (WP_IS_PROXY (self));
  g_return_if_fail (WP_PROXY_GET_CLASS (self)->augment);

  priv = wp_proxy_get_instance_private (self);
  priv->task = g_task_new (self, cancellable, callback, user_data);

  /* we don't simply assign here, we keep all the previous wanted features;
   * it is not allowed to remove features */
  priv->ft_wanted |= ft_wanted;

  /* find which features are wanted but missing from the "ready" set */
  missing = (priv->ft_ready ^ priv->ft_wanted) & priv->ft_wanted;

  /* if the features are not ready, call augment(),
   * otherwise signal the callback directly */
  if (missing != 0) {
    WP_PROXY_GET_CLASS (self)->augment (self, missing);
  } else {
    g_task_return_boolean (priv->task, TRUE);
    g_clear_object (&priv->task);
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

  g_return_if_fail (WP_IS_PROXY (self));

  priv = wp_proxy_get_instance_private (self);

  /* feature already marked as ready */
  if (priv->ft_ready & feature)
    return;

  priv->ft_ready |= feature;

  g_object_notify (G_OBJECT (self), "features");

  /* return from the task if all the wanted features are now ready */
  if (priv->task &&
      (priv->ft_ready & priv->ft_wanted) == priv->ft_wanted) {
    g_task_return_boolean (priv->task, TRUE);
    g_clear_object (&priv->task);
  }
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

  if (priv->task)
    g_task_return_error (priv->task, error);
  else
    g_error_free (error);

  g_clear_object (&priv->task);
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

gboolean
wp_proxy_is_global (WpProxy * self)
{
  return wp_proxy_get_global_id (self) != 0;
}

guint32
wp_proxy_get_global_id (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), 0);

  priv = wp_proxy_get_instance_private (self);
  return priv->global_id;
}

guint32
wp_proxy_get_global_permissions (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), 0);

  priv = wp_proxy_get_instance_private (self);
  return priv->global_perm;
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
  return priv->global_props ? wp_properties_ref (priv->global_props) : NULL;
}

guint32
wp_proxy_get_interface_type (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), 0);

  priv = wp_proxy_get_instance_private (self);
  return priv->iface_type;
}

const gchar *
wp_proxy_get_interface_name (WpProxy * self)
{
  const gchar *name = NULL;

  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  g_object_get (self, "interface-name", &name, NULL);
  return name;
}

GQuark
wp_proxy_get_interface_quark (WpProxy * self)
{
  GQuark q = 0;

  g_return_val_if_fail (WP_IS_PROXY (self), 0);

  g_object_get (self, "interface-quark", &q, NULL);
  return q;
}

guint32
wp_proxy_get_interface_version (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), 0);

  priv = wp_proxy_get_instance_private (self);
  return priv->iface_version;
}

struct pw_proxy *
wp_proxy_get_pw_proxy (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), NULL);

  priv = wp_proxy_get_instance_private (self);
  return priv->pw_proxy;
}

void
wp_proxy_sync (WpProxy * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  WpProxyPrivate *priv;
  g_autoptr (GTask) task = NULL;
  int seq;

  g_return_if_fail (WP_IS_PROXY (self));

  priv = wp_proxy_get_instance_private (self);
  task = g_task_new (self, cancellable, callback, user_data);

  if (G_UNLIKELY (!priv->pw_proxy)) {
    g_warn_if_reached ();
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT, "No pipewire proxy");
    return;
  }

  seq = pw_proxy_sync (priv->pw_proxy, 0);
  if (G_UNLIKELY (seq < 0)) {
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED, "pw_proxy_sync failed: %s",
        g_strerror (-seq));
    return;
  }

  wp_proxy_register_async_task (self, seq, g_steal_pointer (&task));
}

gboolean
wp_proxy_sync_finish (WpProxy * self, GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_PROXY (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);

  return g_task_propagate_boolean (G_TASK (res), error);
}

/**
 * wp_proxy_register_async_task: (skip)
 */
void
wp_proxy_register_async_task (WpProxy * self, int seq, GTask * task)
{
  WpProxyPrivate *priv;

  g_return_if_fail (WP_IS_PROXY (self));
  g_return_if_fail (g_task_is_valid (task, self));

  priv = wp_proxy_get_instance_private (self);
  g_hash_table_insert (priv->async_tasks, GINT_TO_POINTER (seq), task);
}

/**
 * wp_proxy_find_async_task: (skip)
 */
GTask *
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
