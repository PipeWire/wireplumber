/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpEndpoint
 *
 * The #WpEndpoint class allows accessing the properties and methods of a
 * PipeWire endpoint object (`struct pw_endpoint` from the session-manager
 * extension).
 *
 * A #WpEndpoint is constructed internally when a new endpoint appears on the
 * PipeWire registry and it is made available through the #WpObjectManager API.
 *
 * A #WpImplEndpoint allows implementing an endpoint and exporting it to
 * PipeWire, which is done by augmenting the #WpImplEndpoint with
 * %WP_PROXY_FEATURE_BOUND.
 */

#include "endpoint.h"
#include "private.h"
#include "error.h"
#include "wpenums.h"

#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>

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

/* WpEndpoint */

typedef struct _WpEndpointPrivate WpEndpointPrivate;
struct _WpEndpointPrivate
{
  WpProperties *properties;
  WpSpaProps spa_props;
  struct pw_endpoint_info *info;
  struct spa_hook listener;
};

G_DEFINE_TYPE_WITH_PRIVATE (WpEndpoint, wp_endpoint, WP_TYPE_PROXY)

static void
wp_endpoint_init (WpEndpoint * self)
{
}

static void
wp_endpoint_finalize (GObject * object)
{
  WpEndpoint *self = WP_ENDPOINT (object);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  g_clear_pointer (&priv->info, endpoint_info_free);
  g_clear_pointer (&priv->properties, wp_properties_unref);
  wp_spa_props_clear (&priv->spa_props);

  G_OBJECT_CLASS (wp_endpoint_parent_class)->finalize (object);
}

static void
wp_endpoint_augment (WpProxy * proxy, WpProxyFeatures features)
{
  /* call the parent impl first to ensure we have a pw proxy if necessary */
  WP_PROXY_CLASS (wp_endpoint_parent_class)->augment (proxy, features);

  if (features & WP_ENDPOINT_FEATURE_CONTROLS) {
    struct pw_endpoint *pw_proxy = NULL;
    uint32_t ids[] = { SPA_PARAM_Props };

    pw_proxy = (struct pw_endpoint *) wp_proxy_get_pw_proxy (proxy);
    if (!pw_proxy)
      return;

    pw_endpoint_enum_params (pw_proxy, 0, SPA_PARAM_PropInfo, 0, -1, NULL);
    pw_endpoint_subscribe_params (pw_proxy, ids, SPA_N_ELEMENTS (ids));
  }
}

static gconstpointer
wp_endpoint_get_info (WpProxy * proxy)
{
  WpEndpoint *self = WP_ENDPOINT (proxy);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  return priv->info;
}

static WpProperties *
wp_endpoint_get_properties (WpProxy * proxy)
{
  WpEndpoint *self = WP_ENDPOINT (proxy);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  return wp_properties_ref (priv->properties);
}

static gint
wp_endpoint_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const struct spa_pod *filter)
{
  struct pw_endpoint *pwp;
  int endpoint_enum_params_result;

  pwp = (struct pw_endpoint *) wp_proxy_get_pw_proxy (self);
  endpoint_enum_params_result = pw_endpoint_enum_params (pwp, 0, id, start, num,
      filter);
  g_warn_if_fail (endpoint_enum_params_result >= 0);

  return endpoint_enum_params_result;
}

static gint
wp_endpoint_subscribe_params (WpProxy * self, guint32 n_ids, guint32 *ids)
{
  struct pw_endpoint *pwp;
  int endpoint_subscribe_params_result;

  pwp = (struct pw_endpoint *) wp_proxy_get_pw_proxy (self);
  endpoint_subscribe_params_result = pw_endpoint_subscribe_params (pwp, ids,
      n_ids);
  g_warn_if_fail (endpoint_subscribe_params_result >= 0);

  return endpoint_subscribe_params_result;
}

static gint
wp_endpoint_set_param (WpProxy * self, guint32 id, guint32 flags,
    const struct spa_pod *param)
{
  struct pw_endpoint *pwp;
  int endpoint_set_param_result;

  pwp = (struct pw_endpoint *) wp_proxy_get_pw_proxy (self);
  endpoint_set_param_result = pw_endpoint_set_param (pwp, id, flags, param);
  g_warn_if_fail (endpoint_set_param_result >= 0);

  return endpoint_set_param_result;
}

static void
endpoint_event_info (void *data, const struct pw_endpoint_info *info)
{
  WpEndpoint *self = WP_ENDPOINT (data);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  priv->info = endpoint_info_update (priv->info, &priv->properties, info);
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);

  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_ENDPOINT_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");
}

