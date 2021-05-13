/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/*!
 * @file node.c
 */
#define G_LOG_DOMAIN "wp-node"

#include "node.h"
#include "core.h"
#include "object-manager.h"
#include "log.h"
#include "wpenums.h"
#include "private/pipewire-object-mixin.h"

#include <pipewire/impl.h>

/*!
 * @memberof WpNode
 *
 * @signal @b ports-changed
 *
 * @code
 * ports_changed_callback (WpNode * self,
 *                         gpointer user_data)
 * @endcode
 *
 * Emitted when the node's ports change. This is only emitted when %WP_NODE_FEATURE_PORTS is enabled.
 *
 * @b Parameters:
 *
 * @arg `self` - the node
 * @arg `user_data`
 *
 * Flags: Run Last
 *
 * @signal @b state-changed
 *
 * @code
 * state_changed_callback (WpNode * self,
 *                         WpNodeState * old_state,
 *                         WpNodeState * new_state,
 *                         gpointer user_data)
 * @endcode
 *
 * Emitted when the node changes state. This is only emitted when
 * %WP_PIPEWIRE_OBJECT_FEATURE_INFO is enabled.
 *
 * @b Parameters:
 *
 * @arg `self` - the node
 * @arg `old_state` - the old state
 * @arg `new_state` - the new state
 * @arg `user_data`
 *
 * Flags: Run Last
 *
 */
enum {
  SIGNAL_STATE_CHANGED,
  SIGNAL_PORTS_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

/*!
 * 
 * @struct WpNode
 * @section node_section Node
 *
 * @brief The [WpNode](@ref node_section) class allows accessing the properties and methods of a
 * PipeWire node object (`struct pw_node`).
 *
 * A [WpNode](@ref node_section) is constructed internally when a new node appears on the
 * PipeWire registry and it is made available through the [WpObjectManager](@ref object_manager_section) API.
 * Alternatively, a [WpNode](@ref node_section) can also be constructed using
 * wp_node_new_from_factory(), which creates a new node object
 * on the remote PipeWire server by calling into a factory.
 *
 */
/*!
 * @brief
 * @em parent
 * @em ports_om
 */
struct _WpNode
{
  WpGlobalProxy parent;
  WpObjectManager *ports_om;
};

static void wp_node_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

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

  WP_PROXY_CLASS (wp_node_parent_class)->pw_proxy_destroyed (proxy);
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

  /*
   * WpNode::state-changed:
   * @self: the node
   * @old_state: the old state
   * @new_state: the new state
   *
   * @brief Emitted when the node changes state. This is only emitted
   * when %WP_PIPEWIRE_OBJECT_FEATURE_INFO is enabled.
   */
  signals[SIGNAL_STATE_CHANGED] = g_signal_new (
      "state-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
      WP_TYPE_NODE_STATE, WP_TYPE_NODE_STATE);

  /*
   * WpNode::ports-changed:
   *
   * @brief Emitted when the node's ports change. This is only emitted
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

/*!
 * @memberof WpNode
 * @param core: the wireplumber core
 * @param factory_name: the pipewire factory name to construct the node
 * @param properties: (nullable) (transfer full): the properties to pass to the factory
 *
 * @brief Constructs a node on the PipeWire server by asking the remote factory
 * @em param factory_name to create it.
 *
 * Because of the nature of the PipeWire protocol, this operation completes
 * asynchronously at some point in the future. In order to find out when
 * this is done, you should call wp_object_activate(), requesting at least
 * %WP_PROXY_FEATURE_BOUND. When this feature is ready, the node is ready for
 * use on the server. If the node cannot be created, this activation operation
 * will fail.
 *
 * @returns (nullable) (transfer full): the new node or %NULL if the core
 *   is not connected and therefore the node cannot be created
 */

WpNode *
wp_node_new_from_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties)
{
  g_autoptr (WpProperties) props = properties;
  return g_object_new (WP_TYPE_NODE,
      "core", core,
      "factory-name", factory_name,
      "global-properties", props,
      NULL);
}

/*!
 * @memberof WpNode
 * @param self: the node
 * @param error: the error
 *
 * @returns the current state of the node
 */

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

