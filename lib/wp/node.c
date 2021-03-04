/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: node
 * @title: PipeWire Node
 */

#define G_LOG_DOMAIN "wp-node"

#include "node.h"
#include "core.h"
#include "object-manager.h"
#include "debug.h"
#include "wpenums.h"
#include "private/pipewire-object-mixin.h"

#include <pipewire/impl.h>

enum {
  SIGNAL_STATE_CHANGED,
  SIGNAL_PORTS_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

struct _WpNode
{
  WpGlobalProxy parent;
  WpObjectManager *ports_om;
};

static void wp_node_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

/**
 * WpNode:
 *
 * The #WpNode class allows accessing the properties and methods of a
 * PipeWire node object (`struct pw_node`).
 *
 * A #WpNode is constructed internally when a new node appears on the
 * PipeWire registry and it is made available through the #WpObjectManager API.
 * Alternatively, a #WpNode can also be constructed using
 * wp_node_new_from_factory(), which creates a new node object
 * on the remote PipeWire server by calling into a factory.
 */
G_DEFINE_TYPE_WITH_CODE (WpNode, wp_node, WP_TYPE_GLOBAL_PROXY,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT,
        wp_pw_object_mixin_object_interface_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_node_pw_object_mixin_priv_interface_init))

static void
wp_node_init (WpNode * self)
{
}

static void
wp_node_on_ports_om_installed (WpObjectManager *ports_om, WpNode * self)
{
  wp_object_update_features (WP_OBJECT (self), WP_NODE_FEATURE_PORTS, 0);
}

static void
wp_node_emit_ports_changed (WpObjectManager *ports_om, WpNode * self)
{
  g_signal_emit (self, signals[SIGNAL_PORTS_CHANGED], 0);
}

static void
wp_node_enable_feature_ports (WpNode * self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  guint32 bound_id = wp_proxy_get_bound_id (WP_PROXY (self));

  wp_debug_object (self, "enabling WP_NODE_FEATURE_PORTS, bound_id:%u",
      bound_id);

  self->ports_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->ports_om,
      WP_TYPE_PORT,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_NODE_ID, "=u", bound_id,
      NULL);
  wp_object_manager_request_object_features (self->ports_om,
      WP_TYPE_PORT, WP_OBJECT_FEATURES_ALL);

  g_signal_connect_object (self->ports_om, "installed",
      G_CALLBACK (wp_node_on_ports_om_installed), self, 0);
  g_signal_connect_object (self->ports_om, "objects-changed",
      G_CALLBACK (wp_node_emit_ports_changed), self, 0);

  wp_core_install_object_manager (core, self->ports_om);
}

static WpObjectFeatures
wp_node_get_supported_features (WpObject * object)
{
  return wp_pw_object_mixin_get_supported_features (object)
      | WP_NODE_FEATURE_PORTS;
}

enum {
  STEP_PORTS = WP_PW_OBJECT_MIXIN_STEP_CUSTOM_START,
};

static void
wp_node_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PW_OBJECT_MIXIN_STEP_BIND:
  case WP_TRANSITION_STEP_ERROR:
    /* base class can handle BIND and ERROR */
    WP_OBJECT_CLASS (wp_node_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  case WP_PW_OBJECT_MIXIN_STEP_WAIT_INFO:
    /* just wait, info will be emitted anyway after binding */
    break;
  case WP_PW_OBJECT_MIXIN_STEP_CACHE_PARAMS:
    wp_pw_object_mixin_cache_params (object, missing);
    break;
  case STEP_PORTS:
    wp_node_enable_feature_ports (WP_NODE (object));
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_node_deactivate (WpObject * object, WpObjectFeatures features)
{
  wp_pw_object_mixin_deactivate (object, features);

  if (features & WP_NODE_FEATURE_PORTS) {
    WpNode *self = WP_NODE (object);
    g_clear_object (&self->ports_om);
    wp_object_update_features (object, 0, WP_NODE_FEATURE_PORTS);
  }

  WP_OBJECT_CLASS (wp_node_parent_class)->deactivate (object, features);
}

static const struct pw_node_events node_events = {
  PW_VERSION_NODE_EVENTS,
  .info = (HandleEventInfoFunc(node)) wp_pw_object_mixin_handle_event_info,
  .param = wp_pw_object_mixin_handle_event_param,
};

static void
wp_node_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_created (proxy, pw_proxy,
      node, &node_events);
}

static void
wp_node_pw_proxy_destroyed (WpProxy * proxy)
{
  WpNode *self = WP_NODE (proxy);

  wp_pw_object_mixin_handle_pw_proxy_destroyed (proxy);

  g_clear_object (&self->ports_om);
  wp_object_update_features (WP_OBJECT (self), 0, WP_NODE_FEATURE_PORTS);
}