static const struct pw_endpoint_events endpoint_events = {
  PW_VERSION_ENDPOINT_EVENTS,
  .info = endpoint_event_info,
  .param = wp_proxy_handle_event_param,
};

static void
wp_endpoint_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpEndpoint *self = WP_ENDPOINT (proxy);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);

  pw_endpoint_add_listener ((struct pw_endpoint *) pw_proxy,
      &priv->listener, &endpoint_events, self);
}

static void
wp_endpoint_param (WpProxy * proxy, gint seq, guint32 id, guint32 index,
    guint32 next, const struct spa_pod *param)
{
  WpEndpoint *self = WP_ENDPOINT (proxy);
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  g_autoptr (GArray) changed_ids = NULL;
  guint32 prop_id;

  switch (id) {
  case SPA_PARAM_PropInfo:
    wp_spa_props_register_from_prop_info (&priv->spa_props, param);
    break;
  case SPA_PARAM_Props:
    changed_ids = g_array_new (FALSE, FALSE, sizeof (uint32_t));
    wp_spa_props_store_from_props (&priv->spa_props, param, changed_ids);

    for (guint i = 0; i < changed_ids->len; i++) {
      prop_id = g_array_index (changed_ids, uint32_t, i);
      g_signal_emit (self, signals[SIGNAL_CONTROL_CHANGED], 0, prop_id);
    }

    wp_proxy_set_feature_ready (WP_PROXY (self),
        WP_ENDPOINT_FEATURE_CONTROLS);
    break;
  }
}

static const gchar *
get_name (WpEndpoint * self)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return priv->info->name;
}

static const gchar *
get_media_class (WpEndpoint * self)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return priv->info->media_class;
}

static WpDirection
get_direction (WpEndpoint * self)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return priv->info->direction;
}

static const struct spa_pod *
get_control (WpEndpoint * self, guint32 control_id)
{
  WpEndpointPrivate *priv = wp_endpoint_get_instance_private (self);
  return wp_spa_props_get_stored (&priv->spa_props, control_id);
}

static gboolean
set_control (WpEndpoint * self, guint32 control_id,
    const struct spa_pod * pod)
{
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buf, sizeof (buf));
  struct pw_endpoint *pw_proxy = NULL;

  /* set the default endpoint id as a property param on the endpoint;
     our spa_props will be updated by the param event */

  pw_proxy = (struct pw_endpoint *) wp_proxy_get_pw_proxy (WP_PROXY (self));
  if (!pw_proxy)
    return FALSE;

  pw_endpoint_set_param (pw_proxy,
      SPA_PARAM_Props, 0,
      spa_pod_builder_add_object (&b,
          SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
          control_id, SPA_POD_Pod (pod)));

  return TRUE;
}

static void
wp_endpoint_class_init (WpEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_endpoint_finalize;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Endpoint;
  proxy_class->pw_iface_version = PW_VERSION_ENDPOINT;

  proxy_class->augment = wp_endpoint_augment;
  proxy_class->get_info = wp_endpoint_get_info;
  proxy_class->get_properties = wp_endpoint_get_properties;
  proxy_class->enum_params = wp_endpoint_enum_params;
  proxy_class->subscribe_params = wp_endpoint_subscribe_params;
  proxy_class->set_param = wp_endpoint_set_param;

  proxy_class->pw_proxy_created = wp_endpoint_pw_proxy_created;
  proxy_class->param = wp_endpoint_param;

  klass->get_name = get_name;
  klass->get_media_class = get_media_class;
  klass->get_direction = get_direction;
  klass->get_control = get_control;
  klass->set_control = set_control;

  /**
   * WpEndpoint::control-changed:
   * @self: the endpoint
   * @control: the control that changed (a #WpEndpointControl)
   *
   * Emitted when an endpoint control changes value
   */
  signals[SIGNAL_CONTROL_CHANGED] = g_signal_new (
      "control-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * wp_endpoint_get_name:
 * @self: the endpoint
 *
 * Returns: the name of the endpoint
 */
const gchar *
wp_endpoint_get_name (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->get_name, NULL);

  return WP_ENDPOINT_GET_CLASS (self)->get_name (self);
}

/**
 * wp_endpoint_get_media_class:
 * @self: the endpoint
 *
 * Returns: the media class of the endpoint (ex. "Audio/Sink")
 */
const gchar *
wp_endpoint_get_media_class (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->get_media_class, NULL);

  return WP_ENDPOINT_GET_CLASS (self)->get_media_class (self);
}