/*!
 * @memberof WpNode
 * @param self: the node
 * @param max: (out) (optional): the maximum supported number of input ports
 *
 * @brief Requires %WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * @returns the number of input ports of this node, as reported by the node info
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

/*!
 * @memberof WpNode
 * @param self: the node
 * @param max: (out) (optional): the maximum supported number of output ports
 *
 * @brief Requires %WP_PIPEWIRE_OBJECT_FEATURE_INFO
 *
 * @returns the number of output ports of this node, as reported by the node info
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

/*!
 * @memberof WpNode
 * @param self: the node
 *
 * @brief Requires %WP_NODE_FEATURE_PORTS
 *
 * @returns the number of ports of this node. Note that this number may not
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

/*!
 * @memberof WpNode
 * @param self: the node
 *
 * @brief Requires %WP_NODE_FEATURE_PORTS
 *
 * @returns (transfer full): a [WpIterator](@ref iterator_section) that iterates over all
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

/*!
 * @memberof WpNode
 * @param self: the node
 * @...: a list of constraints, terminated by %NULL
 *
 * @brief Requires %WP_NODE_FEATURE_PORTS
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * @returns (transfer full): a [WpIterator](@ref iterator_section) that iterates over all
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

/*!
 * @memberof WpNode
 * @param self: the node
 * @param interest: (transfer full): the interest
 *
 * @brief Requires %WP_NODE_FEATURE_PORTS
 *
 * @returns (transfer full): a [WpIterator](@ref iterator_section) that iterates over all
 *   the ports that belong to this node and match the @em interest
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

/*!
 * @memberof WpNode
 * @param self: the node
 * @...: a list of constraints, terminated by %NULL
 *
 * @brief Requires %WP_NODE_FEATURE_PORTS
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * @returns (transfer full) (nullable): the first port that matches the
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

/*!
 * @memberof WpNode
 * @param self: the node
 * @param interest: (transfer full): the interest
 *
 * @brief Requires %WP_NODE_FEATURE_PORTS
 *
 * @returns (transfer full) (nullable): the first port that matches the
 *    @em interest, or %NULL if there is no such port
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

/*!
 * @memberof WpNode
 * @param self: the node
 * @param command: the command
 *
 * returns Void
 *
 * @brief Sends a command to a node
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

/*!
 * @memberof WpImplNode
 *
 * @props @b pw-impl-node
 *
 * @code
 * "pw-impl-node" gpointer
 * @endcode
 *
 * Flags : Read / Write / Construct Only
 */
enum {
  PROP_PW_IMPL_NODE = WP_PW_OBJECT_MIXIN_PROP_CUSTOM_START,
};

/*!
 * @struct WpImplNode:
 * @memberof WpNode
 * @section impl_node_section WpImplNode
 *
 * @brief A [WpImplNode](@ref impl_node_section) allows running a node
 * implementation (`struct pw_impl_node`) locally,
 * loading the implementation from factory or wrapping a manually
 * constructed `pw_impl_node`. This object can then be exported to PipeWire
 * by requesting %WP_PROXY_FEATURE_BOUND.
 */
struct _WpImplNode
{
  WpProxy parent;
  GWeakRef core;
  struct pw_impl_node *pw_impl_node;
};

static void wp_impl_node_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpImplNode, wp_impl_node, WP_TYPE_PROXY,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT,
        wp_pw_object_mixin_object_interface_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_impl_node_pw_object_mixin_priv_interface_init))

static void
wp_impl_node_init (WpImplNode * self)
{
}

static void
wp_impl_node_constructed (GObject * object)
{
  WpImplNode * self = WP_IMPL_NODE (object);
  WpPwObjectMixinData * data = wp_pw_object_mixin_get_data (self);

  data->info = (gpointer) pw_impl_node_get_info (self->pw_impl_node);
  data->iface = pw_impl_node_get_implementation (self->pw_impl_node);

  /* TODO handle the actual node properties */
  data->properties = wp_properties_new_empty();

  WpObjectFeatures ft =
      wp_pw_object_mixin_get_supported_features (WP_OBJECT (self))
      & ~WP_PROXY_FEATURE_BOUND;
  wp_object_update_features (WP_OBJECT (self), ft, 0);

  G_OBJECT_CLASS (wp_impl_node_parent_class)->constructed (object);
}

