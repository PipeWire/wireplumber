/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include "parser-node.h"
#include "context.h"

struct _WpConfigStaticNodesContext
{
  GObject parent;

  /* Props */
  GWeakRef core;

  WpObjectManager *devices_om;
  GPtrArray *static_nodes;
};

enum {
  PROP_0,
  PROP_CORE,
};

enum {
  SIGNAL_NODE_CREATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (WpConfigStaticNodesContext, wp_config_static_nodes_context,
    G_TYPE_OBJECT)

static void
on_node_created (GObject * proxy, GAsyncResult * res, gpointer user_data)
{
  WpConfigStaticNodesContext *self = user_data;
  g_autoptr (GError) error = NULL;

  if (!wp_proxy_augment_finish (WP_PROXY (proxy), res, &error)) {
    g_warning ("WpConfigStaticNodesContext:%p: failed to export node: %s",
        self, error->message);
    return;
  }

  g_ptr_array_add (self->static_nodes, g_object_ref (proxy));

  /* Emit the node-created signal */
  g_signal_emit (self, signals[SIGNAL_NODE_CREATED], 0, proxy);
}

static void
wp_config_static_nodes_context_create_node (WpConfigStaticNodesContext *self,
  const struct WpParserNodeData *node_data)
{
  g_autoptr (WpProxy) node = NULL;
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_return_if_fail (core);

  /* Create the node */
  node = node_data->n.local ?
      (WpProxy *) wp_impl_node_new_from_pw_factory (core, node_data->n.factory,
          wp_properties_ref (node_data->n.props)) :
      (WpProxy *) wp_node_new_from_factory (core, node_data->n.factory,
          wp_properties_ref (node_data->n.props));
  if (!node) {
    g_warning ("WpConfigStaticNodesContext:%p: failed to create node", self);
    return;
  }

  /* export to pipewire by requesting FEATURE_BOUND */
  wp_proxy_augment (node, WP_PROXY_FEATURE_BOUND, NULL, on_node_created, self);
}

static void
on_device_added (WpObjectManager *om, WpProxy *proxy, gpointer p)
{
  WpConfigStaticNodesContext *self = p;
  g_autoptr (WpProperties) dev_props = wp_proxy_get_properties (proxy);
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  g_autoptr (WpConfigParser) parser = NULL;
  const struct WpParserNodeData *node_data = NULL;

  /* Get the parser node data and skip the node if not found */
  parser = wp_configuration_get_parser (config, WP_PARSER_NODE_EXTENSION);
  node_data = wp_config_parser_get_matched_data (parser, dev_props);
  if (!node_data)
    return;

  /* Create the node */
  wp_config_static_nodes_context_create_node (self, node_data);
}

static gboolean
parser_node_foreach_func (const struct WpParserNodeData *node_data,
    gpointer data)
{
  WpConfigStaticNodesContext *self = data;

  /* Only create nodes that don't have match-device info */
  if (!node_data->has_md) {
    wp_config_static_nodes_context_create_node (self, node_data);
    return TRUE;
  }

  return TRUE;
}

static void
start_static_nodes (WpConfigStaticNodesContext *self)
{
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  g_autoptr (WpConfigParser) parser =
      wp_configuration_get_parser (config, WP_PARSER_NODE_EXTENSION);

  /* Create static nodes without match-device */
  wp_parser_node_foreach (WP_PARSER_NODE (parser), parser_node_foreach_func,
      self);
}

static void
wp_config_static_nodes_context_constructed (GObject * object)
{
  WpConfigStaticNodesContext *self = WP_CONFIG_STATIC_NODES_CONTEXT (object);
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);

  /* Add the node parser and parse the node files */
  wp_configuration_add_extension (config, WP_PARSER_NODE_EXTENSION,
      WP_TYPE_PARSER_NODE);
  wp_configuration_reload (config, WP_PARSER_NODE_EXTENSION);

  /* Install the object manager */
  wp_core_install_object_manager (core, self->devices_om);

  /* Start creating static nodes when the connected callback is triggered */
  g_signal_connect_object (core, "connected", (GCallback) start_static_nodes,
      self, G_CONNECT_SWAPPED);

  G_OBJECT_CLASS (wp_config_static_nodes_context_parent_class)->constructed (object);
}

static void
wp_config_static_nodes_context_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpConfigStaticNodesContext *self = WP_CONFIG_STATIC_NODES_CONTEXT (object);

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&self->core, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_config_static_nodes_context_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpConfigStaticNodesContext *self = WP_CONFIG_STATIC_NODES_CONTEXT (object);

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&self->core));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_config_static_nodes_context_finalize (GObject *object)
{
  WpConfigStaticNodesContext *self = WP_CONFIG_STATIC_NODES_CONTEXT (object);

  g_clear_object (&self->devices_om);
  g_clear_pointer (&self->static_nodes, g_ptr_array_unref);

  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  if (core) {
    g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
    wp_configuration_remove_extension (config, WP_PARSER_NODE_EXTENSION);
  }
  g_weak_ref_clear (&self->core);

  G_OBJECT_CLASS (wp_config_static_nodes_context_parent_class)->finalize (object);
}

static void
wp_config_static_nodes_context_init (WpConfigStaticNodesContext *self)
{
  self->static_nodes = g_ptr_array_new_with_free_func (g_object_unref);
  self->devices_om = wp_object_manager_new ();

  /* Only handle devices */
  wp_object_manager_add_proxy_interest (self->devices_om,
      WP_TYPE_DEVICE, NULL, WP_PROXY_FEATURE_INFO);
  g_signal_connect (self->devices_om, "object-added",
      (GCallback) on_device_added, self);
}

static void
wp_config_static_nodes_context_class_init (WpConfigStaticNodesContextClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_config_static_nodes_context_constructed;
  object_class->finalize = wp_config_static_nodes_context_finalize;
  object_class->set_property = wp_config_static_nodes_context_set_property;
  object_class->get_property = wp_config_static_nodes_context_get_property;

  /* Properties */
  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The wireplumber core",
          WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  /* Signals */
  signals[SIGNAL_NODE_CREATED] = g_signal_new ("node-created",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_PROXY);
}

WpConfigStaticNodesContext *
wp_config_static_nodes_context_new (WpCore *core)
{
  return g_object_new (wp_config_static_nodes_context_get_type (),
    "core", core,
    NULL);
}
