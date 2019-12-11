/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "endpoint.h"
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
  SIGNAL_CONTROL_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

/* helpers */

static struct pw_endpoint_info *
endpoint_info_update (struct pw_endpoint_info *info,
    WpProperties ** props_storage,
    const struct pw_endpoint_info *update)
{
  if (update == NULL)
    return info;

  if (info == NULL) {
    info = calloc(1, sizeof(struct pw_endpoint_info));
    if (info == NULL)
      return NULL;

    info->id = update->id;
    info->name = g_strdup(update->name);
    info->media_class = g_strdup(update->media_class);
    info->direction = update->direction;
    info->flags = update->flags;
  }
  info->change_mask = update->change_mask;

  if (update->change_mask & PW_ENDPOINT_CHANGE_MASK_STREAMS)
    info->n_streams = update->n_streams;

  if (update->change_mask & PW_ENDPOINT_CHANGE_MASK_SESSION)
    info->session_id = update->session_id;

  if (update->change_mask & PW_ENDPOINT_CHANGE_MASK_PROPS) {
    if (*props_storage)
      wp_properties_unref (*props_storage);
    *props_storage = wp_properties_new_copy_dict (update->props);
    info->props = (struct spa_dict *) wp_properties_peek_dict (*props_storage);
  }
  if (update->change_mask & PW_ENDPOINT_CHANGE_MASK_PARAMS) {
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
endpoint_info_free (struct pw_endpoint_info *info)
{
  g_free(info->name);
  g_free(info->media_class);
  free((void *) info->params);
  free(info);
}

/* interface */

G_DEFINE_INTERFACE (WpEndpoint, wp_endpoint, G_TYPE_OBJECT)

static void
wp_endpoint_default_init (WpEndpointInterface * klass)
{
  g_object_interface_install_property (klass,
      g_param_spec_boxed ("properties", "properties",
          "The pipewire properties of the object", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_CONTROL_CHANGED] = g_signal_new (
      "control-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

WpProperties *
wp_endpoint_get_properties (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_IFACE (self)->get_properties, NULL);

  return WP_ENDPOINT_GET_IFACE (self)->get_properties (self);
}

const gchar *
wp_endpoint_get_name (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_IFACE (self)->get_name, NULL);

  return WP_ENDPOINT_GET_IFACE (self)->get_name (self);
}

const gchar *
wp_endpoint_get_media_class (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_IFACE (self)->get_media_class, NULL);

  return WP_ENDPOINT_GET_IFACE (self)->get_media_class (self);
}

WpDirection
wp_endpoint_get_direction (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), 0);
  g_return_val_if_fail (WP_ENDPOINT_GET_IFACE (self)->get_direction, 0);

  return WP_ENDPOINT_GET_IFACE (self)->get_direction (self);
}

const struct spa_pod *
wp_endpoint_get_control (WpEndpoint * self, guint32 control_id)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_IFACE (self)->get_control, NULL);

  return WP_ENDPOINT_GET_IFACE (self)->get_control (self, control_id);
}

gboolean
wp_endpoint_get_control_boolean (WpEndpoint * self, guint32 control_id,
    gboolean * value)
{
  const struct spa_pod *pod = wp_endpoint_get_control (self, control_id);
  bool val;
  if (pod && spa_pod_get_bool (pod, &val) == 0) {
    *value = val;
    return TRUE;
  }
  return FALSE;
}

gboolean
wp_endpoint_get_control_int (WpEndpoint * self, guint32 control_id,
    gint * value)
{
  const struct spa_pod *pod = wp_endpoint_get_control (self, control_id);
  return (pod && spa_pod_get_int (pod, value) == 0);
}

gboolean
wp_endpoint_get_control_float (WpEndpoint * self, guint32 control_id,
    gfloat * value)
{
  const struct spa_pod *pod = wp_endpoint_get_control (self, control_id);
  return (pod && spa_pod_get_float (pod, value) == 0);
}

gboolean
wp_endpoint_set_control (WpEndpoint * self, guint32 control_id,
    const struct spa_pod * value)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), FALSE);
  g_return_val_if_fail (WP_ENDPOINT_GET_IFACE (self)->get_properties, FALSE);

  return WP_ENDPOINT_GET_IFACE (self)->set_control (self, control_id, value);
}

