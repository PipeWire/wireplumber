/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpNode
 *
 * The #WpNode class allows accessing the properties and methods of a
 * PipeWire node object (`struct pw_node`).
 *
 * A #WpNode is constructed internally when a new node appears on the
 * PipeWire registry and it is made available through the #WpObjectManager API.
 * Alternatively, a #WpNode can also be constructed using
 * wp_node_new_from_factory(), which creates a new node object
 * on the remote PipeWire server by calling into a factory.
 *
 * A #WpImplNode allows running a node implementation (`struct pw_impl_node`)
 * locally, loading the implementation from factory or wrapping a manually
 * constructed `pw_impl_node`. This object can then be exported to PipeWire
 * by requesting %WP_PROXY_FEATURE_BOUND and be used as if it was a #WpNode
 * proxy to a remote object.
 */

#define G_LOG_DOMAIN "wp-node"

#include "node.h"
#include "debug.h"
#include "error.h"
#include "private.h"
#include "wpenums.h"

#include <pipewire/pipewire.h>
#include <pipewire/impl.h>

enum {
  SIGNAL_STATE_CHANGED,
  SIGNAL_PORTS_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

typedef struct _WpNodePrivate WpNodePrivate;
struct _WpNodePrivate
{
  struct pw_node_info *info;
  struct spa_hook listener;
  WpObjectManager *ports_om;
  gboolean ft_ports_requested;
};

G_DEFINE_TYPE_WITH_PRIVATE (WpNode, wp_node, WP_TYPE_PROXY)

static void
wp_node_init (WpNode * self)
{
}

static void
wp_node_finalize (GObject * object)
{
  WpNode *self = WP_NODE (object);
  WpNodePrivate *priv = wp_node_get_instance_private (self);

  g_clear_pointer (&priv->info, pw_node_info_free);
  g_clear_object (&priv->ports_om);

  G_OBJECT_CLASS (wp_node_parent_class)->finalize (object);
}

static void
wp_node_on_ports_om_installed (WpObjectManager *ports_om, WpNode * self)
{
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_NODE_FEATURE_PORTS);
}

static void
wp_node_emit_ports_changed (WpObjectManager *ports_om, WpNode * self)
{
  g_signal_emit (self, signals[SIGNAL_PORTS_CHANGED], 0);
}

static void
wp_node_ensure_feature_ports (WpNode * self, guint32 bound_id)
{
  WpNodePrivate *priv = wp_node_get_instance_private (self);
  WpProxyFeatures ft = wp_proxy_get_features (WP_PROXY (self));

  if (priv->ft_ports_requested && !priv->ports_om &&
      (ft & WP_PROXY_FEATURES_STANDARD) == WP_PROXY_FEATURES_STANDARD)
  {
    g_autoptr (WpCore) core = wp_proxy_get_core (WP_PROXY (self));
    GVariantBuilder b;

    if (!bound_id)
      bound_id = wp_proxy_get_bound_id (WP_PROXY (self));

    /* proxy node port -> check for node.id in global properties */
    g_variant_builder_init (&b, G_VARIANT_TYPE ("aa{sv}"));
    g_variant_builder_open (&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}", "type",
        g_variant_new_int32 (WP_OBJECT_MANAGER_CONSTRAINT_PW_GLOBAL_PROPERTY));
    g_variant_builder_add (&b, "{sv}", "name",
        g_variant_new_string (PW_KEY_NODE_ID));
    g_variant_builder_add (&b, "{sv}", "value",
        g_variant_new_take_string (g_strdup_printf ("%u", bound_id)));
    g_variant_builder_close (&b);

    priv->ports_om = wp_object_manager_new ();
    wp_object_manager_add_interest (priv->ports_om,
        WP_TYPE_PORT,
        g_variant_builder_end (&b),
        WP_PROXY_FEATURES_STANDARD);

    g_signal_connect_object (priv->ports_om, "installed",
        G_CALLBACK (wp_node_on_ports_om_installed), self, 0);
    g_signal_connect_object (priv->ports_om, "objects-changed",
        G_CALLBACK (wp_node_emit_ports_changed), self, 0);

    wp_core_install_object_manager (core, priv->ports_om);
  }
}

static void
wp_node_augment (WpProxy * proxy, WpProxyFeatures features)
{
  WpNode *self = WP_NODE (proxy);
  WpNodePrivate *priv = wp_node_get_instance_private (self);

  /* call the parent impl first to ensure we have a pw proxy if necessary */
  WP_PROXY_CLASS (wp_node_parent_class)->augment (proxy, features);

  if (features & WP_NODE_FEATURE_PORTS) {
    priv->ft_ports_requested = TRUE;
    wp_node_ensure_feature_ports (self, 0);
  }
}

