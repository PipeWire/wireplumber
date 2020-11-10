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
  struct pw_node_info *info;
  struct spa_hook listener;
  WpObjectManager *ports_om;
};

static void wp_node_pipewire_object_interface_init (WpPipewireObjectInterface * iface);

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
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT, wp_node_pipewire_object_interface_init));

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
  WpNode *self = WP_NODE (object);
  return
      WP_PROXY_FEATURE_BOUND |
      WP_NODE_FEATURE_PORTS |
      WP_PIPEWIRE_OBJECT_FEATURE_INFO |
      wp_pipewire_object_mixin_param_info_to_features (
          self->info ? self->info->params : NULL,
          self->info ? self->info->n_params : 0);
}

enum {
  STEP_PORTS = WP_PIPEWIRE_OBJECT_MIXIN_STEP_CUSTOM_START,
};

static guint
wp_node_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  step = wp_pipewire_object_mixin_activate_get_next_step (object, transition,
      step, missing);

  /* extend the mixin's state machine; when the only remaining feature to
     enable is FEATURE_PORTS, advance to STEP_PORTS */
  if (step == WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO &&
      missing == WP_NODE_FEATURE_PORTS)
    return STEP_PORTS;

  return step;
}

static void
wp_node_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO:
    wp_pipewire_object_mixin_cache_info (object, transition);
    break;
  case STEP_PORTS:
    wp_node_enable_feature_ports (WP_NODE (object));
    break;
  default:
    WP_OBJECT_CLASS (wp_node_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static void
wp_node_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpNode *self = WP_NODE (object);

  wp_pipewire_object_mixin_deactivate (object, features);

  if (features & WP_NODE_FEATURE_PORTS) {
    g_clear_object (&self->ports_om);
    wp_object_update_features (object, 0, WP_NODE_FEATURE_PORTS);
  }

  WP_OBJECT_CLASS (wp_node_parent_class)->deactivate (object, features);
}

static void
node_event_info(void *data, const struct pw_node_info *info)
{
  WpNode *self = WP_NODE (data);
  enum pw_node_state old_state = self->info ?
      self->info->state : PW_NODE_STATE_CREATING;

  self->info = pw_node_info_update (self->info, info);
  wp_object_update_features (WP_OBJECT (self),
      WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  if (info->change_mask & PW_NODE_CHANGE_MASK_STATE)
    g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, old_state,
        self->info->state);

  wp_pipewire_object_mixin_handle_event_info (self, info,
      PW_NODE_CHANGE_MASK_PROPS, PW_NODE_CHANGE_MASK_PARAMS);
}

static const struct pw_node_events node_events = {
  PW_VERSION_NODE_EVENTS,
  .info = node_event_info,
  .param = wp_pipewire_object_mixin_handle_event_param,
};

static void
wp_node_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpNode *self = WP_NODE (proxy);
  pw_node_add_listener ((struct pw_port *) pw_proxy,
      &self->listener, &node_events, self);
}

static void
wp_node_pw_proxy_destroyed (WpProxy * proxy)
{
  WpNode *self = WP_NODE (proxy);

  g_clear_pointer (&self->info, pw_node_info_free);
  g_clear_object (&self->ports_om);
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_PIPEWIRE_OBJECT_FEATURE_INFO | WP_NODE_FEATURE_PORTS);

  wp_pipewire_object_mixin_deactivate (WP_OBJECT (self),
      WP_OBJECT_FEATURES_ALL);
}

