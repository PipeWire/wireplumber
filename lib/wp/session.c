/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "session.h"
#include "private.h"
#include "wpenums.h"

#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>

enum {
  PROXY_PROP_0,
  PROXY_PROP_INFO,
  PROXY_PROP_PROPERTIES,
};

enum {
  EXPORTED_PROP_0,
  EXPORTED_PROP_GLOBAL_ID,
  EXPORTED_PROP_PROPERTIES,
};

enum {
  SIGNAL_DEFAULT_ENDPOINT_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

/* helpers */

static struct pw_session_info *
session_info_update (struct pw_session_info *info,
    WpProperties ** props_storage,
    const struct pw_session_info *update)
{
  if (update == NULL)
    return info;

  if (info == NULL) {
    info = calloc(1, sizeof(struct pw_session_info));
    if (info == NULL)
      return NULL;

    info->id = update->id;
  }
  info->change_mask = update->change_mask;

  if (update->change_mask & PW_SESSION_CHANGE_MASK_PROPS) {
    if (*props_storage)
      wp_properties_unref (*props_storage);
    *props_storage = wp_properties_new_copy_dict (update->props);
    info->props = (struct spa_dict *) wp_properties_peek_dict (*props_storage);
  }
  if (update->change_mask & PW_SESSION_CHANGE_MASK_PARAMS) {
    info->n_params = update->n_params;
    free((void *) info->params);
    if (update->params) {
      size_t size = info->n_params * sizeof(struct spa_param_info);
      info->params = malloc(size);
      memcpy(info->params, update->params, size);
    }
    else
      info->params = NULL;
  }
  return info;
}

static void
session_info_free (struct pw_session_info *info)
{
  free((void *) info->params);
  free(info);
}

/* interface */

G_DEFINE_INTERFACE (WpSession, wp_session, G_TYPE_OBJECT)

static void
wp_session_default_init (WpSessionInterface * klass)
{
  g_object_interface_install_property (klass,
      g_param_spec_boxed ("properties", "properties",
          "The pipewire properties of the object", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED] = g_signal_new (
      "default-endpoint-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
      WP_TYPE_DEFAULT_ENDPOINT_TYPE, G_TYPE_UINT);
}

WpProperties *
wp_session_get_properties (WpSession * self)
{
  g_return_val_if_fail (WP_IS_SESSION (self), NULL);
  g_return_val_if_fail (WP_SESSION_GET_IFACE (self)->get_properties, NULL);

  return WP_SESSION_GET_IFACE (self)->get_properties (self);
}

guint32
wp_session_get_default_endpoint (WpSession * self,
    WpDefaultEndpointType type)
{
  g_return_val_if_fail (WP_IS_SESSION (self), 0);
  g_return_val_if_fail (WP_SESSION_GET_IFACE (self)->get_default_endpoint, 0);

  return WP_SESSION_GET_IFACE (self)->get_default_endpoint (self, type);
}

void
wp_session_set_default_endpoint (WpSession * self,
    WpDefaultEndpointType type, guint32 id)
{
  g_return_if_fail (WP_IS_SESSION (self));
  g_return_if_fail (WP_SESSION_GET_IFACE (self)->set_default_endpoint);

  WP_SESSION_GET_IFACE (self)->set_default_endpoint (self, type, id);
}

/* proxy */

struct _WpProxySession
{
  WpProxy parent;

  WpProperties *properties;
  WpSpaProps spa_props;
  struct pw_session_info *info;
  struct spa_hook listener;
};

static void wp_proxy_session_iface_init (WpSessionInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpProxySession, wp_proxy_session, WP_TYPE_PROXY,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SESSION, wp_proxy_session_iface_init))

static void
wp_proxy_session_init (WpProxySession * self)
{
}

static void
wp_proxy_session_finalize (GObject * object)
{
  WpProxySession *self = WP_PROXY_SESSION (object);

  g_clear_pointer (&self->info, session_info_free);
  g_clear_pointer (&self->properties, wp_properties_unref);
  wp_spa_props_clear (&self->spa_props);

  G_OBJECT_CLASS (wp_proxy_session_parent_class)->finalize (object);
}

static void
wp_proxy_session_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpProxySession *self = WP_PROXY_SESSION (object);