static void
wp_impl_node_dispose (GObject * object)
{
  WpImplNode *self = WP_IMPL_NODE (object);

  WpObjectFeatures ft =
      wp_pw_object_mixin_get_supported_features (WP_OBJECT (self))
      & ~WP_PROXY_FEATURE_BOUND;
  wp_object_update_features (WP_OBJECT (self), 0, ft);

  G_OBJECT_CLASS (wp_impl_node_parent_class)->dispose (object);
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
    wp_pw_object_mixin_get_property (object, property_id, value, pspec);
    break;
  }
}

enum {
  STEP_EXPORT = WP_TRANSITION_STEP_CUSTOM_START,
};

static guint
wp_impl_node_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  /* BOUND is the only feature that can be in @em missing */
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

    wp_proxy_watch_bind_error (WP_PROXY (self), WP_TRANSITION (transition));
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

  object_class->constructed = wp_impl_node_constructed;
  object_class->dispose = wp_impl_node_dispose;
  object_class->finalize = wp_impl_node_finalize;
  object_class->set_property = wp_impl_node_set_property;
  object_class->get_property = wp_impl_node_get_property;

  wpobject_class->get_supported_features =
      wp_pw_object_mixin_get_supported_features;
  wpobject_class->activate_get_next_step = wp_impl_node_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_impl_node_activate_execute_step;

  wp_pw_object_mixin_class_override_properties (object_class);

  g_object_class_install_property (object_class, PROP_PW_IMPL_NODE,
      g_param_spec_pointer ("pw-impl-node", "pw-impl-node",
          "The actual node implementation, struct pw_impl_node *",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static int
impl_node_collect_params (void *data, int seq,
    uint32_t id, uint32_t index, uint32_t next, struct spa_pod *param)
{
  GPtrArray *result = data;
  g_ptr_array_add (result, wp_spa_pod_new_wrap_const (param));
  return 0;
}

static GPtrArray *
wp_impl_node_enum_params_sync (gpointer instance, guint32 id,
      guint32 start, guint32 num, WpSpaPod *filter)
{
  WpImplNode *self = WP_IMPL_NODE (instance);
  GPtrArray *result =
      g_ptr_array_new_with_free_func ((GDestroyNotify) wp_spa_pod_unref);

  pw_impl_node_for_each_param (self->pw_impl_node, 1, id, start, num,
      filter ? wp_spa_pod_get_spa_pod (filter) : NULL,
      impl_node_collect_params, result);
  return result;
}

static gint
wp_impl_node_set_param (gpointer instance, guint32 id, guint32 flags,
    WpSpaPod * param)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  return spa_node_set_param (d->iface, id, flags,
      wp_spa_pod_get_spa_pod (param));
}

static void
wp_impl_node_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  wp_pw_object_mixin_priv_interface_info_init (iface, node, NODE);
  iface->flags = WP_PW_OBJECT_MIXIN_PRIV_NO_PARAM_CACHE;
  iface->enum_params_sync = wp_impl_node_enum_params_sync;
  iface->set_param = wp_impl_node_set_param;
}

/*!
 * @memberof WpNode
 * @param core: the wireplumber core
 * @param node: an existing pw_impl_node to wrap
 *
 * @returns (transfer full): A new [WpImplNode](@ref impl_node_section) wrapping @em node
 */

WpImplNode *
wp_impl_node_new_wrap (WpCore * core, struct pw_impl_node * node)
{
  return g_object_new (WP_TYPE_IMPL_NODE,
      "core", core,
      "pw-impl-node", node,
      NULL);
}

/*!
 * @memberof WpNode
 * @param core: the wireplumber core
 * @param factory_name: the name of the pipewire factory
 * @param properties: (nullable) (transfer full): properties to be passed to node
 *    constructor
 *
 * @brief Constructs a new node, locally on this process, using the specified
 * @em factory_name.
 *
 * To export this node to the PipeWire server, you need to call
 * wp_object_activate() requesting %WP_PROXY_FEATURE_BOUND and
 * wait for the operation to complete.
 *
 * @returns (nullable) (transfer full): A new [WpImplNode](@ref impl_node_section) wrapping the
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