static gconstpointer
wp_node_get_info (WpProxy * self)
{
  WpNodePrivate *priv = wp_node_get_instance_private (WP_NODE (self));
  return priv->info;
}

static WpProperties *
wp_node_get_properties (WpProxy * self)
{
  WpNodePrivate *priv = wp_node_get_instance_private (WP_NODE (self));
  return wp_properties_new_wrap_dict (priv->info->props);
}

static gint
wp_node_enum_params (WpProxy * self, guint32 id, guint32 start,
    guint32 num, const WpSpaPod * filter)
{
  struct pw_node *pwp;
  int node_enum_params_result;

  pwp = (struct pw_node *) wp_proxy_get_pw_proxy (self);
  node_enum_params_result = pw_node_enum_params (pwp, 0, id, start, num,
      filter ? wp_spa_pod_get_spa_pod (filter) : NULL);
  g_warn_if_fail (node_enum_params_result >= 0);

  return node_enum_params_result;
}

static gint
wp_node_subscribe_params (WpProxy * self, guint32 n_ids, guint32 *ids)
{
  struct pw_node *pwp;
  int node_subscribe_params_result;

  pwp = (struct pw_node *) wp_proxy_get_pw_proxy (self);
  node_subscribe_params_result = pw_node_subscribe_params (pwp, ids, n_ids);
  g_warn_if_fail (node_subscribe_params_result >= 0);

  return node_subscribe_params_result;
}

static gint
wp_node_set_param (WpProxy * self, guint32 id, guint32 flags,
    const WpSpaPod *param)
{
  struct pw_node *pwp;
  int node_set_param_result;

  pwp = (struct pw_node *) wp_proxy_get_pw_proxy (self);
  node_set_param_result = pw_node_set_param (pwp, id, flags,
      wp_spa_pod_get_spa_pod (param));
  g_warn_if_fail (node_set_param_result >= 0);

  return node_set_param_result;
}

static void
node_event_info(void *data, const struct pw_node_info *info)
{
  WpNode *self = WP_NODE (data);
  WpNodePrivate *priv = wp_node_get_instance_private (self);
  enum pw_node_state old_state = priv->info ?
      priv->info->state : PW_NODE_STATE_CREATING;

  priv->info = pw_node_info_update (priv->info, info);
  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);

  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  if (info->change_mask & PW_NODE_CHANGE_MASK_STATE)
    g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, old_state,
        priv->info->state);

  wp_node_ensure_feature_ports (self, 0);
}

static const struct pw_node_events node_events = {
  PW_VERSION_NODE_EVENTS,
  .info = node_event_info,
  .param = wp_proxy_handle_event_param,
};

static void
wp_node_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpNode *self = WP_NODE (proxy);
  WpNodePrivate *priv = wp_node_get_instance_private (self);
  pw_node_add_listener ((struct pw_node *) pw_proxy,
      &priv->listener, &node_events, self);
}

static void
wp_node_bound (WpProxy * proxy, guint32 id)
{
  WpNode *self = WP_NODE (proxy);
  wp_node_ensure_feature_ports (self, id);
}

static void
wp_node_class_init (WpNodeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_node_finalize;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Node;
  proxy_class->pw_iface_version = PW_VERSION_NODE;

  proxy_class->augment = wp_node_augment;
  proxy_class->get_info = wp_node_get_info;
  proxy_class->get_properties = wp_node_get_properties;
  proxy_class->enum_params = wp_node_enum_params;
  proxy_class->subscribe_params = wp_node_subscribe_params;
  proxy_class->set_param = wp_node_set_param;

  proxy_class->pw_proxy_created = wp_node_pw_proxy_created;
  proxy_class->bound = wp_node_bound;

  /**
   * WpNode::state-changed:
   * @self: the node
   * @old_state: the old state
   * @new_state: the new state
   *
   * Emitted when the node changes state. This is only emitted
   * when %WP_PROXY_FEATURE_INFO is enabled.
   */
  signals[SIGNAL_STATE_CHANGED] = g_signal_new (
      "state-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
      WP_TYPE_NODE_STATE, WP_TYPE_NODE_STATE);

  /**
   * WpNode::ports-changed:
   * @self: the node
   *
   * Emitted when the node's ports change. This is only emitted
   * when %WP_NODE_FEATURE_PORTS is enabled.
   */
  signals[SIGNAL_PORTS_CHANGED] = g_signal_new (
      "ports-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/**
 * wp_node_new_from_factory:
 * @core: the wireplumber core
 * @factory_name: the pipewire factory name to construct the node
 * @properties: (nullable) (transfer full): the properties to pass to the factory
 *
 * Constructs a node on the PipeWire server by asking the remote factory
 * @factory_name to create it.
 *
 * Because of the nature of the PipeWire protocol, this operation completes
 * asynchronously at some point in the future. In order to find out when
 * this is done, you should call wp_proxy_augment(), requesting at least
 * %WP_PROXY_FEATURE_BOUND. When this feature is ready, the node is ready for
 * use on the server. If the node cannot be created, this augment operation
 * will fail.
 *
 * Returns: (nullable) (transfer full): the new node or %NULL if the core
 *   is not connected and therefore the node cannot be created
 */
WpNode *
wp_node_new_from_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties)
{
  g_autoptr (WpProperties) props = properties;
  WpNode *self = NULL;
  struct pw_core *pw_core = wp_core_get_pw_core (core);

  if (G_UNLIKELY (!pw_core)) {
    g_critical ("The WirePlumber core is not connected; node cannot be created");
    return NULL;
  }

  self = g_object_new (WP_TYPE_NODE, "core", core, NULL);
  wp_proxy_set_pw_proxy (WP_PROXY (self), pw_core_create_object (pw_core,
          factory_name, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE,
          props ? wp_properties_peek_dict (props) : NULL, 0));
  return self;
}

WpNodeState
wp_node_get_state (WpNode * self, const gchar ** error)
{
  g_return_val_if_fail (WP_IS_NODE (self), WP_NODE_STATE_ERROR);
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
          WP_PROXY_FEATURE_INFO, WP_NODE_STATE_ERROR);

  WpNodePrivate *priv = wp_node_get_instance_private (self);
  if (error)
    *error = priv->info->error;
  return (WpNodeState) priv->info->state;
}