gboolean
wp_endpoint_set_control_boolean (WpEndpoint * self, guint32 control_id,
    gboolean value)
{
  gchar buffer[512];
  return wp_endpoint_set_control (self, control_id, wp_spa_props_build_pod (
          buffer, sizeof (buffer), SPA_POD_Bool (value), 0));
}

gboolean
wp_endpoint_set_control_int (WpEndpoint * self, guint32 control_id,
    gint value)
{
  gchar buffer[512];
  return wp_endpoint_set_control (self, control_id, wp_spa_props_build_pod (
          buffer, sizeof (buffer), SPA_POD_Int (value), 0));
}

gboolean
wp_endpoint_set_control_float (WpEndpoint * self, guint32 control_id,
    gfloat value)
{
  gchar buffer[512];
  return wp_endpoint_set_control (self, control_id, wp_spa_props_build_pod (
          buffer, sizeof (buffer), SPA_POD_Float (value), 0));
}

/* proxy */

struct _WpProxyEndpoint
{
  WpProxy parent;

  WpProperties *properties;
  WpSpaProps spa_props;
  struct pw_endpoint_info *info;
  struct spa_hook listener;
};

static void wp_proxy_endpoint_iface_init (WpEndpointInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpProxyEndpoint, wp_proxy_endpoint, WP_TYPE_PROXY,
    G_IMPLEMENT_INTERFACE (WP_TYPE_ENDPOINT, wp_proxy_endpoint_iface_init))

static void
wp_proxy_endpoint_init (WpProxyEndpoint * self)
{
}

static void
wp_proxy_endpoint_finalize (GObject * object)
{
  WpProxyEndpoint *self = WP_PROXY_ENDPOINT (object);

  g_clear_pointer (&self->info, endpoint_info_free);
  g_clear_pointer (&self->properties, wp_properties_unref);
  wp_spa_props_clear (&self->spa_props);

  G_OBJECT_CLASS (wp_proxy_endpoint_parent_class)->finalize (object);
}

static void
wp_proxy_endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpProxyEndpoint *self = WP_PROXY_ENDPOINT (object);

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
endpoint_event_info (void *data, const struct pw_endpoint_info *info)
{
  WpProxyEndpoint *self = WP_PROXY_ENDPOINT (data);

  self->info = endpoint_info_update (self->info, &self->properties, info);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_ENDPOINT_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
}

static void
endpoint_event_param (void *data, int seq, uint32_t id, uint32_t index,
    uint32_t next, const struct spa_pod *param)
{
  WpProxyEndpoint *self = WP_PROXY_ENDPOINT (data);
  g_autoptr (GArray) changed_ids = NULL;
  guint32 prop_id;

  switch (id) {
  case SPA_PARAM_PropInfo:
    wp_spa_props_register_from_prop_info (&self->spa_props, param);
    break;
  case SPA_PARAM_Props:
    changed_ids = g_array_new (FALSE, FALSE, sizeof (uint32_t));
    wp_spa_props_store_from_props (&self->spa_props, param, changed_ids);

    for (guint i = 0; i < changed_ids->len; i++) {
      prop_id = g_array_index (changed_ids, uint32_t, i);
      g_signal_emit (self, signals[SIGNAL_CONTROL_CHANGED], 0, prop_id);
    }

    wp_proxy_set_feature_ready (WP_PROXY (self),
        WP_PROXY_ENDPOINT_FEATURE_CONTROLS);
    break;
  }
}

static const struct pw_endpoint_proxy_events endpoint_events = {
  PW_VERSION_ENDPOINT_PROXY_EVENTS,
  .info = endpoint_event_info,
  .param = endpoint_event_param,
};

static void
wp_proxy_endpoint_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpProxyEndpoint *self = WP_PROXY_ENDPOINT (proxy);
  pw_endpoint_proxy_add_listener ((struct pw_endpoint_proxy *) pw_proxy,
      &self->listener, &endpoint_events, self);
}

static void
wp_proxy_endpoint_augment (WpProxy * proxy, WpProxyFeatures features)
{
  /* call the parent impl first to ensure we have a pw proxy if necessary */
  WP_PROXY_CLASS (wp_proxy_endpoint_parent_class)->augment (proxy, features);

  if (features & WP_PROXY_ENDPOINT_FEATURE_CONTROLS) {
    struct pw_endpoint_proxy *pw_proxy = NULL;
    uint32_t ids[] = { SPA_PARAM_Props };

    pw_proxy = (struct pw_endpoint_proxy *) wp_proxy_get_pw_proxy (proxy);
    if (!pw_proxy)
      return;

    pw_endpoint_proxy_enum_params (pw_proxy, 0, SPA_PARAM_PropInfo, 0, -1, NULL);
    pw_endpoint_proxy_subscribe_params (pw_proxy, ids, SPA_N_ELEMENTS (ids));
  }
}