/**
 * wp_endpoint_get_direction:
 * @self: the endpoint
 *
 * Returns: the direction of this endpoint
 */
WpDirection
wp_endpoint_get_direction (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), 0);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->get_direction, 0);

  return WP_ENDPOINT_GET_CLASS (self)->get_direction (self);
}

/**
 * wp_endpoint_get_control:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 *
 * Returns: (transfer none) (nullable): the `spa_pod` containing the value
 *   of this control, or %NULL if @control_id does not exist on this endpoint
 */
const struct spa_pod *
wp_endpoint_get_control (WpEndpoint * self, guint32 control_id)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->get_control, NULL);

  return WP_ENDPOINT_GET_CLASS (self)->get_control (self, control_id);
}

/**
 * wp_endpoint_get_control_boolean:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: (out): the boolean value of this control
 *
 * Returns: %TRUE on success, %FALSE if the control does not exist on this
 *   endpoint or if it is not a boolean
 */
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

/**
 * wp_endpoint_get_control_int:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: (out): the integer value of this control
 *
 * Returns: %TRUE on success, %FALSE if the control does not exist on this
 *   endpoint or if it is not an integer
 */
gboolean
wp_endpoint_get_control_int (WpEndpoint * self, guint32 control_id,
    gint * value)
{
  const struct spa_pod *pod = wp_endpoint_get_control (self, control_id);
  return (pod && spa_pod_get_int (pod, value) == 0);
}

/**
 * wp_endpoint_get_control_float:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: (out): the floating-point number value of this control
 *
 * Returns: %TRUE on success, %FALSE if the control does not exist on this
 *   endpoint or if it is not a floating-point number
 */
gboolean
wp_endpoint_get_control_float (WpEndpoint * self, guint32 control_id,
    gfloat * value)
{
  const struct spa_pod *pod = wp_endpoint_get_control (self, control_id);
  return (pod && spa_pod_get_float (pod, value) == 0);
}

/**
 * wp_endpoint_set_control:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: the new value for this control, as a `spa_pod`
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_endpoint_set_control (WpEndpoint * self, guint32 control_id,
    const struct spa_pod * value)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), FALSE);
  g_return_val_if_fail (WP_ENDPOINT_GET_CLASS (self)->set_control, FALSE);

  return WP_ENDPOINT_GET_CLASS (self)->set_control (self, control_id, value);
}

/**
 * wp_endpoint_set_control_boolean:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: the new value for this control, as a boolean
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_endpoint_set_control_boolean (WpEndpoint * self, guint32 control_id,
    gboolean value)
{
  gchar buffer[512];
  return wp_endpoint_set_control (self, control_id, wp_spa_props_build_pod (
          buffer, sizeof (buffer), SPA_POD_Bool (value), 0));
}

/**
 * wp_endpoint_set_control_int:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: the new value for this control, as an integer
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_endpoint_set_control_int (WpEndpoint * self, guint32 control_id,
    gint value)
{
  gchar buffer[512];
  return wp_endpoint_set_control (self, control_id, wp_spa_props_build_pod (
          buffer, sizeof (buffer), SPA_POD_Int (value), 0));
}

/**
 * wp_endpoint_set_control_float:
 * @self: the endpoint
 * @control_id: the control id (a #WpEndpointControl)
 * @value: the new value for this control, as a floating-point number
 *
 * Returns: %TRUE on success, %FALSE if an error occurred
 */
gboolean
wp_endpoint_set_control_float (WpEndpoint * self, guint32 control_id,
    gfloat value)
{
  gchar buffer[512];
  return wp_endpoint_set_control (self, control_id, wp_spa_props_build_pod (
          buffer, sizeof (buffer), SPA_POD_Float (value), 0));
}

/* WpImplEndpoint */

typedef struct _WpImplEndpointPrivate WpImplEndpointPrivate;
struct _WpImplEndpointPrivate
{
  WpEndpointPrivate *pp;
  struct pw_endpoint_info info;
  struct spa_param_info param_info[2];
};

G_DEFINE_TYPE_WITH_PRIVATE (WpImplEndpoint, wp_impl_endpoint, WP_TYPE_ENDPOINT)