  switch (property_id) {
  case PROXY_PROP_INFO:
    g_value_set_pointer (value, self->info);
    break;
  case PROXY_PROP_PROPERTIES:
    g_value_set_boxed (value, self->properties);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
session_event_info (void *data, const struct pw_session_info *info)
{
  WpProxySession *self = WP_PROXY_SESSION (data);

  self->info = session_info_update (self->info, &self->properties, info);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_SESSION_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
}

static void
session_event_param (void *data, int seq, uint32_t id, uint32_t index,
    uint32_t next, const struct spa_pod *param)
{
  WpProxySession *self = WP_PROXY_SESSION (data);
  g_autoptr (GArray) changed_ids = NULL;
  guint32 prop_id;
  gint32 value;

  switch (id) {
  case SPA_PARAM_PropInfo:
    wp_spa_props_register_from_prop_info (&self->spa_props, param);
    break;
  case SPA_PARAM_Props:
    changed_ids = g_array_new (FALSE, FALSE, sizeof (uint32_t));
    wp_spa_props_store_from_props (&self->spa_props, param, changed_ids);

    for (guint i = 0; i < changed_ids->len; i++) {
      prop_id = g_array_index (changed_ids, uint32_t, i);
      param = wp_spa_props_get_stored (&self->spa_props, prop_id);
      if (spa_pod_get_int (param, &value) == 0) {
        g_signal_emit (self, signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED], 0,
            prop_id, value);
      }
    }

    wp_proxy_set_feature_ready (WP_PROXY (self),
        WP_PROXY_SESSION_FEATURE_DEFAULT_ENDPOINT);
    break;
  }
}

static const struct pw_session_proxy_events session_events = {
  PW_VERSION_SESSION_PROXY_EVENTS,
  .info = session_event_info,
  .param = session_event_param,
};

static void
wp_proxy_session_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpProxySession *self = WP_PROXY_SESSION (proxy);
  pw_session_proxy_add_listener ((struct pw_session_proxy *) pw_proxy,
      &self->listener, &session_events, self);
}

static void
wp_proxy_session_augment (WpProxy * proxy, WpProxyFeatures features)
{
  /* call the parent impl first to ensure we have a pw proxy if necessary */
  WP_PROXY_CLASS (wp_proxy_session_parent_class)->augment (proxy, features);

  if (features & WP_PROXY_SESSION_FEATURE_DEFAULT_ENDPOINT) {
    struct pw_session_proxy *pw_proxy = NULL;
    uint32_t ids[] = { SPA_PARAM_Props };

    pw_proxy = (struct pw_session_proxy *) wp_proxy_get_pw_proxy (proxy);
    if (!pw_proxy)
      return;

    pw_session_proxy_enum_params (pw_proxy, 0, SPA_PARAM_PropInfo, 0, -1, NULL);
    pw_session_proxy_subscribe_params (pw_proxy, ids, SPA_N_ELEMENTS (ids));
  }
}

static WpProperties *
wp_proxy_session_get_properties (WpSession * session)
{
  WpProxySession *self = WP_PROXY_SESSION (session);
  return wp_properties_ref (self->properties);
}

static guint32
wp_proxy_session_get_default_endpoint (WpSession * session,
    WpDefaultEndpointType type)
{
  WpProxySession *self = WP_PROXY_SESSION (session);
  const struct spa_pod *pod;
  gint32 value;

  pod = wp_spa_props_get_stored (&self->spa_props, type);
  if (pod && spa_pod_get_int (pod, &value) == 0)
    return (guint32) value;
  return 0;
}

static void
wp_proxy_session_set_default_endpoint (WpSession * session,
    WpDefaultEndpointType type, guint32 id)
{
  WpProxySession *self = WP_PROXY_SESSION (session);
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buf, sizeof (buf));
  struct pw_session_proxy *pw_proxy = NULL;

  /* set the default endpoint id as a property param on the session;
     our spa_props cache will be updated by the param event */

  pw_proxy = (struct pw_session_proxy *) wp_proxy_get_pw_proxy (WP_PROXY (self));
  pw_session_proxy_set_param (pw_proxy,
      SPA_PARAM_Props, 0,
      spa_pod_builder_add_object (&b,
          SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
          type, SPA_POD_Int (id)));
}