static WpProperties *
wp_proxy_endpoint_get_properties (WpEndpoint * endpoint)
{
  WpProxyEndpoint *self = WP_PROXY_ENDPOINT (endpoint);
  return wp_properties_ref (self->properties);
}

const gchar *
wp_proxy_endpoint_get_name (WpEndpoint * endpoint)
{
  WpProxyEndpoint *self = WP_PROXY_ENDPOINT (endpoint);
  return self->info->name;
}

const gchar *
wp_proxy_endpoint_get_media_class (WpEndpoint * endpoint)
{
  WpProxyEndpoint *self = WP_PROXY_ENDPOINT (endpoint);
  return self->info->media_class;
}

WpDirection
wp_proxy_endpoint_get_direction (WpEndpoint * endpoint)
{
  WpProxyEndpoint *self = WP_PROXY_ENDPOINT (endpoint);
  return self->info->direction;
}

static const struct spa_pod *
wp_proxy_endpoint_get_control (WpEndpoint * endpoint, guint32 control_id)
{
  WpProxyEndpoint *self = WP_PROXY_ENDPOINT (endpoint);
  return wp_spa_props_get_stored (&self->spa_props, control_id);
}

static gboolean
wp_proxy_endpoint_set_control (WpEndpoint * endpoint, guint32 control_id,
    const struct spa_pod * pod)
{
  WpProxyEndpoint *self = WP_PROXY_ENDPOINT (endpoint);
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buf, sizeof (buf));
  struct pw_endpoint_proxy *pw_proxy = NULL;

  /* set the default endpoint id as a property param on the endpoint;
     our spa_props will be updated by the param event */

  pw_proxy = (struct pw_endpoint_proxy *) wp_proxy_get_pw_proxy (WP_PROXY (self));
  pw_endpoint_proxy_set_param (pw_proxy,
      SPA_PARAM_Props, 0,
      spa_pod_builder_add_object (&b,
          SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
          control_id, SPA_POD_Pod (pod)));

  return TRUE;
}

static void
wp_proxy_endpoint_class_init (WpProxyEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_proxy_endpoint_finalize;
  object_class->get_property = wp_proxy_endpoint_get_property;

  proxy_class->pw_proxy_created = wp_proxy_endpoint_pw_proxy_created;
  proxy_class->augment = wp_proxy_endpoint_augment;

  g_object_class_install_property (object_class, PROXY_PROP_INFO,
      g_param_spec_pointer ("info", "info", "The native info structure",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_override_property (object_class, PROXY_PROP_PROPERTIES,
      "properties");
}

static void
wp_proxy_endpoint_iface_init (WpEndpointInterface * iface)
{
  iface->get_properties = wp_proxy_endpoint_get_properties;
  iface->get_name = wp_proxy_endpoint_get_name;
  iface->get_media_class = wp_proxy_endpoint_get_media_class;
  iface->get_direction = wp_proxy_endpoint_get_direction;
  iface->get_control = wp_proxy_endpoint_get_control;
  iface->set_control = wp_proxy_endpoint_set_control;
}

const struct pw_endpoint_info *
wp_proxy_endpoint_get_info (WpProxyEndpoint * self)
{
  g_return_val_if_fail (WP_IS_PROXY_ENDPOINT (self), NULL);
  return self->info;
}

/* exported */

typedef struct _WpExportedEndpointPrivate WpExportedEndpointPrivate;
struct _WpExportedEndpointPrivate
{
  WpProxy *client_ep;
  struct spa_hook listener;
  struct spa_hook proxy_listener;
  struct pw_endpoint_info info;
  struct spa_param_info param_info[2];
  WpProperties *properties;
  WpSpaProps spa_props;
};

static void wp_exported_endpoint_iface_init (WpEndpointInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpExportedEndpoint, wp_exported_endpoint, WP_TYPE_EXPORTED,
    G_IMPLEMENT_INTERFACE (WP_TYPE_ENDPOINT, wp_exported_endpoint_iface_init)
    G_ADD_PRIVATE (WpExportedEndpoint))

static void
wp_exported_endpoint_init (WpExportedEndpoint * self)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (self));

  priv->properties = wp_properties_new_empty ();

  priv->param_info[0] = SPA_PARAM_INFO (SPA_PARAM_Props, SPA_PARAM_INFO_READWRITE);
  priv->param_info[1] = SPA_PARAM_INFO (SPA_PARAM_PropInfo, SPA_PARAM_INFO_READ);

  priv->info.version = PW_VERSION_ENDPOINT_INFO;
  priv->info.props = (struct spa_dict *) wp_properties_peek_dict (priv->properties);
  priv->info.params = priv->param_info;
  priv->info.n_params = SPA_N_ELEMENTS (priv->param_info);
}