static void
wp_impl_endpoint_init (WpImplEndpoint * self)
{
  WpImplEndpointPrivate *priv = wp_impl_endpoint_get_instance_private (self);

  /* store a pointer to the parent's private; we use that structure
    as well to optimize memory usage and to be able to re-use some of the
    parent's methods without reimplementing them */
  priv->pp = wp_endpoint_get_instance_private (WP_ENDPOINT (self));

  priv->pp->properties = wp_properties_new_empty ();

  priv->param_info[0] = SPA_PARAM_INFO (SPA_PARAM_Props, SPA_PARAM_INFO_READWRITE);
  priv->param_info[1] = SPA_PARAM_INFO (SPA_PARAM_PropInfo, SPA_PARAM_INFO_READ);

  priv->info.version = PW_VERSION_ENDPOINT_INFO;
  priv->info.props =
      (struct spa_dict *) wp_properties_peek_dict (priv->pp->properties);
  priv->info.params = priv->param_info;
  priv->info.n_params = SPA_N_ELEMENTS (priv->param_info);
  priv->pp->info = &priv->info;
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_ENDPOINT_FEATURE_CONTROLS);
}

static void
wp_impl_endpoint_finalize (GObject * object)
{
  WpImplEndpointPrivate *priv =
      wp_impl_endpoint_get_instance_private (WP_IMPL_ENDPOINT (object));

  /* set to NULL to prevent parent's finalize from calling free() on it */
  priv->pp->info = NULL;
  g_free (priv->info.name);
  g_free (priv->info.media_class);

  G_OBJECT_CLASS (wp_impl_endpoint_parent_class)->finalize (object);
}

static void
client_endpoint_update (WpImplEndpoint * self, guint32 change_mask,
    guint32 info_change_mask)
{
  WpImplEndpointPrivate *priv = wp_impl_endpoint_get_instance_private (self);
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buf, sizeof (buf));
  struct pw_client_endpoint *pw_proxy = NULL;
  struct pw_endpoint_info *info = NULL;
  g_autoptr (GPtrArray) params = NULL;

  pw_proxy = (struct pw_client_endpoint *) wp_proxy_get_pw_proxy (WP_PROXY (self));

  if (change_mask & PW_CLIENT_ENDPOINT_UPDATE_PARAMS) {
    params = wp_spa_props_build_all_pods (&priv->pp->spa_props, &b);
  }
  if (change_mask & PW_CLIENT_ENDPOINT_UPDATE_INFO) {
    info = &priv->info;
    info->change_mask = info_change_mask;
  }

  pw_client_endpoint_update (pw_proxy,
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
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);
  WpImplEndpointPrivate *priv = wp_impl_endpoint_get_instance_private (self);
  g_autoptr (GArray) changed_ids = NULL;
  guint32 prop_id;

  if (id != SPA_PARAM_Props)
    return -ENOENT;

  changed_ids = g_array_new (FALSE, FALSE, sizeof (guint32));
  wp_spa_props_store_from_props (&priv->pp->spa_props, param, changed_ids);

  for (guint i = 0; i < changed_ids->len; i++) {
    prop_id = g_array_index (changed_ids, guint32, i);
    g_signal_emit (self, signals[SIGNAL_CONTROL_CHANGED], 0, prop_id);
  }

  client_endpoint_update (self, PW_CLIENT_ENDPOINT_UPDATE_PARAMS, 0);

  return 0;
}

static struct pw_client_endpoint_events client_endpoint_events = {
  PW_VERSION_CLIENT_ENDPOINT_EVENTS,
  .set_param = client_endpoint_set_param,
};