static void
wp_proxy_session_class_init (WpProxySessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_proxy_session_finalize;
  object_class->get_property = wp_proxy_session_get_property;

  proxy_class->pw_proxy_created = wp_proxy_session_pw_proxy_created;
  proxy_class->augment = wp_proxy_session_augment;

  g_object_class_install_property (object_class, PROXY_PROP_INFO,
      g_param_spec_pointer ("info", "info", "The native info structure",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_override_property (object_class, PROXY_PROP_PROPERTIES,
      "properties");
}

static void
wp_proxy_session_iface_init (WpSessionInterface * iface)
{
  iface->get_properties = wp_proxy_session_get_properties;
  iface->get_default_endpoint = wp_proxy_session_get_default_endpoint;
  iface->set_default_endpoint = wp_proxy_session_set_default_endpoint;
}

const struct pw_session_info *
wp_proxy_session_get_info (WpProxySession * self)
{
  g_return_val_if_fail (WP_IS_PROXY_SESSION (self), NULL);
  return self->info;
}

/* exported */

typedef struct _WpExportedSessionPrivate WpExportedSessionPrivate;
struct _WpExportedSessionPrivate
{
  WpProxy *client_sess;
  struct spa_hook listener;
  struct spa_hook proxy_listener;
  struct pw_session_info info;
  struct spa_param_info param_info[2];
  WpProperties *properties;
  WpSpaProps spa_props;
};

static void wp_exported_session_iface_init (WpSessionInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpExportedSession, wp_exported_session, WP_TYPE_EXPORTED,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SESSION, wp_exported_session_iface_init)
    G_ADD_PRIVATE (WpExportedSession))

static void
wp_exported_session_init (WpExportedSession * self)
{
  WpExportedSessionPrivate *priv =
      wp_exported_session_get_instance_private (WP_EXPORTED_SESSION (self));

  priv->properties = wp_properties_new_empty ();

  priv->param_info[0] = SPA_PARAM_INFO (SPA_PARAM_Props, SPA_PARAM_INFO_READWRITE);
  priv->param_info[1] = SPA_PARAM_INFO (SPA_PARAM_PropInfo, SPA_PARAM_INFO_READ);

  priv->info.version = PW_VERSION_SESSION_INFO;
  priv->info.props = (struct spa_dict *) wp_properties_peek_dict (priv->properties);
  priv->info.params = priv->param_info;
  priv->info.n_params = SPA_N_ELEMENTS (priv->param_info);

  wp_spa_props_register (&priv->spa_props, WP_DEFAULT_ENDPOINT_TYPE_AUDIO_SOURCE,
      "Default Audio Source", SPA_POD_Int (0));
  wp_spa_props_register (&priv->spa_props, WP_DEFAULT_ENDPOINT_TYPE_AUDIO_SINK,
      "Default Audio Sink", SPA_POD_Int (0));
  wp_spa_props_register (&priv->spa_props, WP_DEFAULT_ENDPOINT_TYPE_VIDEO_SOURCE,
      "Default Video Source", SPA_POD_Int (0));
}

static void
wp_exported_session_finalize (GObject * object)
{
  WpExportedSessionPrivate *priv =
      wp_exported_session_get_instance_private (WP_EXPORTED_SESSION (object));

  g_clear_pointer (&priv->properties, wp_properties_unref);
  wp_spa_props_clear (&priv->spa_props);

  G_OBJECT_CLASS (wp_exported_session_parent_class)->finalize (object);
}

static void
wp_exported_session_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpExportedSessionPrivate *priv =
      wp_exported_session_get_instance_private (WP_EXPORTED_SESSION (object));

  switch (property_id) {
  case EXPORTED_PROP_GLOBAL_ID:
    g_value_set_uint (value, priv->info.id);
    break;
  case EXPORTED_PROP_PROPERTIES:
    g_value_set_boxed (value, priv->properties);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
client_session_update (WpExportedSession * self, guint32 change_mask,
    guint32 info_change_mask)
{
  WpExportedSessionPrivate *priv =
      wp_exported_session_get_instance_private (self);
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buf, sizeof (buf));
  struct pw_client_session_proxy *pw_proxy = NULL;
  struct pw_session_info *info = NULL;
  g_autoptr (GPtrArray) params = NULL;

  pw_proxy = (struct pw_client_session_proxy *) wp_proxy_get_pw_proxy (
      priv->client_sess);

  if (change_mask & PW_CLIENT_SESSION_UPDATE_PARAMS) {
    params = wp_spa_props_build_all_pods (&priv->spa_props, &b);
  }
  if (change_mask & PW_CLIENT_SESSION_UPDATE_INFO) {
    info = &priv->info;
    info->change_mask = info_change_mask;
  }

  pw_client_session_proxy_update (pw_proxy,
      change_mask,
      params ? params->len : 0,
      (const struct spa_pod **) (params ? params->pdata : NULL),
      info);

  if (info)
    info->change_mask = 0;
}

static int
client_session_set_param (void *object,
    uint32_t id, uint32_t flags, const struct spa_pod *param)
{
  WpExportedSession *self = WP_EXPORTED_SESSION (object);
  WpExportedSessionPrivate *priv =
      wp_exported_session_get_instance_private (self);
  g_autoptr (GArray) changed_ids = NULL;
  guint32 prop_id;
  gint32 value;

  if (id != SPA_PARAM_Props)
    return -ENOENT;

  changed_ids = g_array_new (FALSE, FALSE, sizeof (guint32));
  wp_spa_props_store_from_props (&priv->spa_props, param, changed_ids);

  for (guint i = 0; i < changed_ids->len; i++) {
    prop_id = g_array_index (changed_ids, guint32, i);
    param = wp_spa_props_get_stored (&priv->spa_props, prop_id);
    if (spa_pod_get_int (param, &value) == 0) {
      g_signal_emit (self, signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED], 0,
          prop_id, value);
    }
  }

  client_session_update (self, PW_CLIENT_SESSION_UPDATE_PARAMS, 0);

  return 0;
}