static void
wp_node_class_init (WpNodeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_pw_object_mixin_get_property;

  wpobject_class->get_supported_features = wp_node_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_pw_object_mixin_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_node_activate_execute_step;
  wpobject_class->deactivate = wp_node_deactivate;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Node;
  proxy_class->pw_iface_version = PW_VERSION_NODE;
  proxy_class->pw_proxy_created = wp_node_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_node_pw_proxy_destroyed;

  wp_pw_object_mixin_class_override_properties (object_class);

  /**
   * WpNode::state-changed:
   * @self: the node
   * @old_state: the old state
   * @new_state: the new state
   *
   * Emitted when the node changes state. This is only emitted
   * when %WP_PIPEWIRE_OBJECT_FEATURE_INFO is enabled.
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

static void
wp_node_process_info (gpointer instance, gpointer old_info, gpointer i)
{
  const struct pw_node_info *info = i;

  if (info->change_mask & PW_NODE_CHANGE_MASK_STATE) {
    enum pw_node_state old_state = old_info ?
        ((struct pw_node_info *) old_info)->state : PW_NODE_STATE_CREATING;
    g_signal_emit (instance, signals[SIGNAL_STATE_CHANGED], 0,
        old_state, info->state);
  }
}

static gint
wp_node_enum_params (gpointer instance, guint32 id,
    guint32 start, guint32 num, WpSpaPod *filter)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  return pw_node_enum_params (d->iface, 0, id, start, num,
      filter ? wp_spa_pod_get_spa_pod (filter) : NULL);
}

static gint
wp_node_set_param (gpointer instance, guint32 id, guint32 flags,
    WpSpaPod * param)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  return pw_node_set_param (d->iface, id, flags,
      wp_spa_pod_get_spa_pod (param));
}

static void
wp_node_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  wp_pw_object_mixin_priv_interface_info_init (iface, node, NODE);
  iface->process_info = wp_node_process_info;
  iface->enum_params = wp_node_enum_params;
  iface->set_param = wp_node_set_param;
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
 * this is done, you should call wp_object_activate(), requesting at least
 * %WP_PROXY_FEATURE_BOUND. When this feature is ready, the node is ready for
 * use on the server. If the node cannot be created, this activation operation
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
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, WP_NODE_STATE_ERROR);

  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);
  const struct pw_node_info *info = d->info;

  if (error)
    *error = info->error;
  return (WpNodeState) info->state;
}

/**
 * wp_node_get_n_input_ports:
 * @self: the node
 * @max: (out) (optional): the maximum supported number of input ports
 *
 * Requires %WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * Returns: the number of input ports of this node, as reported by the node info
 */
guint
wp_node_get_n_input_ports (WpNode * self, guint * max)
{
  g_return_val_if_fail (WP_IS_NODE (self), 0);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);
  const struct pw_node_info *info = d->info;

  if (max)
    *max = info->max_input_ports;
  return info->n_input_ports;
}

/**
 * wp_node_get_n_output_ports:
 * @self: the node
 * @max: (out) (optional): the maximum supported number of output ports
 *
 * Requires %WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * Returns: the number of output ports of this node, as reported by the node info
 */
guint
wp_node_get_n_output_ports (WpNode * self, guint * max)
{
  g_return_val_if_fail (WP_IS_NODE (self), 0);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);
  const struct pw_node_info *info = d->info;

  if (max)
    *max = info->max_output_ports;
  return info->n_output_ports;
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
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_NODE_FEATURE_PORTS, 0);

  return wp_object_manager_get_n_objects (self->ports_om);
}

/**
 * wp_node_new_ports_iterator:
 * @self: the node
 *
 * Requires %WP_NODE_FEATURE_PORTS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the ports that belong to this node
 */
WpIterator *
wp_node_new_ports_iterator (WpNode * self)
{
  g_return_val_if_fail (WP_IS_NODE (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_NODE_FEATURE_PORTS, NULL);

  return wp_object_manager_new_iterator (self->ports_om);
}

/**
 * wp_node_new_ports_filtered_iterator:
 * @self: the node
 * @...: a list of constraints, terminated by %NULL
 *
 * Requires %WP_NODE_FEATURE_PORTS
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the ports that belong to this node and match the constraints
 */
WpIterator *
wp_node_new_ports_filtered_iterator (WpNode * self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_PORT, &args);
  va_end (args);
  return wp_node_new_ports_filtered_iterator_full (self, interest);
}

/**
 * wp_node_new_ports_filtered_iterator_full: (rename-to wp_node_new_ports_filtered_iterator)
 * @self: the node
 * @interest: (transfer full): the interest
 *
 * Requires %WP_NODE_FEATURE_PORTS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the ports that belong to this node and match the @interest
 */
