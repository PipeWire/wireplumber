/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpEndpointLink
 *
 * The #WpEndpointLink class allows accessing the properties and methods of a
 * PipeWire endpoint link object (`struct pw_endpoint_link` from the
 * session-manager extension).
 *
 * A #WpEndpointLink is constructed internally when a new endpoint link appears
 * on the PipeWire registry and it is made available through the
 * #WpObjectManager API.
 */

#define G_LOG_DOMAIN "wp-endpoint-link"

#include "endpoint-link.h"
#include "debug.h"
#include "private.h"
#include "error.h"
#include "wpenums.h"

#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager.h>
#include <pipewire/extensions/session-manager/introspect-funcs.h>

enum {
  SIGNAL_STATE_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

/* WpEndpointLink */

typedef struct _WpEndpointLinkPrivate WpEndpointLinkPrivate;
struct _WpEndpointLinkPrivate
{
  WpProperties *properties;
  struct pw_endpoint_link_info *info;
  struct pw_endpoint_link *iface;
  struct spa_hook listener;
};

G_DEFINE_TYPE_WITH_PRIVATE (WpEndpointLink, wp_endpoint_link, WP_TYPE_PROXY)

static void
wp_endpoint_link_init (WpEndpointLink * self)
{
}

static void
wp_endpoint_link_finalize (GObject * object)
{
  WpEndpointLink *self = WP_ENDPOINT_LINK (object);
  WpEndpointLinkPrivate *priv = wp_endpoint_link_get_instance_private (self);

  g_clear_pointer (&priv->properties, wp_properties_unref);
  g_clear_pointer (&priv->info, pw_endpoint_link_info_free);

  G_OBJECT_CLASS (wp_endpoint_link_parent_class)->finalize (object);
}

static gconstpointer
wp_endpoint_link_get_info (WpProxy * proxy)
{
  WpEndpointLink *self = WP_ENDPOINT_LINK (proxy);
  WpEndpointLinkPrivate *priv = wp_endpoint_link_get_instance_private (self);

  return priv->info;
}

static WpProperties *
wp_endpoint_link_get_properties (WpProxy * proxy)
{
  WpEndpointLink *self = WP_ENDPOINT_LINK (proxy);
  WpEndpointLinkPrivate *priv = wp_endpoint_link_get_instance_private (self);

  return wp_properties_ref (priv->properties);
}

static gint
wp_endpoint_link_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const struct spa_pod *filter)
{
  WpEndpointLinkPrivate *priv =
      wp_endpoint_link_get_instance_private (WP_ENDPOINT_LINK (self));
  int endpoint_link_enum_params_result;

  endpoint_link_enum_params_result =
      pw_endpoint_link_enum_params (priv->iface, 0, id, start, num, filter);
  g_warn_if_fail (endpoint_link_enum_params_result >= 0);

  return endpoint_link_enum_params_result;
}

static gint
wp_endpoint_link_subscribe_params (WpProxy * self, guint32 n_ids, guint32 *ids)
{
  WpEndpointLinkPrivate *priv =
      wp_endpoint_link_get_instance_private (WP_ENDPOINT_LINK (self));
  int endpoint_link_subscribe_params_result;

  endpoint_link_subscribe_params_result =
      pw_endpoint_link_subscribe_params (priv->iface, ids, n_ids);
  g_warn_if_fail (endpoint_link_subscribe_params_result >= 0);

  return endpoint_link_subscribe_params_result;
}

static gint
wp_endpoint_link_set_param (WpProxy * self, guint32 id, guint32 flags,
    const struct spa_pod *param)
{
  WpEndpointLinkPrivate *priv =
      wp_endpoint_link_get_instance_private (WP_ENDPOINT_LINK (self));
  int endpoint_link_set_param_result;

  endpoint_link_set_param_result =
      pw_endpoint_link_set_param (priv->iface, id, flags, param);
  g_warn_if_fail (endpoint_link_set_param_result >= 0);

  return endpoint_link_set_param_result;
}

static void
endpoint_link_event_info (void *data, const struct pw_endpoint_link_info *info)
{
  WpEndpointLink *self = WP_ENDPOINT_LINK (data);
  WpEndpointLinkPrivate *priv = wp_endpoint_link_get_instance_private (self);
  WpEndpointLinkState old_state =
      priv->info ? priv->info->state : PW_ENDPOINT_LINK_STATE_ERROR;

  priv->info = pw_endpoint_link_info_update (priv->info, info);

  if (info->change_mask & PW_ENDPOINT_LINK_CHANGE_MASK_PROPS) {
    g_clear_pointer (&priv->properties, wp_properties_unref);
    priv->properties = wp_properties_new_wrap_dict (priv->info->props);
  }

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_ENDPOINT_LINK_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  if (info->change_mask & PW_ENDPOINT_LINK_CHANGE_MASK_STATE) {
    g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0,
        old_state, info->state, info->error);
  }
}

