/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: endpoint
 * @title: PIpeWire Endpoint
 */

#include "spa/param/param.h"
#define G_LOG_DOMAIN "wp-endpoint"

#include "endpoint.h"
#include "node.h"
#include "session.h"
#include "object-manager.h"
#include "error.h"
#include "debug.h"
#include "wpenums.h"
#include "spa-type.h"
#include "si-factory.h"
#include "private/impl-endpoint.h"
#include "private/pipewire-object-mixin.h"

#include <pipewire/extensions/session-manager.h>
#include <pipewire/extensions/session-manager/introspect-funcs.h>
#include <spa/utils/result.h>

enum {
  PROP_NAME = WP_PW_OBJECT_MIXIN_PROP_CUSTOM_START,
  PROP_MEDIA_CLASS,
  PROP_DIRECTION,
};

typedef struct _WpEndpointPrivate WpEndpointPrivate;
struct _WpEndpointPrivate
{
  gint place_holder;
};

static void wp_endpoint_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

/**
 * WpEndpoint:
 *
 * The #WpEndpoint class allows accessing the properties and methods of a
 * PipeWire endpoint object (`struct pw_endpoint` from the session-manager
 * extension).
 *
 * A #WpEndpoint is constructed internally when a new endpoint appears on the
 * PipeWire registry and it is made available through the #WpObjectManager API.
 */
G_DEFINE_TYPE_WITH_CODE (WpEndpoint, wp_endpoint, WP_TYPE_GLOBAL_PROXY,
    G_ADD_PRIVATE (WpEndpoint)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT,
        wp_pw_object_mixin_object_interface_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_endpoint_pw_object_mixin_priv_interface_init))

static void
wp_endpoint_init (WpEndpoint * self)
{
}

static void
wp_endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (object);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, d->info ?
        ((struct pw_endpoint_info *) d->info)->name : NULL);
    break;
  case PROP_MEDIA_CLASS:
    g_value_set_string (value, d->info ?
        ((struct pw_endpoint_info *) d->info)->media_class : NULL);
    break;
  case PROP_DIRECTION:
    g_value_set_enum (value, d->info ?
        ((struct pw_endpoint_info *) d->info)->direction : 0);
    break;
  default:
    wp_pw_object_mixin_get_property (object, property_id, value, pspec);
    break;
  }
}

static WpObjectFeatures
wp_endpoint_get_supported_features (WpObject * object)
{
  return wp_pw_object_mixin_get_supported_features(object);
}

static void
wp_endpoint_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PW_OBJECT_MIXIN_STEP_BIND:
  case WP_TRANSITION_STEP_ERROR:
    /* base class can handle BIND and ERROR */
    WP_OBJECT_CLASS (wp_endpoint_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  case WP_PW_OBJECT_MIXIN_STEP_WAIT_INFO:
    /* just wait, info will be emitted anyway after binding */
    break;
  case WP_PW_OBJECT_MIXIN_STEP_CACHE_PARAMS:
    wp_pw_object_mixin_cache_params (object, missing);
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_endpoint_deactivate (WpObject * object, WpObjectFeatures features)
{
  wp_pw_object_mixin_deactivate (object, features);
  WP_OBJECT_CLASS (wp_endpoint_parent_class)->deactivate (object, features);
}

static const struct pw_endpoint_events endpoint_events = {
  PW_VERSION_ENDPOINT_EVENTS,
  .info = (HandleEventInfoFunc(endpoint)) wp_pw_object_mixin_handle_event_info,
  .param = wp_pw_object_mixin_handle_event_param,
};

static void
wp_endpoint_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_created (proxy, pw_proxy,
      endpoint, &endpoint_events);
}

static void
wp_endpoint_pw_proxy_destroyed (WpProxy * proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_destroyed (proxy);

  wp_object_update_features (WP_OBJECT (proxy), 0, 0);
}