WpIterator *
wp_node_new_ports_filtered_iterator_full (WpNode * self,
    WpObjectInterest * interest)
{
  g_return_val_if_fail (WP_IS_NODE (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_NODE_FEATURE_PORTS, NULL);

  return wp_object_manager_new_filtered_iterator_full (self->ports_om,
      interest);
}

/**
 * wp_node_lookup_port:
 * @self: the node
 * @...: a list of constraints, terminated by %NULL
 *
 * Requires %WP_NODE_FEATURE_PORTS
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * Returns: (transfer full) (nullable): the first port that matches the
 *    constraints, or %NULL if there is no such port
 */
WpPort *
wp_node_lookup_port (WpNode * self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_PORT, &args);
  va_end (args);
  return wp_node_lookup_port_full (self, interest);
}

/**
 * wp_node_lookup_port_full: (rename-to wp_node_lookup_port)
 * @self: the node
 * @interest: (transfer full): the interest
 *
 * Requires %WP_NODE_FEATURE_PORTS
 *
 * Returns: (transfer full) (nullable): the first port that matches the
 *    @interest, or %NULL if there is no such port
 */
WpPort *
wp_node_lookup_port_full (WpNode * self, WpObjectInterest * interest)
{
  g_return_val_if_fail (WP_IS_NODE (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_NODE_FEATURE_PORTS, NULL);

  return (WpPort *)
      wp_object_manager_lookup_full (self->ports_om, interest);
}

/**
 * wp_node_send_command:
 * @self: the node
 * @command: the command
 *
 * Sends a command to a node
 */
void wp_node_send_command (WpNode * self, const gchar * command)
{
  WpSpaIdValue command_value = wp_spa_id_value_from_short_name (
      "Spa:Pod:Object:Command:Node", command);

  g_return_if_fail (WP_IS_NODE (self));
  g_return_if_fail (command_value != NULL);

  struct spa_command cmd =
      SPA_NODE_COMMAND_INIT(wp_spa_id_value_number (command_value));
  pw_node_send_command (wp_proxy_get_pw_proxy (WP_PROXY (self)), &cmd);
}


enum {
  PROP_0,
  PROP_PW_IMPL_NODE,
};

struct _WpImplNode
{
  WpProxy parent;
  GWeakRef core;
  struct pw_impl_node *pw_impl_node;
  struct pw_proxy *proxy;
};

/**
 * WpImplNode:
 *
 * A #WpImplNode allows running a node implementation (`struct pw_impl_node`)
 * locally, loading the implementation from factory or wrapping a manually
 * constructed `pw_impl_node`. This object can then be exported to PipeWire
 * by requesting %WP_PROXY_FEATURE_BOUND.
 */
G_DEFINE_TYPE (WpImplNode, wp_impl_node, WP_TYPE_PROXY)

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

static WpObjectFeatures
wp_impl_node_get_supported_features (WpObject * object)
{
  return WP_PROXY_FEATURE_BOUND;
}

enum {
  STEP_EXPORT = WP_TRANSITION_STEP_CUSTOM_START,
};

static guint
wp_impl_node_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  /* we only support BOUND, so this is the only
     feature that can be in @missing */
  g_return_val_if_fail (missing == WP_PROXY_FEATURE_BOUND,
      WP_TRANSITION_STEP_ERROR);

  return STEP_EXPORT;
}

static void
wp_impl_node_activate_execute_step (WpObject * object,
      WpFeatureActivationTransition * transition, guint step,
      WpObjectFeatures missing)
{
  WpImplNode *self = WP_IMPL_NODE (object);

  switch (step) {
  case STEP_EXPORT: {
    g_autoptr (WpCore) core = wp_object_get_core (object);
    struct pw_core *pw_core = wp_core_get_pw_core (core);
    g_return_if_fail (pw_core);

    wp_proxy_set_pw_proxy (WP_PROXY (self),
        pw_core_export (pw_core, PW_TYPE_INTERFACE_Node, NULL,
            self->pw_impl_node, 0));
    break;
  }
  default:
    g_assert_not_reached ();
  }
}

static void
wp_impl_node_class_init (WpImplNodeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;

  object_class->finalize = wp_impl_node_finalize;
  object_class->set_property = wp_impl_node_set_property;
  object_class->get_property = wp_impl_node_get_property;

  wpobject_class->get_supported_features = wp_impl_node_get_supported_features;
  wpobject_class->activate_get_next_step = wp_impl_node_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_impl_node_activate_execute_step;

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
 * wp_object_activate() requesting %WP_PROXY_FEATURE_BOUND and
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
