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
#include "private.h"

#include "endpoint.h"
#include "proxy-client.h"
#include "proxy-device.h"
#include "proxy-link.h"
#include "proxy-node.h"
#include "proxy-port.h"
#include "session.h"

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

  char *iface_type;
  guint32 iface_version;
  gpointer local_object;

  struct pw_proxy *pw_proxy;

  /* The proxy listener */
  struct spa_hook listener;

  /* augment state */
  WpProxyFeatures ft_ready;
  GPtrArray *augment_tasks; // element-type: GTask*

  GHashTable *async_tasks; // <int seq, GTask*>
};

enum {
  PROP_0,
  PROP_CORE,
  PROP_GLOBAL,
  PROP_GLOBAL_ID,
  PROP_GLOBAL_PERMISSIONS,
  PROP_GLOBAL_PROPERTIES,
  PROP_INTERFACE_TYPE,
  PROP_INTERFACE_VERSION,
  PROP_LOCAL_OBJECT,
  PROP_FEATURES,
  PROP_PW_PROXY,
  PROP_INFO,
  PROP_PROPERTIES,
};

enum
{
  SIGNAL_PW_PROXY_CREATED,
  SIGNAL_PW_PROXY_DESTROYED,
  SIGNAL_PARAM,
  LAST_SIGNAL,
};

static guint wp_proxy_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_BOXED_TYPE (WpGlobal, wp_global, wp_global_ref, wp_global_unref)
G_DEFINE_TYPE_WITH_PRIVATE (WpProxy, wp_proxy, G_TYPE_OBJECT)

static struct {
  /* the pipewire interface type */
  const char * pw_type;
  /* the minimum interface version that the remote object must support */
  guint32 req_version;
  /* the _get_type() function of the subclass */
  GType (*get_type) (void);
  /* the destroy function of the local object, if any */
  void (*local_object_destroy) (gpointer);
} types_assoc[] = {
  { PW_TYPE_INTERFACE_Core, 0, wp_proxy_get_type, NULL },
  { PW_TYPE_INTERFACE_Registry, 0, wp_proxy_get_type, NULL },
  { PW_TYPE_INTERFACE_Node, 0, wp_proxy_node_get_type, (GDestroyNotify)pw_impl_node_destroy },
  { PW_TYPE_INTERFACE_Port, 0, wp_proxy_port_get_type, NULL },
  { PW_TYPE_INTERFACE_Factory, 0, wp_proxy_get_type, (GDestroyNotify)pw_impl_factory_destroy },
  { PW_TYPE_INTERFACE_Link, 0, wp_proxy_link_get_type, (GDestroyNotify)pw_impl_link_destroy },
  { PW_TYPE_INTERFACE_Client, 0, wp_proxy_client_get_type, (GDestroyNotify)pw_impl_client_destroy },
  { PW_TYPE_INTERFACE_Module, 0, wp_proxy_get_type, (GDestroyNotify)pw_impl_module_destroy },
  { PW_TYPE_INTERFACE_Device, 0, wp_proxy_device_get_type, (GDestroyNotify)pw_impl_device_destroy },
  { PW_TYPE_INTERFACE_Metadata, 0, wp_proxy_get_type, NULL },
  { PW_TYPE_INTERFACE_Session, 0, wp_proxy_session_get_type, NULL },
  { PW_TYPE_INTERFACE_Endpoint, 0, wp_proxy_endpoint_get_type, NULL },
  { PW_TYPE_INTERFACE_EndpointStream, 0, wp_proxy_get_type, NULL },
  { PW_TYPE_INTERFACE_EndpointLink, 0, wp_proxy_get_type, NULL },
  { PW_TYPE_INTERFACE_ClientNode, 0, wp_proxy_get_type, NULL },
  { PW_TYPE_INTERFACE_ClientSession, 0, wp_proxy_get_type, NULL },
  { PW_TYPE_INTERFACE_ClientEndpoint, 0, wp_proxy_get_type, NULL },
};

static inline GType
wp_proxy_find_instance_type (const char * type, guint32 version)
{
  for (gint i = 0; i < SPA_N_ELEMENTS (types_assoc); i++) {
    if (g_strcmp0 (types_assoc[i].pw_type, type) == 0 &&
        types_assoc[i].req_version <= version)
      return types_assoc[i].get_type ();
  }

  return WP_TYPE_PROXY;
}