static void
wp_impl_endpoint_augment (WpProxy * proxy, WpProxyFeatures features)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (proxy);
  WpImplEndpointPrivate *priv = wp_impl_endpoint_get_instance_private (self);

  /* if any of the default features is requested, make sure BOUND
     is also requested, as they all depend on binding the endpoint */
  if (features & WP_PROXY_FEATURES_STANDARD)
    features |= WP_PROXY_FEATURE_BOUND;

  if (features & WP_PROXY_FEATURE_BOUND) {
    g_autoptr (WpCore) core = wp_proxy_get_core (proxy);
    struct pw_core *pw_core = wp_core_get_pw_core (core);
    struct pw_proxy *pw_proxy = NULL;

    /* no pw_core -> we are not connected */
    if (!pw_core) {
      wp_proxy_augment_error (proxy, g_error_new (WP_DOMAIN_LIBRARY,
            WP_LIBRARY_ERROR_OPERATION_FAILED,
            "The WirePlumber core is not connected; "
            "object cannot be exported to PipeWire"));
      return;
    }

    /* make sure these props are not present; they are added by the server */
    wp_properties_set (priv->pp->properties, PW_KEY_OBJECT_ID, NULL);
    wp_properties_set (priv->pp->properties, PW_KEY_CLIENT_ID, NULL);
    wp_properties_set (priv->pp->properties, PW_KEY_FACTORY_ID, NULL);

    /* add must-have global properties */
    wp_properties_set (priv->pp->properties,
        PW_KEY_ENDPOINT_NAME, priv->info.name);
    wp_properties_set (priv->pp->properties,
        PW_KEY_MEDIA_CLASS, priv->info.media_class);

    pw_proxy = pw_core_create_object (pw_core, "client-endpoint",
        PW_TYPE_INTERFACE_ClientEndpoint, PW_VERSION_CLIENT_ENDPOINT,
        wp_properties_peek_dict (priv->pp->properties), 0);
    wp_proxy_set_pw_proxy (proxy, pw_proxy);

    pw_client_endpoint_add_listener (pw_proxy, &priv->pp->listener,
        &client_endpoint_events, self);

    client_endpoint_update (WP_IMPL_ENDPOINT (self),
        PW_CLIENT_ENDPOINT_UPDATE_PARAMS | PW_CLIENT_ENDPOINT_UPDATE_INFO,
        PW_ENDPOINT_CHANGE_MASK_ALL);
  }
}

static gint
wp_impl_endpoint_set_param (WpProxy * self, guint32 id, guint32 flags,
    const struct spa_pod *param)
{
  return client_endpoint_set_param (self, id, flags, param);
}

static gboolean
wp_impl_endpoint_set_control (WpEndpoint * endpoint, guint32 control_id,
    const struct spa_pod * pod)
{
  WpImplEndpointPrivate *priv =
      wp_impl_endpoint_get_instance_private (WP_IMPL_ENDPOINT (endpoint));

  if (wp_spa_props_store_pod (&priv->pp->spa_props, control_id, pod) < 0)
    return FALSE;

  g_signal_emit (endpoint, signals[SIGNAL_CONTROL_CHANGED], 0, control_id);

  /* update only after the endpoint has been exported */
  if (wp_proxy_get_features (WP_PROXY (endpoint)) & WP_PROXY_FEATURE_BOUND) {
    client_endpoint_update (WP_IMPL_ENDPOINT (endpoint),
        PW_CLIENT_ENDPOINT_UPDATE_PARAMS, 0);
  }

  return TRUE;
}

static void
wp_impl_endpoint_class_init (WpImplEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;
  WpEndpointClass *endpoint_class = (WpEndpointClass *) klass;

  object_class->finalize = wp_impl_endpoint_finalize;

  proxy_class->augment = wp_impl_endpoint_augment;
  proxy_class->enum_params = NULL;
  proxy_class->subscribe_params = NULL;
  proxy_class->set_param = wp_impl_endpoint_set_param;

  proxy_class->pw_proxy_created = NULL;
  proxy_class->param = NULL;

  endpoint_class->set_control = wp_impl_endpoint_set_control;
}

/**
 * wp_impl_endpoint_new:
 * @core: the #WpCore
 *
 * Returns: (transfer full): the newly constructed endpoint implementation
 */
WpImplEndpoint *
wp_impl_endpoint_new (WpCore * core)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_IMPL_ENDPOINT,
      "core", core,
      NULL);
}

/**
 * wp_impl_endpoint_set_property:
 * @self: the endpoint implementation
 * @key: a property key
 * @value: a property value
 *
 * Sets the specified property on the PipeWire properties of the endpoint.
 *
 * If this property is set before exporting the endpoint, then it is also used
 * in the construction process of the endpoint object and appears as a global
 * property.
 */
void
wp_impl_endpoint_set_property (WpImplEndpoint * self,
    const gchar * key, const gchar * value)
{
  WpImplEndpointPrivate *priv;

  g_return_if_fail (WP_IS_IMPL_ENDPOINT (self));
  priv = wp_impl_endpoint_get_instance_private (self);

  wp_properties_set (priv->pp->properties, key, value);

  g_object_notify (G_OBJECT (self), "properties");

  /* update only after the endpoint has been exported */
  if (wp_proxy_get_features (WP_PROXY (self)) & WP_PROXY_FEATURE_BOUND) {
    client_endpoint_update (self, PW_CLIENT_ENDPOINT_UPDATE_INFO,
        PW_ENDPOINT_CHANGE_MASK_PROPS);
  }
}