static const struct pw_endpoint_link_events endpoint_link_events = {
  PW_VERSION_ENDPOINT_LINK_EVENTS,
  .info = endpoint_link_event_info,
  .param = wp_proxy_handle_event_param,
};

static void
wp_endpoint_link_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpEndpointLink *self = WP_ENDPOINT_LINK (proxy);
  WpEndpointLinkPrivate *priv = wp_endpoint_link_get_instance_private (self);

  priv->iface = (struct pw_endpoint_link *) pw_proxy;
  pw_endpoint_link_add_listener (priv->iface, &priv->listener,
      &endpoint_link_events, self);
}

static void
wp_endpoint_link_class_init (WpEndpointLinkClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_endpoint_link_finalize;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_EndpointLink;
  proxy_class->pw_iface_version = PW_VERSION_ENDPOINT_LINK;

  proxy_class->get_info = wp_endpoint_link_get_info;
  proxy_class->get_properties = wp_endpoint_link_get_properties;
  proxy_class->enum_params = wp_endpoint_link_enum_params;
  proxy_class->subscribe_params = wp_endpoint_link_subscribe_params;
  proxy_class->set_param = wp_endpoint_link_set_param;
  proxy_class->pw_proxy_created = wp_endpoint_link_pw_proxy_created;

  /**
   * WpEndpointLink::state-changed:
   * @self: the endpoint link
   * @old_state: the old state of the link
   * @new_state: the new state of the link
   * @error: (nullable): the error string if the new state is
   *   %WP_ENDPOINT_LINK_STATE_ERROR
   *
   * Emitted when an endpoint link changes state
   */
  signals[SIGNAL_STATE_CHANGED] = g_signal_new (
      "state-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 3,
      WP_TYPE_ENDPOINT_LINK_STATE, WP_TYPE_ENDPOINT_LINK_STATE, G_TYPE_STRING);
}

/**
 * wp_endpoint_link_get_linked_object_ids:
 * @self: the endpoint link
 * @output_endpoint: (out) (optional): the bound id of the output (source)
 *    endpoint
 * @output_stream: (out) (optional): the bound id of the output (source)
 *    endpoint's stream
 * @input_endpoint: (out) (optional): the bound id of the input (sink)
 *    endpoint
 * @input_stream: (out) (optional): the bound id of the input (sink)
 *    endpoint's stream
 *
 * Retrieves the ids of the objects that are linked by this endpoint link
 *
 * Note: Using this method requires %WP_PROXY_FEATURE_INFO
 */
void
wp_endpoint_link_get_linked_object_ids (WpEndpointLink * self,
    guint32 * output_endpoint, guint32 * output_stream,
    guint32 * input_endpoint, guint32 * input_stream)
{
  g_return_if_fail (WP_IS_ENDPOINT_LINK (self));

  WpEndpointLinkPrivate *priv = wp_endpoint_link_get_instance_private (self);
  g_return_if_fail (priv->info);

  if (output_endpoint)
    *output_endpoint = priv->info->output_endpoint_id;
  if (output_stream)
    *output_stream = priv->info->output_stream_id;
  if (input_endpoint)
    *input_endpoint = priv->info->input_endpoint_id;
  if (input_stream)
    *input_stream = priv->info->input_stream_id;
}

/**
 * wp_endpoint_link_get_state:
 * @self: the endpoint link
 * @error: (out) (optional) (transfer none): the error string if the state is
 *   %WP_ENDPOINT_LINK_STATE_ERROR
 *
 * Retrieves the current state of the link
 *
 * Note: Using this method requires %WP_PROXY_FEATURE_INFO
 * Returns: the current state of the link
 */
WpEndpointLinkState
wp_endpoint_link_get_state (WpEndpointLink * self, const gchar ** error)
{
  g_return_val_if_fail (WP_IS_ENDPOINT_LINK (self), WP_ENDPOINT_LINK_STATE_ERROR);

  WpEndpointLinkPrivate *priv = wp_endpoint_link_get_instance_private (self);
  g_return_val_if_fail (priv->info, WP_ENDPOINT_LINK_STATE_ERROR);

  if (error)
    *error = priv->info->error;
  return priv->info->state;
}