static void
wp_exported_endpoint_finalize (GObject * object)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (object));

  g_clear_pointer (&priv->properties, wp_properties_unref);
  wp_spa_props_clear (&priv->spa_props);
  g_free (priv->info.name);
  g_free (priv->info.media_class);

  G_OBJECT_CLASS (wp_exported_endpoint_parent_class)->finalize (object);
}

static void
wp_exported_endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (object));

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
client_endpoint_update (WpExportedEndpoint * self, guint32 change_mask,
    guint32 info_change_mask)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (self);
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buf, sizeof (buf));
  struct pw_client_endpoint_proxy *pw_proxy = NULL;
  struct pw_endpoint_info *info = NULL;
  g_autoptr (GPtrArray) params = NULL;

  pw_proxy = (struct pw_client_endpoint_proxy *) wp_proxy_get_pw_proxy (
      priv->client_ep);

  if (change_mask & PW_CLIENT_ENDPOINT_UPDATE_PARAMS) {
    params = wp_spa_props_build_all_pods (&priv->spa_props, &b);
  }
  if (change_mask & PW_CLIENT_ENDPOINT_UPDATE_INFO) {
    info = &priv->info;
    info->change_mask = info_change_mask;
  }

  pw_client_endpoint_proxy_update (pw_proxy,
      change_mask,
      params ? params->len : 0,
      (const struct spa_pod **) (params ? params->pdata : NULL),
      info);

  if (info)
    info->change_mask = 0;
}

static int
client_endpoint_set_param (void *object,
    uint32_t id, uint32_t flags, const struct spa_pod *param)
{
  WpExportedEndpoint *self = WP_EXPORTED_ENDPOINT (object);
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (self);
  g_autoptr (GArray) changed_ids = NULL;
  guint32 prop_id;

  if (id != SPA_PARAM_Props)
    return -ENOENT;

  changed_ids = g_array_new (FALSE, FALSE, sizeof (guint32));
  wp_spa_props_store_from_props (&priv->spa_props, param, changed_ids);

  for (guint i = 0; i < changed_ids->len; i++) {
    prop_id = g_array_index (changed_ids, guint32, i);
    g_signal_emit (self, signals[SIGNAL_CONTROL_CHANGED], 0, prop_id);
  }

  client_endpoint_update (self, PW_CLIENT_ENDPOINT_UPDATE_PARAMS, 0);

  return 0;
}

static void
client_endpoint_proxy_bound (void *object, uint32_t global_id)
{
  WpExportedEndpoint *self = WP_EXPORTED_ENDPOINT (object);
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (self);

  priv->info.id = global_id;
  wp_exported_notify_export_done (WP_EXPORTED (self), NULL);
}

static struct pw_client_endpoint_proxy_events client_endpoint_events = {
  PW_VERSION_CLIENT_ENDPOINT_PROXY_EVENTS,
  .set_param = client_endpoint_set_param,
};

static struct pw_proxy_events client_ep_proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .bound = client_endpoint_proxy_bound,
};