static void
wp_endpoint_class_init (WpEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_endpoint_get_property;

  wpobject_class->get_supported_features = wp_endpoint_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_pw_object_mixin_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_endpoint_activate_execute_step;
  wpobject_class->deactivate = wp_endpoint_deactivate;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Endpoint;
  proxy_class->pw_iface_version = PW_VERSION_ENDPOINT;
  proxy_class->pw_proxy_created = wp_endpoint_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_endpoint_pw_proxy_destroyed;

  wp_pw_object_mixin_class_override_properties (object_class);

  /**
   * WpEndpoint:name:
   *
   * The name of the endpoint
   */
  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "name", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * WpEndpoint:media-class:
   *
   * The media class of the endpoint (ex. "Audio/Sink")
   */
  g_object_class_install_property (object_class, PROP_MEDIA_CLASS,
      g_param_spec_string ("media-class", "media-class", "media-class", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * WpEndpoint:direction:
   *
   * The direction of the endpoint
   */
  g_object_class_install_property (object_class, PROP_DIRECTION,
      g_param_spec_enum ("direction", "direction", "direction",
          WP_TYPE_DIRECTION, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static gint
wp_endpoint_enum_params (gpointer instance, guint32 id,
    guint32 start, guint32 num, WpSpaPod *filter)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  return pw_endpoint_enum_params (d->iface, 0, id, start, num,
      filter ? wp_spa_pod_get_spa_pod (filter) : NULL);
}

static gint
wp_endpoint_set_param (gpointer instance, guint32 id, guint32 flags,
    WpSpaPod * param)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  return pw_endpoint_set_param (d->iface, id, flags,
      wp_spa_pod_get_spa_pod (param));
}

static void
wp_endpoint_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  wp_pw_object_mixin_priv_interface_info_init (iface, endpoint, ENDPOINT);
  iface->enum_params = wp_endpoint_enum_params;
  iface->set_param = wp_endpoint_set_param;
}

/**
 * wp_endpoint_get_name:
 * @self: the endpoint
 *
 * Requires %WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * Returns: the name of the endpoint
 */
const gchar *
wp_endpoint_get_name (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, NULL);

  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);
  return ((struct pw_endpoint_info *) d->info)->name;
}

/**
 * wp_endpoint_get_media_class:
 * @self: the endpoint
 *
 * Requires %WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * Returns: the media class of the endpoint (ex. "Audio/Sink")
 */
const gchar *
wp_endpoint_get_media_class (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, NULL);

  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);
  return ((struct pw_endpoint_info *) d->info)->media_class;
}

/**
 * wp_endpoint_get_direction:
 * @self: the endpoint
 *
 * Requires %WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * Returns: the direction of this endpoint
 */
WpDirection
wp_endpoint_get_direction (WpEndpoint * self)
{
  g_return_val_if_fail (WP_IS_ENDPOINT (self), 0);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);
  return (WpDirection) ((struct pw_endpoint_info *) d->info)->direction;
}

/**
 * wp_endpoint_create_link:
 * @self: the endpoint
 * @props: the link properties
 *
 * Creates a #WpEndpointLink between @self and another endpoint, which
 * must be specified in @props.
 * @props may contain:
 *  - `endpoint-link.output.endpoint`: the bound id of the endpoint
 *        that is in the %WP_DIRECTION_OUTPUT direction
 *  - `endpoint-link.input.endpoint`: the bound id of the endpoint
 *        that is in the %WP_DIRECTION_INPUT direction
 *
 * The id of @self is not necessary to be specified, so only one of
 * `endpoint-link.output.endpoint`, `endpoint-link.input.endpoint`
 * is actually required.
 */
void
wp_endpoint_create_link (WpEndpoint * self, WpProperties * props)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);
  int res;

  res = pw_endpoint_create_link (d->iface, wp_properties_peek_dict (props));
  if (res < 0) {
    wp_warning_object (self, "pw_endpoint_create_link: %d: %s", res,
        spa_strerror (res));
  }
}

/* WpImplEndpoint */

enum {
  IMPL_PROP_0,
  IMPL_PROP_ITEM,
};

struct _WpImplEndpoint
{
  WpEndpoint parent;

  struct spa_interface iface;
  struct pw_endpoint_info info;
  WpProperties *immutable_props;

  WpSiEndpoint *item;
};