static void
client_session_proxy_bound (void *object, uint32_t global_id)
{
  WpExportedSession *self = WP_EXPORTED_SESSION (object);
  WpExportedSessionPrivate *priv =
      wp_exported_session_get_instance_private (self);

  priv->info.id = global_id;
  wp_exported_notify_export_done (WP_EXPORTED (self), NULL);
}

static struct pw_client_session_proxy_events client_session_events = {
  PW_VERSION_CLIENT_SESSION_PROXY_EVENTS,
  .set_param = client_session_set_param,
};

static struct pw_proxy_events client_sess_proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .bound = client_session_proxy_bound,
};

static void
wp_exported_session_export (WpExported * self)
{
  WpExportedSessionPrivate *priv =
      wp_exported_session_get_instance_private (WP_EXPORTED_SESSION (self));
  g_autoptr (WpCore) core = wp_exported_get_core (self);
  struct pw_client_session_proxy *pw_proxy = NULL;

  priv->client_sess = wp_core_create_remote_object (core, "client-session",
      PW_TYPE_INTERFACE_ClientSession, PW_VERSION_CLIENT_SESSION_PROXY,
      priv->properties);

  pw_proxy = (struct pw_client_session_proxy *) wp_proxy_get_pw_proxy (
      priv->client_sess);

  pw_client_session_proxy_add_listener (pw_proxy, &priv->listener,
      &client_session_events, self);
  pw_proxy_add_listener ((struct pw_proxy *) pw_proxy, &priv->proxy_listener,
      &client_sess_proxy_events, self);

  client_session_update (WP_EXPORTED_SESSION (self),
      PW_CLIENT_SESSION_UPDATE_PARAMS | PW_CLIENT_SESSION_UPDATE_INFO,
      PW_SESSION_CHANGE_MASK_ALL);
}

static void
wp_exported_session_unexport (WpExported * self)
{
  WpExportedSessionPrivate *priv =
      wp_exported_session_get_instance_private (WP_EXPORTED_SESSION (self));

  g_clear_object (&priv->client_sess);
  priv->info.id = 0;
}

static WpProxy *
wp_exported_session_get_proxy (WpExported * self)
{
  WpExportedSessionPrivate *priv =
      wp_exported_session_get_instance_private (WP_EXPORTED_SESSION (self));

  return priv->client_sess ? g_object_ref (priv->client_sess) : NULL;
}

static WpProperties *
wp_exported_session_get_properties (WpSession * session)
{
  WpExportedSessionPrivate *priv =
      wp_exported_session_get_instance_private (WP_EXPORTED_SESSION (session));

  return wp_properties_ref (priv->properties);
}