/**
 * wp_endpoint_link_request_state:
 * @self: the endpoint link
 * @target: the desired target state of the link
 *
 * Requests a state change on the link
 *
 * Note: Using this method requires %WP_PROXY_FEATURE_PW_PROXY
 */
void
wp_endpoint_link_request_state (WpEndpointLink * self,
    WpEndpointLinkState target)
{
  g_return_if_fail (WP_IS_ENDPOINT_LINK (self));

  WpEndpointLinkPrivate *priv = wp_endpoint_link_get_instance_private (self);
  g_return_if_fail (priv->iface);

  pw_endpoint_link_request_state (priv->iface,
      (enum pw_endpoint_link_state) target);
}

/* WpImplEndpointLink */

enum {
  IMPL_PROP_0,
  IMPL_PROP_ITEM,
};

struct _WpImplEndpointLink
{
  WpEndpointLink parent;

  struct spa_interface iface;
  struct spa_hook_list hooks;
  struct pw_endpoint_link_info info;

  WpSiLink *item;
};

G_DEFINE_TYPE (WpImplEndpointLink, wp_impl_endpoint_link, WP_TYPE_ENDPOINT_LINK)

#define pw_endpoint_link_emit(hooks,method,version,...) \
    spa_hook_list_call_simple(hooks, struct pw_endpoint_link_events, \
        method, version, ##__VA_ARGS__)

#define pw_endpoint_link_emit_info(hooks,...)  \
    pw_endpoint_link_emit(hooks, info, 0, ##__VA_ARGS__)
#define pw_endpoint_link_emit_param(hooks,...) \
    pw_endpoint_link_emit(hooks, param, 0, ##__VA_ARGS__)

static int
impl_add_listener(void *object,
    struct spa_hook *listener,
    const struct pw_endpoint_link_events *events,
    void *data)
{
  WpImplEndpointLink *self = WP_IMPL_ENDPOINT_LINK (object);
  struct spa_hook_list save;

  spa_hook_list_isolate (&self->hooks, &save, listener, events, data);

  self->info.change_mask = PW_ENDPOINT_LINK_CHANGE_MASK_ALL;
  pw_endpoint_link_emit_info (&self->hooks, &self->info);
  self->info.change_mask = 0;

  spa_hook_list_join (&self->hooks, &save);
  return 0;
}

static int
impl_enum_params (void *object, int seq,
    uint32_t id, uint32_t start, uint32_t num,
    const struct spa_pod *filter)
{
  return -ENOENT;
}

static int
impl_subscribe_params (void *object, uint32_t *ids, uint32_t n_ids)
{
  return 0;
}

static int
impl_set_param (void *object, uint32_t id, uint32_t flags,
    const struct spa_pod *param)
{
  return -ENOENT;
}

static void
on_item_activated (WpSessionItem * item, GAsyncResult * res, gpointer data)
{
  WpImplEndpointLink *self = WP_IMPL_ENDPOINT_LINK (data);
  g_autoptr (GError) error = NULL;

  if (!wp_session_item_activate_finish (item, res, &error)) {
    wp_message_object (self, "failed to activate link: %s", error->message);
    self->info.error = g_strdup (error->message);
    /* on_si_link_flags_changed() will be called right after we return,
       taking care of the rest... */
  }
}

static int
impl_request_state (void *object, enum pw_endpoint_link_state state)
{
  WpImplEndpointLink *self = WP_IMPL_ENDPOINT_LINK (object);
  int ret = 0;

  if (state == self->info.state)
    return ret;

  switch (state) {
  case PW_ENDPOINT_LINK_STATE_ACTIVE:
    wp_session_item_deactivate (WP_SESSION_ITEM (self->item));
    wp_session_item_activate (WP_SESSION_ITEM (self->item),
        (GAsyncReadyCallback) on_item_activated, self);
    break;
  case PW_ENDPOINT_LINK_STATE_INACTIVE:
    wp_session_item_deactivate (WP_SESSION_ITEM (self->item));
    break;
  default:
    ret = -EINVAL;
    break;
  }

  return ret;
}

static const struct pw_endpoint_link_methods impl_endpoint_link = {
  PW_VERSION_ENDPOINT_LINK_METHODS,
  .add_listener = impl_add_listener,
  .subscribe_params = impl_subscribe_params,
  .enum_params = impl_enum_params,
  .set_param = impl_set_param,
  .request_state = impl_request_state,
};

static void
populate_properties (WpImplEndpointLink * self, WpProperties *global_props)
{
  WpEndpointLinkPrivate *priv =
      wp_endpoint_link_get_instance_private (WP_ENDPOINT_LINK (self));

  g_clear_pointer (&priv->properties, wp_properties_unref);
  priv->properties = wp_si_link_get_properties (self->item);
  priv->properties = wp_properties_ensure_unique_owner (priv->properties);
  wp_properties_update (priv->properties, global_props);

  self->info.props = priv->properties ?
      (struct spa_dict *) wp_properties_peek_dict (priv->properties) : NULL;

  g_object_notify (G_OBJECT (self), "properties");
}

static void
on_si_link_properties_changed (WpSiLink * item, WpImplEndpointLink * self)
{
  populate_properties (self, wp_proxy_get_global_properties (WP_PROXY (self)));

  self->info.change_mask = PW_ENDPOINT_LINK_CHANGE_MASK_PROPS;
  pw_endpoint_link_emit_info (&self->hooks, &self->info);
  self->info.change_mask = 0;
}

static void
on_si_link_flags_changed (WpSiLink * item, WpSiFlags flags,
    WpImplEndpointLink * self)
{
  enum pw_endpoint_link_state old_state = self->info.state;

  if (flags & WP_SI_FLAG_IN_ERROR)
    self->info.state = PW_ENDPOINT_LINK_STATE_ERROR;
  else if (flags & WP_SI_FLAG_ACTIVE)
    self->info.state = PW_ENDPOINT_LINK_STATE_ACTIVE;
  else if (flags & WP_SI_FLAG_ACTIVATING)
    self->info.state = PW_ENDPOINT_LINK_STATE_PREPARING;
  else
    self->info.state = PW_ENDPOINT_LINK_STATE_INACTIVE;

  if (self->info.state != PW_ENDPOINT_LINK_STATE_ERROR)
    g_clear_pointer (&self->info.error, g_free);

  if (old_state != self->info.state) {
    self->info.change_mask = PW_ENDPOINT_LINK_CHANGE_MASK_STATE;
    pw_endpoint_link_emit_info (&self->hooks, &self->info);
    self->info.change_mask = 0;

    g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0,
        old_state, self->info.state, self->info.error);
  }
}

static void
wp_impl_endpoint_link_init (WpImplEndpointLink * self)
{
  /* reuse the parent's private to optimize memory usage and to be able
     to re-use some of the parent's methods without reimplementing them */
  WpEndpointLinkPrivate *priv =
      wp_endpoint_link_get_instance_private (WP_ENDPOINT_LINK (self));

  self->iface = SPA_INTERFACE_INIT (
      PW_TYPE_INTERFACE_EndpointLink,
      PW_VERSION_ENDPOINT_LINK,
      &impl_endpoint_link, self);
  spa_hook_list_init (&self->hooks);

  priv->iface = (struct pw_endpoint_link *) &self->iface;
}

static void
wp_impl_endpoint_link_finalize (GObject * object)
{
  WpImplEndpointLink *self = WP_IMPL_ENDPOINT_LINK (object);
  WpEndpointLinkPrivate *priv =
      wp_endpoint_link_get_instance_private (WP_ENDPOINT_LINK (self));

  g_free (self->info.error);
  priv->info = NULL;

  G_OBJECT_CLASS (wp_impl_endpoint_link_parent_class)->finalize (object);
}

static void
wp_impl_endpoint_link_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpImplEndpointLink *self = WP_IMPL_ENDPOINT_LINK (object);

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
wp_impl_endpoint_link_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpImplEndpointLink *self = WP_IMPL_ENDPOINT_LINK (object);

  switch (property_id) {
  case IMPL_PROP_ITEM:
    g_value_set_object (value, self->item);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_impl_endpoint_link_augment (WpProxy * proxy, WpProxyFeatures features)
{
  WpImplEndpointLink *self = WP_IMPL_ENDPOINT_LINK (proxy);
  WpEndpointLinkPrivate *priv =
      wp_endpoint_link_get_instance_private (WP_ENDPOINT_LINK (self));
  g_autoptr (GVariant) info = NULL;
  g_autoptr (GVariantIter) immutable_props = NULL;
  g_autoptr (WpProperties) props = NULL;

  /* PW_PROXY depends on BOUND */
  if (features & WP_PROXY_FEATURE_PW_PROXY)
    features |= WP_PROXY_FEATURE_BOUND;

  /* BOUND depends on INFO */
  if (features & WP_PROXY_FEATURE_BOUND)
    features |= WP_PROXY_FEATURE_INFO;

  if (features & WP_PROXY_FEATURE_INFO) {
    const gchar *key, *value;
    WpSiStream *stream;

    /* get info from the interface */
    info = wp_si_link_get_registration_info (self->item);
    g_variant_get (info, "a{ss}", &immutable_props);

    /* get the current state */
    self->info.state =
        (wp_session_item_get_flags (WP_SESSION_ITEM (self->item))
            & WP_SI_FLAG_ACTIVE)
        ? WP_ENDPOINT_LINK_STATE_ACTIVE
        : WP_ENDPOINT_LINK_STATE_INACTIVE;

    /* associate with the session, the endpoints and the streams */
    self->info.session_id = wp_session_item_get_associated_proxy_id (
        WP_SESSION_ITEM (self->item), WP_TYPE_SESSION);

    stream = wp_si_link_get_out_stream (self->item);
    self->info.output_endpoint_id = wp_session_item_get_associated_proxy_id (
        WP_SESSION_ITEM (stream), WP_TYPE_ENDPOINT);
    self->info.output_stream_id = wp_session_item_get_associated_proxy_id (
        WP_SESSION_ITEM (stream), WP_TYPE_ENDPOINT_STREAM);

    stream = wp_si_link_get_in_stream (self->item);
    self->info.input_endpoint_id = wp_session_item_get_associated_proxy_id (
        WP_SESSION_ITEM (stream), WP_TYPE_ENDPOINT);
    self->info.input_stream_id = wp_session_item_get_associated_proxy_id (
        WP_SESSION_ITEM (stream), WP_TYPE_ENDPOINT_STREAM);

    /* construct export properties (these will come back through
       the registry and appear in wp_proxy_get_global_properties) */
    props = wp_properties_new_empty ();
    wp_properties_setf (props, PW_KEY_SESSION_ID, "%d", self->info.session_id);
    wp_properties_setf (props, PW_KEY_ENDPOINT_LINK_OUTPUT_ENDPOINT, "%d",
        self->info.output_endpoint_id);
    wp_properties_setf (props, PW_KEY_ENDPOINT_LINK_OUTPUT_STREAM, "%d",
        self->info.output_stream_id);
    wp_properties_setf (props, PW_KEY_ENDPOINT_LINK_INPUT_ENDPOINT, "%d",
        self->info.input_endpoint_id);
    wp_properties_setf (props, PW_KEY_ENDPOINT_LINK_INPUT_STREAM, "%d",
        self->info.input_stream_id);

    /* populate immutable (global) properties */
    while (g_variant_iter_next (immutable_props, "{&s&s}", &key, &value))
      wp_properties_set (props, key, value);

    /* populate standard properties */
    populate_properties (self, props);

    /* subscribe to changes */
    g_signal_connect_object (self->item, "link-properties-changed",
        G_CALLBACK (on_si_link_properties_changed), self, 0);
    g_signal_connect_object (self->item, "flags-changed",
        G_CALLBACK (on_si_link_flags_changed), self, 0);

    /* finalize info struct */
    self->info.version = PW_VERSION_ENDPOINT_LINK_INFO;
    self->info.error = NULL;
    self->info.params = NULL;
    self->info.n_params = 0;
    priv->info = &self->info;
    g_object_notify (G_OBJECT (self), "info");

    wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
  }

  if (features & WP_PROXY_FEATURE_BOUND) {
    g_autoptr (WpCore) core = wp_proxy_get_core (proxy);
    struct pw_core *pw_core = wp_core_get_pw_core (core);

    /* no pw_core -> we are not connected */
    if (!pw_core) {
      wp_proxy_augment_error (proxy, g_error_new (WP_DOMAIN_LIBRARY,
            WP_LIBRARY_ERROR_OPERATION_FAILED,
            "The WirePlumber core is not connected; "
            "object cannot be exported to PipeWire"));
      return;
    }

    wp_proxy_set_pw_proxy (proxy, pw_core_export (pw_core,
            PW_TYPE_INTERFACE_EndpointLink,
            wp_properties_peek_dict (props),
            priv->iface, 0));
  }
}

static void
wp_impl_endpoint_link_class_init (WpImplEndpointLinkClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_impl_endpoint_link_finalize;
  object_class->set_property = wp_impl_endpoint_link_set_property;
  object_class->get_property = wp_impl_endpoint_link_get_property;

  proxy_class->augment = wp_impl_endpoint_link_augment;

  proxy_class->pw_proxy_created = NULL;
  proxy_class->param = NULL;

  g_object_class_install_property (object_class, IMPL_PROP_ITEM,
      g_param_spec_object ("item", "item", "item", WP_TYPE_SI_LINK,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpImplEndpointLink *
wp_impl_endpoint_link_new (WpCore * core, WpSiLink * item)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_IMPL_ENDPOINT_LINK,
      "core", core,
      "item", item,
      NULL);
}