static void wp_endpoint_impl_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpImplEndpoint, wp_impl_endpoint, WP_TYPE_ENDPOINT,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_endpoint_impl_pw_object_mixin_priv_interface_init))

static struct spa_param_info impl_param_info[] = {
  SPA_PARAM_INFO (SPA_PARAM_Props, SPA_PARAM_INFO_READWRITE),
  SPA_PARAM_INFO (SPA_PARAM_PropInfo, SPA_PARAM_INFO_READ)
};

static void
on_si_link_exported (WpSessionItem * link, GAsyncResult * res, gpointer data)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (data);
  g_autoptr (GError) error = NULL;

  if (!wp_session_item_export_finish (link, res, &error)) {
    wp_warning_object (self, "failed to export link: %s", error->message);
    g_object_unref (link);
  }
}

static int
impl_create_link (void *object, const struct spa_dict *props)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);
  const gchar *self_ep, *peer_ep;
  guint32 peer_ep_id;
  g_autoptr (WpSiEndpoint) peer_si_endpoint = NULL;
  g_autoptr (WpSession) session = NULL;
  g_autoptr (WpEndpoint) peer_ep_proxy = NULL;

  /* find the session */
  session = wp_session_item_get_associated_proxy (
      WP_SESSION_ITEM (self->item), WP_TYPE_SESSION);
  g_return_val_if_fail (session, -ENAVAIL);

  if (self->info.direction == PW_DIRECTION_OUTPUT) {
    self_ep = spa_dict_lookup (props, PW_KEY_ENDPOINT_LINK_OUTPUT_ENDPOINT);
    peer_ep = spa_dict_lookup (props, PW_KEY_ENDPOINT_LINK_INPUT_ENDPOINT);
  } else {
    self_ep = spa_dict_lookup (props, PW_KEY_ENDPOINT_LINK_INPUT_ENDPOINT);
    peer_ep = spa_dict_lookup (props, PW_KEY_ENDPOINT_LINK_OUTPUT_ENDPOINT);
  }

  wp_debug_object (self, "requested link between %s [self] & %s [peer]",
      self_ep, peer_ep);

  /* verify arguments */
  if (!peer_ep) {
    wp_warning_object (self,
        "a peer endpoint must be specified at the very least");
    return -EINVAL;
  }
  if (self_ep && ((guint32) atoi (self_ep))
          != wp_proxy_get_bound_id (WP_PROXY (self))) {
    wp_warning_object (self,
        "creating links for other endpoints is now allowed");
    return -EACCES;
  }

  /* convert to int */
  peer_ep_id = (guint32) atoi (peer_ep);

  /* find the peer endpoint */
  peer_ep_proxy = wp_session_lookup_endpoint (session,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", peer_ep_id, NULL);
  if (!peer_ep_proxy) {
    wp_warning_object (self, "endpoint %d not found in session", peer_ep_id);
    return -EINVAL;
  }

  g_object_get (peer_ep_proxy, "item", &peer_si_endpoint, NULL);

  wp_info_object (self, "creating endpoint link between "
      "%s " WP_OBJECT_FORMAT ", %s " WP_OBJECT_FORMAT,
      wp_endpoint_get_name (WP_ENDPOINT (self)),
      WP_OBJECT_ARGS (self->item),
      wp_endpoint_get_name (peer_ep_proxy),
      WP_OBJECT_ARGS (peer_si_endpoint));

  /* create the link */
  {
    g_autoptr (WpSessionItem) link = NULL;
    g_autoptr (WpCore) core = NULL;
    GVariantBuilder b;
    guint64 out_endpoint_i, in_endpoint_i;

    core = wp_object_get_core (WP_OBJECT (self));
    link = wp_session_item_make (core, "si-standard-link");
    if (!link) {
      wp_warning_object (self, "si-standard-link factory is not available");
      return -ENAVAIL;
    }

    if (self->info.direction == PW_DIRECTION_OUTPUT) {
      out_endpoint_i = (guint64) self->item;
      in_endpoint_i = (guint64) peer_si_endpoint;
    } else {
      out_endpoint_i = (guint64) peer_si_endpoint;
      in_endpoint_i = (guint64) self->item;
    }

    g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "out-endpoint",
        g_variant_new_uint64 (out_endpoint_i));
    g_variant_builder_add (&b, "{sv}", "in-endpoint",
        g_variant_new_uint64 (in_endpoint_i));
    g_variant_builder_add (&b, "{sv}", "manage-lifetime",
        g_variant_new_boolean (TRUE));
    if (G_UNLIKELY (!wp_session_item_configure (link, g_variant_builder_end (&b)))) {
      g_critical ("si-standard-link configuration failed");
      return -ENAVAIL;
    }

    wp_session_item_export (link, session,
        (GAsyncReadyCallback) on_si_link_exported, self);
    link = NULL;
  }

  return 0;
}