/**
 * wp_node_get_n_input_ports:
 * @self: the node
 * @max: (out) (optional): the maximum supported number of input ports
 *
 * Requires %WP_PROXY_FEATURE_INFO
 *
 * Returns: the number of input ports of this node, as reported by the node info
 */
guint
wp_node_get_n_input_ports (WpNode * self, guint * max)
{
  g_return_val_if_fail (WP_IS_NODE (self), 0);
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
          WP_PROXY_FEATURE_INFO, 0);

  WpNodePrivate *priv = wp_node_get_instance_private (self);
  if (max)
    *max = priv->info->max_input_ports;
  return priv->info->n_input_ports;
}

/**
 * wp_node_get_n_output_ports:
 * @self: the node
 * @max: (out) (optional): the maximum supported number of output ports
 *
 * Requires %WP_PROXY_FEATURE_INFO
 *
 * Returns: the number of output ports of this node, as reported by the node info
 */
guint
wp_node_get_n_output_ports (WpNode * self, guint * max)
{
  g_return_val_if_fail (WP_IS_NODE (self), 0);
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
          WP_PROXY_FEATURE_INFO, 0);

  WpNodePrivate *priv = wp_node_get_instance_private (self);
  if (max)
    *max = priv->info->max_output_ports;
  return priv->info->n_output_ports;
}

/**
 * wp_node_get_n_ports:
 * @self: the node
 *
 * Requires %WP_NODE_FEATURE_PORTS
 *
 * Returns: the number of ports of this node. Note that this number may not
 *   add up to wp_node_get_n_input_ports() + wp_node_get_n_output_ports()
 *   because it is discovered by looking at the number of available ports
 *   in the registry, however ports may appear there with a delay or may
 *   not appear at all if this client does not have permission to read them
 */
guint
wp_node_get_n_ports (WpNode * self)
{
  g_return_val_if_fail (WP_IS_NODE (self), 0);
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
          WP_NODE_FEATURE_PORTS, 0);

  WpNodePrivate *priv = wp_node_get_instance_private (self);
  return wp_object_manager_get_n_objects (priv->ports_om);
}

/**
 * wp_node_find_port:
 * @self: the node
 * @bound_id: the bound id of the port object to find
 *
 * Requires %WP_NODE_FEATURE_PORTS
 *
 * Returns: (transfer full) (nullable): the port that has the given
 *    @bound_id, or %NULL if there is no such port
 */
WpPort *
wp_node_find_port (WpNode * self, guint32 bound_id)
{
  g_return_val_if_fail (WP_IS_NODE (self), NULL);
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
          WP_NODE_FEATURE_PORTS, NULL);

  WpNodePrivate *priv = wp_node_get_instance_private (self);
  return (WpPort *)
      wp_object_manager_find_proxy (priv->ports_om, bound_id);
}

/**
 * wp_node_iterate_ports:
 * @self: the node
 *
 * Requires %WP_NODE_FEATURE_PORTS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the ports that belong to this node
 */
WpIterator *
wp_node_iterate_ports (WpNode * self)
{
  g_return_val_if_fail (WP_IS_NODE (self), NULL);
  g_return_val_if_fail (wp_proxy_get_features (WP_PROXY (self)) &
          WP_NODE_FEATURE_PORTS, NULL);

  WpNodePrivate *priv = wp_node_get_instance_private (self);
  return wp_object_manager_iterate (priv->ports_om);
}