static guint32
wp_exported_session_get_default_endpoint (WpSession * session,
    WpDefaultEndpointType type)
{
  WpExportedSessionPrivate *priv =
      wp_exported_session_get_instance_private (WP_EXPORTED_SESSION (session));
  const struct spa_pod *pod;
  gint32 value;

  pod = wp_spa_props_get_stored (&priv->spa_props, type);
  if (pod && spa_pod_get_int (pod, &value) == 0)
    return (guint32) value;
  return 0;
}

static void
wp_exported_session_set_default_endpoint (WpSession * session,
    WpDefaultEndpointType type, guint32 id)
{
  WpExportedSessionPrivate *priv =
      wp_exported_session_get_instance_private (WP_EXPORTED_SESSION (session));

  wp_spa_props_store (&priv->spa_props, type, SPA_POD_Int (id));

  g_signal_emit (session, signals[SIGNAL_DEFAULT_ENDPOINT_CHANGED], 0, type, id);

  /* update only after the session has been exported */
  if (priv->info.id != 0) {
    client_session_update (WP_EXPORTED_SESSION (session),
      PW_CLIENT_SESSION_UPDATE_PARAMS, 0);
  }
}

static void
wp_exported_session_class_init (WpExportedSessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpExportedClass *exported_class = (WpExportedClass *) klass;

  object_class->finalize = wp_exported_session_finalize;
  object_class->get_property = wp_exported_session_get_property;

  exported_class->export = wp_exported_session_export;
  exported_class->unexport = wp_exported_session_unexport;
  exported_class->get_proxy = wp_exported_session_get_proxy;

  g_object_class_install_property (object_class, EXPORTED_PROP_GLOBAL_ID,
      g_param_spec_uint ("global-id", "global-id",
          "The pipewire global id of the exported session", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_override_property (object_class, EXPORTED_PROP_PROPERTIES,
      "properties");
}

static void
wp_exported_session_iface_init (WpSessionInterface * iface)
{
  iface->get_properties = wp_exported_session_get_properties;
  iface->get_default_endpoint = wp_exported_session_get_default_endpoint;
  iface->set_default_endpoint = wp_exported_session_set_default_endpoint;
}

WpExportedSession *
wp_exported_session_new (WpCore * core)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_EXPORTED_SESSION,
      "core", core,
      NULL);
}

/**
 * wp_exported_session_get_global_id: (method)
 * @self: the session
 *
 * Returns: the pipewire global id of the exported session object. This
 *   is only valid after the wp_exported_export() async operation has finished.
 */
guint32
wp_exported_session_get_global_id (WpExportedSession * self)
{
  WpExportedSessionPrivate *priv;

  g_return_val_if_fail (WP_IS_EXPORTED_SESSION (self), 0);
  priv = wp_exported_session_get_instance_private (WP_EXPORTED_SESSION (self));

  return priv->info.id;
}

void
wp_exported_session_set_property (WpExportedSession * self,
    const gchar * key, const gchar * value)
{
  WpExportedSessionPrivate *priv;

  g_return_if_fail (WP_IS_EXPORTED_SESSION (self));
  priv = wp_exported_session_get_instance_private (WP_EXPORTED_SESSION (self));

  wp_properties_set (priv->properties, key, value);

  g_object_notify (G_OBJECT (self), "properties");

  /* update only after the session has been exported */
  if (priv->info.id != 0) {
    client_session_update (WP_EXPORTED_SESSION (self),
      PW_CLIENT_SESSION_UPDATE_INFO, PW_SESSION_CHANGE_MASK_PROPS);
  }
}

void
wp_exported_session_update_properties (WpExportedSession * self,
    WpProperties * updates)
{
  WpExportedSessionPrivate *priv;

  g_return_if_fail (WP_IS_EXPORTED_SESSION (self));
  priv = wp_exported_session_get_instance_private (WP_EXPORTED_SESSION (self));

  wp_properties_update_from_dict (priv->properties,
      wp_properties_peek_dict (updates));

  g_object_notify (G_OBJECT (self), "properties");

  /* update only after the session has been exported */
  if (priv->info.id != 0) {
    client_session_update (WP_EXPORTED_SESSION (self),
      PW_CLIENT_SESSION_UPDATE_INFO, PW_SESSION_CHANGE_MASK_PROPS);
  }
}