static const struct pw_endpoint_methods impl_endpoint = {
  PW_VERSION_ENDPOINT_METHODS,
  .add_listener =
      (ImplAddListenerFunc(endpoint)) wp_pw_object_mixin_impl_add_listener,
  .subscribe_params = wp_pw_object_mixin_impl_subscribe_params,
  .enum_params = wp_pw_object_mixin_impl_enum_params,
  .set_param = wp_pw_object_mixin_impl_set_param,
  .create_link = impl_create_link,
};

static void
wp_impl_endpoint_init (WpImplEndpoint * self)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);

  self->iface = SPA_INTERFACE_INIT (
      PW_TYPE_INTERFACE_Endpoint,
      PW_VERSION_ENDPOINT,
      &impl_endpoint, self);

  d->info = &self->info;
  d->iface = &self->iface;
}

static void
populate_properties (WpImplEndpoint * self)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);

  g_clear_pointer (&d->properties, wp_properties_unref);
  d->properties = wp_si_endpoint_get_properties (self->item);
  if (!d->properties)
    d->properties = wp_properties_new_empty ();
  d->properties = wp_properties_ensure_unique_owner (d->properties);
  wp_properties_update (d->properties, self->immutable_props);

  self->info.props = (struct spa_dict *) wp_properties_peek_dict (d->properties);
}

static void
on_si_endpoint_properties_changed (WpSiEndpoint * item, WpImplEndpoint * self)
{
  populate_properties (self);
  wp_pw_object_mixin_notify_info (self, PW_ENDPOINT_CHANGE_MASK_PROPS);
}

static void
on_node_params_changed (WpNode * node, guint32 param_id, WpImplEndpoint * self)
{
  if (param_id == SPA_PARAM_PropInfo || param_id == SPA_PARAM_Props)
    wp_pw_object_mixin_notify_params_changed (self, param_id);
}

static void
wp_impl_endpoint_constructed (GObject * object)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);
  g_autoptr (GVariant) info = NULL;
  g_autoptr (GVariantIter) immutable_props = NULL;
  g_autoptr (WpObject) node = NULL;
  const gchar *key, *value;
  guchar direction;

  self->info.version = PW_VERSION_ENDPOINT_INFO;

  info = wp_si_endpoint_get_registration_info (self->item);
  g_variant_get (info, "(ssya{ss})", &self->info.name,
      &self->info.media_class, &direction, &immutable_props);

  self->info.direction = (enum pw_direction) direction;

  /* associate with the session */
  self->info.session_id = wp_session_item_get_associated_proxy_id (
      WP_SESSION_ITEM (self->item), WP_TYPE_SESSION);

  /* construct export properties (these will come back through
    the registry and appear in wp_proxy_get_global_properties) */
  self->immutable_props = wp_properties_new (
      PW_KEY_ENDPOINT_NAME, self->info.name,
      PW_KEY_MEDIA_CLASS, self->info.media_class,
      NULL);
  wp_properties_setf (self->immutable_props, PW_KEY_SESSION_ID,
      "%d", self->info.session_id);

  /* populate immutable (global) properties */
  while (g_variant_iter_next (immutable_props, "{&s&s}", &key, &value))
    wp_properties_set (self->immutable_props, key, value);

  /* populate standard properties */
  populate_properties (self);

  /* subscribe to changes */
  g_signal_connect_object (self->item, "endpoint-properties-changed",
      G_CALLBACK (on_si_endpoint_properties_changed), self, 0);

  /* if the item has a node, proxy its ParamProps */
  node = wp_session_item_get_associated_proxy (
      WP_SESSION_ITEM (self->item), WP_TYPE_NODE);
  if (node && (wp_object_get_active_features (node) &
                  WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS)) {
    self->info.params = impl_param_info;
    self->info.n_params = G_N_ELEMENTS (impl_param_info);

    g_signal_connect_object (node, "params-changed",
        G_CALLBACK (on_node_params_changed), self, 0);

    wp_object_update_features (WP_OBJECT (self),
        WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS, 0);
  } else {
    self->info.params = NULL;
    self->info.n_params = 0;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  G_OBJECT_CLASS (wp_impl_endpoint_parent_class)->constructed (object);
}