/**
 * wp_impl_endpoint_update_properties:
 * @self: the endpoint implementation
 * @updates: a set of properties to add or update in the endpoint's properties
 *
 * Adds or updates the values of the PipeWire properties of the endpoint
 * using the properties in @updates as a source.
 *
 * If the properties are set before exporting the endpoint, then they are also
 * used in the construction process of the endpoint object and appear as
 * global properties.
 */
void
wp_impl_endpoint_update_properties (WpImplEndpoint * self,
    WpProperties * updates)
{
  WpImplEndpointPrivate *priv;

  g_return_if_fail (WP_IS_IMPL_ENDPOINT (self));
  priv = wp_impl_endpoint_get_instance_private (self);

  wp_properties_update_from_dict (priv->pp->properties,
      wp_properties_peek_dict (updates));

  g_object_notify (G_OBJECT (self), "properties");

  /* update only after the endpoint has been exported */
  if (wp_proxy_get_features (WP_PROXY (self)) & WP_PROXY_FEATURE_BOUND) {
    client_endpoint_update (self, PW_CLIENT_ENDPOINT_UPDATE_INFO,
        PW_ENDPOINT_CHANGE_MASK_PROPS);
  }
}

/**
 * wp_impl_endpoint_set_name:
 * @self: the endpoint implementation
 * @name: the name to set
 *
 * Sets the name of the endpoint to be @name.
 *
 * This only makes sense to set before exporting the endpoint.
 */
void
wp_impl_endpoint_set_name (WpImplEndpoint * self, const gchar * name)
{
  WpImplEndpointPrivate *priv;

  g_return_if_fail (WP_IS_IMPL_ENDPOINT (self));
  priv = wp_impl_endpoint_get_instance_private (self);

  g_free (priv->info.name);
  priv->info.name = g_strdup (name);
}

/**
 * wp_impl_endpoint_set_media_class:
 * @self: the endpoint implementation
 * @media_class: the media class to set
 *
 * Sets the media class of the endpoint to be @media_class.
 *
 * This only makes sense to set before exporting the endpoint.
 */
void
wp_impl_endpoint_set_media_class (WpImplEndpoint * self,
    const gchar * media_class)
{
  WpImplEndpointPrivate *priv;

  g_return_if_fail (WP_IS_IMPL_ENDPOINT (self));
  priv = wp_impl_endpoint_get_instance_private (self);

  g_free (priv->info.media_class);
  priv->info.media_class = g_strdup (media_class);
}

/**
 * wp_impl_endpoint_set_direction:
 * @self: the endpoint implementation
 * @dir: the direction to set
 *
 * Sets the direction of the endpoint to be @dir.
 *
 * This only makes sense to set before exporting the endpoint.
 */
void
wp_impl_endpoint_set_direction (WpImplEndpoint * self, WpDirection dir)
{
  WpImplEndpointPrivate *priv;

  g_return_if_fail (WP_IS_IMPL_ENDPOINT (self));
  priv = wp_impl_endpoint_get_instance_private (self);

  priv->info.direction = (enum pw_direction) dir;
}

/**
 * wp_impl_endpoint_register_control:
 * @self: the endpoint implementation
 * @control: the control to make available
 *
 * Registers the specified @control as a SPA property of this endpoint,
 * making it appear to remote clients.
 *
 * This is required to do before setting or getting a value for this control.
 * This is also required to be done before exporting the endpoint.
 */
void
wp_impl_endpoint_register_control (WpImplEndpoint * self,
    WpEndpointControl control)
{
  WpImplEndpointPrivate *priv;

  g_return_if_fail (WP_IS_IMPL_ENDPOINT (self));
  priv = wp_impl_endpoint_get_instance_private (self);

  switch (control) {
  case WP_ENDPOINT_CONTROL_VOLUME:
    wp_spa_props_register (&priv->pp->spa_props, control,
      "Volume", SPA_POD_CHOICE_RANGE_Float (1.0, 0.0, 10.0));
    break;
  case WP_ENDPOINT_CONTROL_MUTE:
    wp_spa_props_register (&priv->pp->spa_props, control,
      "Mute", SPA_POD_CHOICE_Bool (false));
    break;
  case WP_ENDPOINT_CONTROL_CHANNEL_VOLUMES:
    wp_spa_props_register (&priv->pp->spa_props, control,
      "Channel Volumes", SPA_POD_CHOICE_RANGE_Float (1.0, 0.0, 10.0));
    break;
  default:
    g_warning ("Unknown endpoint control: 0x%x", control);
    break;
  }
}