static void
wp_exported_endpoint_export (WpExported * self)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (self));
  g_autoptr (WpCore) core = wp_exported_get_core (self);
  struct pw_client_endpoint_proxy *pw_proxy = NULL;

  /* make sure these props are not present; they are added by the server */
  wp_properties_set (priv->properties, PW_KEY_OBJECT_ID, NULL);
  wp_properties_set (priv->properties, PW_KEY_CLIENT_ID, NULL);
  wp_properties_set (priv->properties, PW_KEY_FACTORY_ID, NULL);

  /* add must-have global properties */
  wp_properties_set (priv->properties, PW_KEY_ENDPOINT_NAME, priv->info.name);
  wp_properties_set (priv->properties, PW_KEY_MEDIA_CLASS, priv->info.media_class);

  priv->client_ep = wp_core_create_remote_object (core, "client-endpoint",
      PW_TYPE_INTERFACE_ClientEndpoint, PW_VERSION_CLIENT_ENDPOINT_PROXY,
      priv->properties);

  pw_proxy = (struct pw_client_endpoint_proxy *) wp_proxy_get_pw_proxy (
      priv->client_ep);

  pw_client_endpoint_proxy_add_listener (pw_proxy, &priv->listener,
      &client_endpoint_events, self);
  pw_proxy_add_listener ((struct pw_proxy *) pw_proxy, &priv->proxy_listener,
      &client_ep_proxy_events, self);

  client_endpoint_update (WP_EXPORTED_ENDPOINT (self),
      PW_CLIENT_ENDPOINT_UPDATE_PARAMS | PW_CLIENT_ENDPOINT_UPDATE_INFO,
      PW_ENDPOINT_CHANGE_MASK_ALL);
}

static void
wp_exported_endpoint_unexport (WpExported * self)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (self));

  g_clear_object (&priv->client_ep);
  priv->info.id = 0;
}

static WpProxy *
wp_exported_endpoint_get_proxy (WpExported * self)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (self));

  return priv->client_ep ? g_object_ref (priv->client_ep) : NULL;
}

static WpProperties *
wp_exported_endpoint_get_properties (WpEndpoint * endpoint)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (endpoint));

  return wp_properties_ref (priv->properties);
}

const gchar *
wp_exported_endpoint_get_name (WpEndpoint * endpoint)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (endpoint));
  return priv->info.name;
}

const gchar *
wp_exported_endpoint_get_media_class (WpEndpoint * endpoint)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (endpoint));
  return priv->info.media_class;
}

WpDirection
wp_exported_endpoint_get_direction (WpEndpoint * endpoint)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (endpoint));
  return priv->info.direction;
}

static const struct spa_pod *
wp_exported_endpoint_get_control (WpEndpoint * endpoint, guint32 control_id)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (endpoint));

  return wp_spa_props_get_stored (&priv->spa_props, control_id);
}

static gboolean
wp_exported_endpoint_set_control (WpEndpoint * endpoint, guint32 control_id,
    const struct spa_pod * pod)
{
  WpExportedEndpointPrivate *priv =
      wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (endpoint));

  if (wp_spa_props_store_pod (&priv->spa_props, control_id, pod) < 0)
    return FALSE;

  g_signal_emit (endpoint, signals[SIGNAL_CONTROL_CHANGED], 0, control_id);

  /* update only after the endpoint has been exported */
  if (priv->info.id != 0) {
    client_endpoint_update (WP_EXPORTED_ENDPOINT (endpoint),
      PW_CLIENT_ENDPOINT_UPDATE_PARAMS, 0);
  }

  return TRUE;
}

static void
wp_exported_endpoint_class_init (WpExportedEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpExportedClass *exported_class = (WpExportedClass *) klass;

  object_class->finalize = wp_exported_endpoint_finalize;
  object_class->get_property = wp_exported_endpoint_get_property;

  exported_class->export = wp_exported_endpoint_export;
  exported_class->unexport = wp_exported_endpoint_unexport;
  exported_class->get_proxy = wp_exported_endpoint_get_proxy;

  g_object_class_install_property (object_class, EXPORTED_PROP_GLOBAL_ID,
      g_param_spec_uint ("global-id", "global-id",
          "The pipewire global id of the exported endpoint", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_override_property (object_class, EXPORTED_PROP_PROPERTIES,
      "properties");
}

static void
wp_exported_endpoint_iface_init (WpEndpointInterface * iface)
{
  iface->get_properties = wp_exported_endpoint_get_properties;
  iface->get_name = wp_exported_endpoint_get_name;
  iface->get_media_class = wp_exported_endpoint_get_media_class;
  iface->get_direction = wp_exported_endpoint_get_direction;
  iface->get_control = wp_exported_endpoint_get_control;
  iface->set_control = wp_exported_endpoint_set_control;
}

WpExportedEndpoint *
wp_exported_endpoint_new (WpCore * core)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_EXPORTED_ENDPOINT,
      "core", core,
      NULL);
}