static void
wp_impl_endpoint_dispose (GObject * object)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);

  g_clear_pointer (&self->immutable_props, wp_properties_unref);
  g_clear_pointer (&self->info.name, g_free);

  wp_object_update_features (WP_OBJECT (self), 0,
      WP_PIPEWIRE_OBJECT_FEATURE_INFO |
      WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS);

  G_OBJECT_CLASS (wp_impl_endpoint_parent_class)->dispose (object);
}

static void
wp_impl_endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);

  switch (property_id) {
  case IMPL_PROP_ITEM:
    self->item = g_value_get_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_impl_endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);

  switch (property_id) {
  case IMPL_PROP_ITEM:
    g_value_set_object (value, self->item);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

enum {
  STEP_ACTIVATE_NODE = WP_PW_OBJECT_MIXIN_STEP_CUSTOM_START + 1,
};

static guint
wp_impl_endpoint_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);

  /* before anything else, if the item has a node,
     cache its props so that enum_params works */
  if (missing & WP_PIPEWIRE_OBJECT_FEATURES_ALL) {
    g_autoptr (WpObject) node = wp_session_item_get_associated_proxy (
        WP_SESSION_ITEM (self->item), WP_TYPE_NODE);

    if (node && (wp_object_get_supported_features (node) &
                    WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS) &&
               !(wp_object_get_active_features (node) &
                    WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS))
      return STEP_ACTIVATE_NODE;
  }

  return WP_OBJECT_CLASS (wp_impl_endpoint_parent_class)->
      activate_get_next_step (object, transition, step, missing);
}