enum {
  PROP_0,
  PROP_PW_IMPL_NODE,
};

struct _WpImplNode
{
  WpNode parent;
  struct pw_impl_node *pw_impl_node;
};

G_DEFINE_TYPE (WpImplNode, wp_impl_node, WP_TYPE_NODE)

static void
wp_impl_node_init (WpImplNode * self)
{
}

static void
wp_impl_node_finalize (GObject * object)
{
  WpImplNode *self = WP_IMPL_NODE (object);

  g_clear_pointer (&self->pw_impl_node, pw_impl_node_destroy);

  G_OBJECT_CLASS (wp_impl_node_parent_class)->finalize (object);
}

static void
wp_impl_node_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpImplNode *self = WP_IMPL_NODE (object);

  switch (property_id) {
  case PROP_PW_IMPL_NODE:
    self->pw_impl_node = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_impl_node_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpImplNode *self = WP_IMPL_NODE (object);

  switch (property_id) {
  case PROP_PW_IMPL_NODE:
    g_value_set_pointer (value, self->pw_impl_node);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_impl_node_augment (WpProxy * proxy, WpProxyFeatures features)
{
  WpImplNode *self = WP_IMPL_NODE (proxy);

  /* if any of the default features is requested, make sure BOUND
     is also requested, as they all depend on binding the pw_impl_node */
  if (features & WP_NODE_FEATURES_STANDARD)
    features |= WP_PROXY_FEATURE_BOUND;

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

    /* export to get a proxy; feature will complete
         when the pw_proxy.bound event will be called.
       properties are NULL because they are not needed;
         remote-node uses the properties of the pw_impl_node */
    wp_proxy_set_pw_proxy (proxy, pw_core_export (pw_core,
            PW_TYPE_INTERFACE_Node, NULL, self->pw_impl_node, 0));
  }

  if (features & WP_NODE_FEATURE_PORTS) {
    WpNodePrivate *priv = wp_node_get_instance_private (WP_NODE (self));
    priv->ft_ports_requested = TRUE;
    wp_node_ensure_feature_ports (WP_NODE (self), 0);
  }
}

static void
wp_impl_node_class_init (WpImplNodeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_impl_node_finalize;
  object_class->set_property = wp_impl_node_set_property;
  object_class->get_property = wp_impl_node_get_property;

  proxy_class->augment = wp_impl_node_augment;

  g_object_class_install_property (object_class, PROP_PW_IMPL_NODE,
      g_param_spec_pointer ("pw-impl-node", "pw-impl-node",
          "The actual node implementation, struct pw_impl_node *",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/**
 * wp_impl_node_new_wrap:
 * @core: the wireplumber core
 * @node: an existing pw_impl_node to wrap
 *
 * Returns: (transfer full): A new #WpImplNode wrapping @node
 */
WpImplNode *
wp_impl_node_new_wrap (WpCore * core, struct pw_impl_node * node)
{
  return g_object_new (WP_TYPE_IMPL_NODE,
      "core", core,
      "pw-impl-node", node,
      NULL);
}

/**
 * wp_impl_node_new_from_pw_factory:
 * @core: the wireplumber core
 * @factory_name: the name of the pipewire factory
 * @properties: (nullable) (transfer full): properties to be passed to node
 *    constructor
 *
 * Constructs a new node, locally on this process, using the specified
 * @factory_name.
 *
 * To export this node to the PipeWire server, you need to call
 * wp_proxy_augment() requesting %WP_PROXY_FEATURE_BOUND and
 * wait for the operation to complete.
 *
 * Returns: (nullable) (transfer full): A new #WpImplNode wrapping the
 *   node that was constructed by the factory, or %NULL if the factory
 *   does not exist or was unable to construct the node
 */
WpImplNode *
wp_impl_node_new_from_pw_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties)
{
  g_autoptr (WpProperties) props = properties;
  struct pw_context *pw_context = wp_core_get_pw_context (core);
  struct pw_impl_factory *factory = NULL;
  struct pw_impl_node *node = NULL;

  g_return_val_if_fail (pw_context != NULL, NULL);

  factory = pw_context_find_factory (pw_context, factory_name);
  if (!factory) {
    wp_warning ("pipewire factory '%s' not found", factory_name);
    return NULL;
  }

  node = pw_impl_factory_create_object (factory,
      NULL, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE,
      props ? wp_properties_to_pw_properties (props) : NULL, 0);
  if (!node) {
    wp_warning ("failed to create node from factory '%s'", factory_name);
    return NULL;
  }

  return wp_impl_node_new_wrap (core, node);
}