static void
wp_node_class_init (WpNodeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_pipewire_object_mixin_get_property;

  wpobject_class->get_supported_features = wp_node_get_supported_features;
  wpobject_class->activate_get_next_step = wp_node_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_node_activate_execute_step;
  wpobject_class->deactivate = wp_node_deactivate;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Node;
  proxy_class->pw_iface_version = PW_VERSION_NODE;
  proxy_class->pw_proxy_created = wp_node_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_node_pw_proxy_destroyed;

  wp_pipewire_object_mixin_class_override_properties (object_class);

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

static gconstpointer
wp_node_get_native_info (WpPipewireObject * obj)
{
  return WP_NODE (obj)->info;
}

static WpProperties *
wp_node_get_properties (WpPipewireObject * obj)
{
  return wp_properties_new_wrap_dict (WP_NODE (obj)->info->props);
}

static GVariant *
wp_node_get_param_info (WpPipewireObject * obj)
{
  WpNode *self = WP_NODE (obj);
  return wp_pipewire_object_mixin_param_info_to_gvariant (self->info->params,
      self->info->n_params);
}

static void
wp_node_enum_params (WpPipewireObject * obj, const gchar * id,
    WpSpaPod *filter, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  wp_pipewire_object_mixin_enum_params (pw_node, obj, id, filter, cancellable,
      callback, user_data);
}

static void
wp_node_set_param (WpPipewireObject * obj, const gchar * id, WpSpaPod * param)
{
  wp_pipewire_object_mixin_set_param (pw_node, obj, id, param);
}

static void
wp_node_pipewire_object_interface_init (WpPipewireObjectInterface * iface)
{
  iface->get_native_info = wp_node_get_native_info;
  iface->get_properties = wp_node_get_properties;
  iface->get_param_info = wp_node_get_param_info;
  iface->enum_params = wp_node_enum_params;
  iface->enum_params_finish = wp_pipewire_object_mixin_enum_params_finish;
  iface->enum_cached_params = wp_pipewire_object_mixin_enum_cached_params;
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

  if (error)
    *error = self->info->error;
  return (WpNodeState) self->info->state;
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

  if (max)
    *max = self->info->max_input_ports;
  return self->info->n_input_ports;
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

  if (max)
    *max = self->info->max_output_ports;
  return self->info->n_output_ports;
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
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_NODE_FEATURE_PORTS, NULL);

  return wp_object_manager_iterate (self->ports_om);
}

/**
 * wp_node_iterate_ports_filtered:
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
wp_node_iterate_ports_filtered (WpNode * self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_PORT, &args);
  va_end (args);
  return wp_node_iterate_ports_filtered_full (self, interest);
}

/**
 * wp_node_iterate_ports_filtered_full: (rename-to wp_node_iterate_ports_filtered)
 * @self: the node
 * @interest: (transfer full): the interest
 *
 * Requires %WP_NODE_FEATURE_PORTS
 *
 * Returns: (transfer full): a #WpIterator that iterates over all
 *   the ports that belong to this node and match the @interest
 */
WpIterator *
wp_node_iterate_ports_filtered_full (WpNode * self, WpObjectInterest * interest)
{
  g_return_val_if_fail (WP_IS_NODE (self), NULL);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_NODE_FEATURE_PORTS, NULL);

  return wp_object_manager_iterate_filtered_full (self->ports_om, interest);
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
void wp_node_send_command (WpNode * self, WpNodeCommand command)
{
  struct pw_node *pwp;
  struct spa_command cmd =
      SPA_NODE_COMMAND_INIT((enum spa_node_command) command);

  g_return_if_fail (WP_IS_NODE (self));

  pwp = (struct pw_node *) wp_proxy_get_pw_proxy (WP_PROXY (self));
  pw_node_send_command (pwp, &cmd);
}


enum {
  PROP_0,
  PROP_CORE,
  PROP_PW_IMPL_NODE,
};

struct _WpImplNode
{
  GObject parent;
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
 * by requesting %WP_PROXY_FEATURE_BOUND and be used as if it was a #WpNode
 * proxy to a remote object.
 */
G_DEFINE_TYPE (WpImplNode, wp_impl_node, G_TYPE_OBJECT)

static void
wp_impl_node_init (WpImplNode * self)
{
  g_weak_ref_init (&self->core, NULL);
}

static void
wp_impl_node_finalize (GObject * object)
{
  WpImplNode *self = WP_IMPL_NODE (object);

  g_clear_pointer (&self->proxy, pw_proxy_destroy);
  g_clear_pointer (&self->pw_impl_node, pw_impl_node_destroy);
  g_weak_ref_clear (&self->core);

  G_OBJECT_CLASS (wp_impl_node_parent_class)->finalize (object);
}

static void
wp_impl_node_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpImplNode *self = WP_IMPL_NODE (object);

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&self->core, g_value_get_object (value));
    break;
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
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&self->core));
    break;
  case PROP_PW_IMPL_NODE:
    g_value_set_pointer (value, self->pw_impl_node);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_impl_node_class_init (WpImplNodeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_impl_node_finalize;
  object_class->set_property = wp_impl_node_set_property;
  object_class->get_property = wp_impl_node_get_property;

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The WpCore", WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

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

/**
 * wp_impl_node_export:
 */
void
wp_impl_node_export (WpImplNode * self)
{
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  struct pw_core *pw_core = wp_core_get_pw_core (core);

  g_return_if_fail (pw_core);

  self->proxy = pw_core_export (pw_core,
      PW_TYPE_INTERFACE_Node, NULL, self->pw_impl_node, 0);
}