/**
 * wp_exported_endpoint_get_global_id: (method)
 * @self: the endpoint
 *
 * Returns: the pipewire global id of the exported endpoint object. This
 *   is only valid after the wp_exported_export() async operation has finished.
 */
guint32
wp_exported_endpoint_get_global_id (WpExportedEndpoint * self)
{
  WpExportedEndpointPrivate *priv;

  g_return_val_if_fail (WP_IS_EXPORTED_ENDPOINT (self), 0);
  priv = wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (self));

  return priv->info.id;
}

void
wp_exported_endpoint_set_property (WpExportedEndpoint * self,
    const gchar * key, const gchar * value)
{
  WpExportedEndpointPrivate *priv;

  g_return_if_fail (WP_IS_EXPORTED_ENDPOINT (self));
  priv = wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (self));

  wp_properties_set (priv->properties, key, value);

  g_object_notify (G_OBJECT (self), "properties");

  /* update only after the endpoint has been exported */
  if (priv->info.id != 0) {
    client_endpoint_update (WP_EXPORTED_ENDPOINT (self),
      PW_CLIENT_ENDPOINT_UPDATE_INFO, PW_ENDPOINT_CHANGE_MASK_PROPS);
  }
}

void
wp_exported_endpoint_update_properties (WpExportedEndpoint * self,
    WpProperties * updates)
{
  WpExportedEndpointPrivate *priv;

  g_return_if_fail (WP_IS_EXPORTED_ENDPOINT (self));
  priv = wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (self));

  wp_properties_update_from_dict (priv->properties,
      wp_properties_peek_dict (updates));

  g_object_notify (G_OBJECT (self), "properties");

  /* update only after the endpoint has been exported */
  if (priv->info.id != 0) {
    client_endpoint_update (WP_EXPORTED_ENDPOINT (self),
      PW_CLIENT_ENDPOINT_UPDATE_INFO, PW_ENDPOINT_CHANGE_MASK_PROPS);
  }
}

void
wp_exported_endpoint_set_name (WpExportedEndpoint * self, const gchar * name)
{
  WpExportedEndpointPrivate *priv;

  g_return_if_fail (WP_IS_EXPORTED_ENDPOINT (self));
  priv = wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (self));

  g_free (priv->info.name);
  priv->info.name = g_strdup (name);
}

void
wp_exported_endpoint_set_media_class (WpExportedEndpoint * self,
    const gchar * media_class)
{
  WpExportedEndpointPrivate *priv;

  g_return_if_fail (WP_IS_EXPORTED_ENDPOINT (self));
  priv = wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (self));

  g_free (priv->info.media_class);
  priv->info.media_class = g_strdup (media_class);
}

void
wp_exported_endpoint_set_direction (WpExportedEndpoint * self, WpDirection dir)
{
  WpExportedEndpointPrivate *priv;

  g_return_if_fail (WP_IS_EXPORTED_ENDPOINT (self));
  priv = wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (self));

  priv->info.direction = (enum pw_direction) dir;
}

void
wp_exported_endpoint_register_control (WpExportedEndpoint * self,
    WpEndpointControl control)
{
  WpExportedEndpointPrivate *priv;

  g_return_if_fail (WP_IS_EXPORTED_ENDPOINT (self));
  priv = wp_exported_endpoint_get_instance_private (WP_EXPORTED_ENDPOINT (self));

  switch (control) {
  case WP_ENDPOINT_CONTROL_VOLUME:
    wp_spa_props_register (&priv->spa_props, control,
      "Volume", SPA_POD_CHOICE_RANGE_Float (1.0, 0.0, 10.0));
    break;
  case WP_ENDPOINT_CONTROL_MUTE:
    wp_spa_props_register (&priv->spa_props, control,
      "Mute", SPA_POD_CHOICE_Bool (false));
    break;
  case WP_ENDPOINT_CONTROL_CHANNEL_VOLUMES:
    wp_spa_props_register (&priv->spa_props, control,
      "Channel Volumes", SPA_POD_CHOICE_RANGE_Float (1.0, 0.0, 10.0));
    break;
  default:
    g_warning ("Unknown endpoint control: 0x%x", control);
    break;
  }
}