static void
wp_impl_endpoint_node_activated (WpObject * node,
    GAsyncResult * res, WpTransition * transition)
{
  WpImplEndpoint *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (node, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  self->info.params = impl_param_info;
  self->info.n_params = G_N_ELEMENTS (impl_param_info);

  g_signal_connect_object (node, "params-changed",
      G_CALLBACK (on_node_params_changed), self, 0);

  wp_object_update_features (WP_OBJECT (self),
      WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS, 0);
  wp_pw_object_mixin_notify_info (self, PW_ENDPOINT_CHANGE_MASK_PARAMS);
}

static void
wp_impl_endpoint_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (object);

  switch (step) {
  case STEP_ACTIVATE_NODE: {
    g_autoptr (WpObject) node = wp_session_item_get_associated_proxy (
        WP_SESSION_ITEM (self->item), WP_TYPE_NODE);

    wp_object_activate (node,
        WP_PROXY_FEATURE_BOUND | WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS,
        NULL, (GAsyncReadyCallback) wp_impl_endpoint_node_activated,
        transition);
    break;
  }
  case WP_PW_OBJECT_MIXIN_STEP_BIND: {
    g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
    struct pw_core *pw_core = wp_core_get_pw_core (core);

    /* no pw_core -> we are not connected */
    if (!pw_core) {
      wp_transition_return_error (WP_TRANSITION (transition), g_error_new (
              WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
              "The WirePlumber core is not connected; "
              "object cannot be exported to PipeWire"));
      return;
    }

    /* bind */
    wp_proxy_set_pw_proxy (WP_PROXY (self), pw_core_export (pw_core,
            PW_TYPE_INTERFACE_Endpoint,
            wp_properties_peek_dict (self->immutable_props),
            &self->iface, 0));
    break;
  }
  default:
    WP_OBJECT_CLASS (wp_impl_endpoint_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static void
wp_impl_endpoint_pw_proxy_destroyed (WpProxy * proxy)
{
  wp_object_update_features (WP_OBJECT (proxy), 0, 0);
}

static void
wp_impl_endpoint_class_init (WpImplEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->constructed = wp_impl_endpoint_constructed;
  object_class->dispose = wp_impl_endpoint_dispose;
  object_class->set_property = wp_impl_endpoint_set_property;
  object_class->get_property = wp_impl_endpoint_get_property;

  wpobject_class->activate_get_next_step =
      wp_impl_endpoint_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_impl_endpoint_activate_execute_step;

  proxy_class->pw_proxy_created = NULL;
  proxy_class->pw_proxy_destroyed = wp_impl_endpoint_pw_proxy_destroyed;

  g_object_class_install_property (object_class, IMPL_PROP_ITEM,
      g_param_spec_object ("item", "item", "item", WP_TYPE_SI_ENDPOINT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static GPtrArray *
wp_impl_endpoint_enum_params_sync (gpointer instance, guint32 id,
    guint32 start, guint32 num, WpSpaPod *filter)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (instance);
  g_autoptr (WpPipewireObject) node = wp_session_item_get_associated_proxy (
        WP_SESSION_ITEM (self->item), WP_TYPE_NODE);

  if (!node) {
    wp_warning_object (self, "associated node is no longer available");
    return NULL;
  }

  /* bypass a few things, knowing that the node
     caches params in the mixin param store */
  WpPwObjectMixinData *data = wp_pw_object_mixin_get_data (node);
  GPtrArray *params = wp_pw_object_mixin_get_stored_params (data, id);
  /* TODO filter */

  return params;
}

static gint
wp_impl_endpoint_set_param (gpointer instance, guint32 id, guint32 flags,
    WpSpaPod * param)
{
  WpImplEndpoint *self = WP_IMPL_ENDPOINT (instance);
  g_autoptr (WpPipewireObject) node = wp_session_item_get_associated_proxy (
        WP_SESSION_ITEM (self->item), WP_TYPE_NODE);

  if (!node) {
    wp_warning_object (self, "associated node is no longer available");
    return -EPIPE;
  }

  WpSpaIdValue idval = wp_spa_id_value_from_number ("Spa:Enum:ParamId", id);
  if (!idval) {
    wp_critical_object (self, "invalid param id: %u", id);
    return -EINVAL;
  }

  return wp_pipewire_object_set_param (node, wp_spa_id_value_short_name (idval),
      flags, param) ? 0 : -EIO;
}

#define pw_endpoint_emit(hooks,method,version,...) \
    spa_hook_list_call_simple(hooks, struct pw_endpoint_events, \
        method, version, ##__VA_ARGS__)

static void
wp_impl_endpoint_emit_info (struct spa_hook_list * hooks, gconstpointer info)
{
  pw_endpoint_emit (hooks, info, 0, info);
}

static void
wp_impl_endpoint_emit_param (struct spa_hook_list * hooks, int seq,
      guint32 id, guint32 index, guint32 next, const struct spa_pod *param)
{
  pw_endpoint_emit (hooks, param, 0, seq, id, index, next, param);
}

static void
wp_endpoint_impl_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  iface->flags |= WP_PW_OBJECT_MIXIN_PRIV_NO_PARAM_CACHE;
  iface->enum_params_sync = wp_impl_endpoint_enum_params_sync;
  iface->set_param = wp_impl_endpoint_set_param;
  iface->emit_info = wp_impl_endpoint_emit_info;
  iface->emit_param = wp_impl_endpoint_emit_param;
}

WpImplEndpoint *
wp_impl_endpoint_new (WpCore * core, WpSiEndpoint * item)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_IMPL_ENDPOINT,
      "core", core,
      "item", item,
      NULL);
}