void
wp_proxy_local_object_destroy_for_type (const char *type, gpointer local_object)
{
  g_return_if_fail (local_object);

  for (gint i = 0; i < SPA_N_ELEMENTS (types_assoc); i++) {
    if (g_strcmp0 (types_assoc[i].pw_type, type) == 0) {
      if (types_assoc[i].local_object_destroy)
         types_assoc[i].local_object_destroy (local_object);
      return;
    }
  }
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

  g_debug ("%s:%p destroyed pw_proxy %p (%s; %s; %u)",
      G_OBJECT_TYPE_NAME (self), self, priv->pw_proxy, priv->iface_type,
      priv->global ? "global" : "not global",
      priv->global ? priv->global->id : 0);
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

static const struct pw_proxy_events proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_event_destroy,
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
  priv->augment_tasks = g_ptr_array_new_with_free_func (g_object_unref);
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

  g_debug ("%s:%p dispose (global %u; pw_proxy %p)",
      G_OBJECT_TYPE_NAME (object), object,
      priv->global ? priv->global->id : 0,
      priv->pw_proxy);

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

  /* Clear the local object */
  if (priv->local_object) {
    wp_proxy_local_object_destroy_for_type (priv->iface_type,
        priv->local_object);
    priv->local_object = NULL;
  }

  g_clear_pointer (&priv->iface_type, g_free);
  g_clear_pointer (&priv->augment_tasks, g_ptr_array_unref);
  g_clear_pointer (&priv->global, wp_global_unref);
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
  case PROP_GLOBAL:
    priv->global = g_value_dup_boxed (value);
    break;
  case PROP_INTERFACE_TYPE:
    priv->iface_type = g_value_dup_string (value);
    break;
  case PROP_INTERFACE_VERSION:
    priv->iface_version = g_value_get_uint (value);
    break;
  case PROP_LOCAL_OBJECT:
    priv->local_object = g_value_get_pointer (value);
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
  WpProxy *self = WP_PROXY (object);
  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&priv->core));
    break;
  case PROP_GLOBAL_ID:
    g_value_set_uint (value, priv->global ? priv->global->id : 0);
    break;
  case PROP_GLOBAL_PERMISSIONS:
    g_value_set_uint (value, priv->global ? priv->global->permissions : 0);
    break;
  case PROP_GLOBAL_PROPERTIES:
    g_value_set_boxed (value, priv->global ? priv->global->properties : NULL);
    break;
  case PROP_INTERFACE_TYPE:
    g_value_set_string (value, priv->iface_type);
    break;
  case PROP_INTERFACE_VERSION:
    g_value_set_uint (value, priv->iface_version);
    break;
  case PROP_LOCAL_OBJECT:
    g_value_set_pointer (value, priv->local_object);
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
    priv->pw_proxy = pw_registry_bind (
        wp_core_get_pw_registry (core), priv->global->id,
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

  g_object_class_install_property (object_class, PROP_GLOBAL,
      g_param_spec_boxed ("global", "global", "Internal WpGlobal object",
          wp_global_get_type (),
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_GLOBAL_ID,
      g_param_spec_uint ("global-id", "global-id",
          "The pipewire global id", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_GLOBAL_PERMISSIONS,
      g_param_spec_uint ("global-permissions", "global-permissions",
          "The pipewire global permissions", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_GLOBAL_PROPERTIES,
      g_param_spec_boxed ("global-properties", "global-properties",
          "The pipewire global properties", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_INTERFACE_TYPE,
      g_param_spec_string ("interface-type", "interface-type",
          "The pipewire interface type", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_INTERFACE_VERSION,
      g_param_spec_uint ("interface-version", "interface-version",
          "The pipewire interface version", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_LOCAL_OBJECT,
      g_param_spec_pointer ("local-object", "local-object",
          "The local object this proxy refers to, if any",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FEATURES,
      g_param_spec_flags ("features", "features",
          "The ready WpProxyFeatures on this proxy", WP_TYPE_PROXY_FEATURES, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_PROXY,
      g_param_spec_pointer ("pw-proxy", "pw-proxy", "The struct pw_proxy *",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_INFO,
      g_param_spec_pointer ("info", "info", "The native info structure",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties",
          "The pipewire properties of the object", WP_TYPE_PROPERTIES,
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

  wp_proxy_signals[SIGNAL_PARAM] = g_signal_new (
      "param", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpProxyClass, param), NULL, NULL, NULL, G_TYPE_NONE, 5,
      G_TYPE_INT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_POINTER);
}

WpProxy *
wp_proxy_new_global (WpCore * core, WpGlobal * global)
{
  GType gtype = wp_proxy_find_instance_type (global->type, global->version);
  return g_object_new (gtype,
      "core", core,
      "global", global,
      "interface-type", global->type,
      "interface-version", global->version,
      NULL);
}

WpProxy *
wp_proxy_new_wrap (WpCore * core, struct pw_proxy * proxy, const char *type,
    guint32 version, gpointer local_object)
{
  GType gtype = wp_proxy_find_instance_type (type, version);
  return g_object_new (gtype,
      "core", core,
      "pw-proxy", proxy,
      "interface-type", type,
      "interface-version", version,
      "local-object", local_object,
      NULL);
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
  guint i;

  g_return_if_fail (WP_IS_PROXY (self));

  priv = wp_proxy_get_instance_private (self);

  /* feature already marked as ready */
  if (priv->ft_ready & feature)
    return;

  priv->ft_ready |= feature;

  g_object_notify (G_OBJECT (self), "features");

  /* return from the task if all the wanted features are now ready */
  for (i = priv->augment_tasks->len; i > 0; i--) {
    GTask *task = g_ptr_array_index (priv->augment_tasks, i - 1);
    WpProxyFeatures wanted = GPOINTER_TO_UINT (g_task_get_task_data (task));

    if ((priv->ft_ready & wanted) == wanted) {
      g_task_return_boolean (task, TRUE);
      /* this is safe as long as we are traversing the array backwards */
      g_ptr_array_remove_index_fast (priv->augment_tasks, i - 1);
    }
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
  guint i;

  g_return_if_fail (WP_IS_PROXY (self));

  priv = wp_proxy_get_instance_private (self);

  for (i = 0; i < priv->augment_tasks->len; i++) {
    GTask *task = g_ptr_array_index (priv->augment_tasks, i);
    g_task_return_error (task, g_error_copy (error));
  }

  g_ptr_array_set_size (priv->augment_tasks, 0);
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

gboolean
wp_proxy_is_global (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), FALSE);

  priv = wp_proxy_get_instance_private (self);
  return priv->global != NULL;
}

guint32
wp_proxy_get_global_id (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), 0);

  priv = wp_proxy_get_instance_private (self);
  return priv->global ? priv->global->id : 0;
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

const char *
wp_proxy_get_interface_type (WpProxy * self)
{
  WpProxyPrivate *priv;

  g_return_val_if_fail (WP_IS_PROXY (self), 0);

  priv = wp_proxy_get_instance_private (self);
  return priv->iface_type;
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

/**
 * wp_proxy_get_info:
 * @self: the proxy
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

/**
 * wp_proxy_enum_params:
 * @self: the proxy
 * @id: the parameter id to enum or PW_ID_ANY for all
 * @start: the start index or 0 for the first param
 * @num: the maximum number of params to retrieve
 * @filter: (nullable): a param filter or NULL
 *
 * Starts enumeration of object parameters. For each param, the
 * #WpProxy::param signal will be emited.
 *
 * This method gives access to the low level `enum_params` method of the object,
 * if it exists. For most use cases, prefer using a higher level API, such as
 * wp_proxy_enum_params_collect() or something from the subclasses of #WpProxy
 *
 * Returns: On success, this returns a sequence number that can be used
 *  to track which emissions of the #WpProxy::param signal are responses
 *  to this call. On failure, it returns a negative number, which could be:
 *  * -EINVAL: An invalid parameter was passed
 *  * -EIO: The object is not ready to receive this method call
 *  * -ENOTSUP: this method is not supported on this object
 */
gint
wp_proxy_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const struct spa_pod *filter)
{
  g_return_val_if_fail (WP_IS_PROXY (self), -EINVAL);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  g_return_val_if_fail (priv->pw_proxy, -EIO);

  return (WP_PROXY_GET_CLASS (self)->enum_params) ?
      WP_PROXY_GET_CLASS (self)->enum_params (self, id, start, num, filter) :
      -ENOTSUP;
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
 * wp_proxy_enum_params_collect:
 * @self: the proxy
 * @id: the parameter id to enum or PW_ID_ANY for all
 * @start: the start index or 0 for the first param
 * @num: the maximum number of params to retrieve
 * @filter: (nullable): a param filter or NULL
 * @cancellable: (nullable): a cancellable for the async operation
 * @callback: (scope async): a callback to call with the result
 * @user_data: (closure): data to pass to @callback
 *
 * Enumerate object parameters. This will asynchronously return the result,
 * or an error, by calling the given @callback. The result is going to
 * be a #GPtrArray containing spa_pod pointers, which can be retrieved
 * with wp_proxy_enum_params_collect_finish().
 */
void
wp_proxy_enum_params_collect (WpProxy * self,
    guint32 id, guint32 start, guint32 num, const struct spa_pod *filter,
    GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data)
{
  g_autoptr (GTask) task = NULL;
  int seq;
  GPtrArray *params;

  g_return_if_fail (WP_IS_PROXY (self));

  /* create task for enum_params */
  task = g_task_new (self, cancellable, callback, user_data);
  params = g_ptr_array_new_with_free_func (free);
  g_task_set_task_data (task, params, (GDestroyNotify) g_ptr_array_unref);

  /* call enum_params */
  seq = wp_proxy_enum_params (self, id, start, num, filter);
  if (G_UNLIKELY (seq < 0)) {
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
 * wp_proxy_enum_params_collect_finish:
 *
 * Returns: (transfer full) (element-type spa_pod*):
 *    the collected params
 */
GPtrArray *
wp_proxy_enum_params_collect_finish (WpProxy * self,
    GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);
  g_return_val_if_fail (g_task_is_valid (res, self), NULL);

  return g_task_propagate_pointer (G_TASK (res), error);
}

/**
 * wp_proxy_subscribe_params: (skip)
 * @self: the proxy
 * @n_ids: the number of IDs specified in the the variable arugments list
 *
 * Sets up the proxy to automatically emit the #WpProxy::param signal for
 * the given ids when they are changed.
 *
 * Returns: A positive number or zero on success. On failure, it returns
 *   a negative number. Well known failure codes are:
 *   * -EINVAL: An invalid parameter was passed
 *   * -EIO: The object is not ready to receive this method call
 *   * -ENOTSUP: this method is not supported on this object
 */
gint
wp_proxy_subscribe_params (WpProxy * self, guint32 n_ids, ...)
{
  g_return_val_if_fail (WP_IS_PROXY (self), -EINVAL);
  g_return_val_if_fail (n_ids != 0, -EINVAL);

  va_list args;
  guint32 *ids = g_alloca (n_ids * sizeof (guint32));

  va_start (args, n_ids);
  for (gint i = 0; i < n_ids; i++)
    ids[i] = va_arg (args, guint32);
  va_end (args);

  return wp_proxy_subscribe_params_array (self, n_ids, ids);
}

/**
 * wp_proxy_subscribe_params_array: (rename-to wp_proxy_subscribe_params)
 * @self: the proxy
 * @n_ids: the number of IDs specified in @ids
 * @ids: (array length=n_ids): a list of param IDs to subscribe to
 *
 * Sets up the proxy to automatically emit the #WpProxy::param signal for
 * the given ids when they are changed.
 *
 * Returns: A positive number or zero on success. On failure, it returns
 *   a negative number. Well known failure codes are:
 *   * -EINVAL: An invalid parameter was passed
 *   * -EIO: The object is not ready to receive this method call
 *   * -ENOTSUP: this method is not supported on this object
 */
gint
wp_proxy_subscribe_params_array (WpProxy * self, guint32 n_ids, guint32 *ids)
{
  g_return_val_if_fail (WP_IS_PROXY (self), -EINVAL);
  g_return_val_if_fail (n_ids != 0, -EINVAL);
  g_return_val_if_fail (ids != NULL, -EINVAL);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  g_return_val_if_fail (priv->pw_proxy, -EIO);

  return (WP_PROXY_GET_CLASS (self)->subscribe_params) ?
      WP_PROXY_GET_CLASS (self)->subscribe_params (self, n_ids, ids) :
      -ENOTSUP;
}

/**
 * wp_proxy_set_param:
 * @self: the proxy
 * @id: the parameter id to set
 * @flags: extra parameter flags
 * @param: the parameter to set
 *
 * Sets a parameter on the object.
 *
 * Returns: A positive number or zero on success. On failure, it returns
 *   a negative number. Well known failure codes are:
 *   * -EINVAL: An invalid parameter was passed
 *   * -EIO: The object is not ready to receive this method call
 *   * -ENOTSUP: this method is not supported on this object
 */
gint
wp_proxy_set_param (WpProxy * self, guint32 id, guint32 flags,
    const struct spa_pod *param)
{
  g_return_val_if_fail (WP_IS_PROXY (self), -EINVAL);

  WpProxyPrivate *priv = wp_proxy_get_instance_private (self);
  g_return_val_if_fail (priv->pw_proxy, -EIO);

  return (WP_PROXY_GET_CLASS (self)->set_param) ?
      WP_PROXY_GET_CLASS (self)->set_param (self, id, flags, param) :
      -ENOTSUP;
}

void
wp_proxy_handle_event_param (void * proxy, int seq, uint32_t id,
    uint32_t index, uint32_t next, const struct spa_pod *param)
{
  WpProxy *self = WP_PROXY (proxy);
  GTask *task;

  g_signal_emit (self, wp_proxy_signals[SIGNAL_PARAM], 0, seq, id, index, next,
      param);

  /* if this param event was emited because of enum_params_collect(),
   * copy the param in the result array of that API */
  task = wp_proxy_find_async_task (self, seq, FALSE);
  if (task) {
    GPtrArray *array = g_task_get_task_data (task);
    g_ptr_array_add (array, spa_pod_copy (param));
  }
}
