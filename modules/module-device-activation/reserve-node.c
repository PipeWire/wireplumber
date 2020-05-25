/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "reserve-node.h"

struct _WpReserveNode
{
  GObject parent;

  /* Props */
  GWeakRef node;
  WpReserveDevice *device_data;

  gboolean acquired;
  GSource *timeout_source;
};

enum {
  NODE_PROP_0,
  NODE_PROP_NODE,
  NODE_PROP_DEVICE_DATA,
};

G_DEFINE_TYPE (WpReserveNode, wp_reserve_node, G_TYPE_OBJECT)

static void
on_node_destroyed (WpProxy *node, WpReserveNode *self)
{
  if (self->acquired)
    wp_reserve_device_release (self->device_data);
}

static void
wp_reserve_node_constructed (GObject * object)
{
  WpReserveNode *self = WP_RESERVE_NODE (object);
  WpProxy *node = g_weak_ref_get (&self->node);

  g_return_if_fail (node);

  /* Make sure the device is released when the pw proxy node is destroyed */
  g_signal_connect_object (node, "pw-proxy-destroyed",
      (GCallback) on_node_destroyed, self, 0);

  G_OBJECT_CLASS (wp_reserve_node_parent_class)->constructed (object);
}

static void
wp_reserve_node_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  WpReserveNode *self = WP_RESERVE_NODE (object);

  switch (property_id) {
  case NODE_PROP_NODE:
    g_value_take_object (value, g_weak_ref_get (&self->node));
    break;
  case NODE_PROP_DEVICE_DATA:
    g_value_set_object (value, self->device_data);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_reserve_node_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  WpReserveNode *self = WP_RESERVE_NODE (object);

  switch (property_id) {
  case NODE_PROP_NODE:
    g_weak_ref_set (&self->node, g_value_get_object (value));
    break;
  case NODE_PROP_DEVICE_DATA:
    self->device_data = g_value_dup_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_reserve_node_finalize (GObject * object)
{
  WpReserveNode *self = WP_RESERVE_NODE (object);

  /* Clear the current timeout release callback */
  if (self->timeout_source)
      g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  /* Release device if acquired */
  if (self->acquired)
    wp_reserve_device_release (self->device_data);

  /* Props */
  g_weak_ref_clear (&self->node);
  g_clear_object (&self->device_data);

  G_OBJECT_CLASS (wp_reserve_node_parent_class)->finalize (object);
}

static void
wp_reserve_node_init (WpReserveNode * self)
{
  /* Props */
  g_weak_ref_init (&self->node, NULL);

  self->acquired = FALSE;
  self->timeout_source = NULL;
}

static void
wp_reserve_node_class_init (WpReserveNodeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_reserve_node_constructed;
  object_class->get_property = wp_reserve_node_get_property;
  object_class->set_property = wp_reserve_node_set_property;
  object_class->finalize = wp_reserve_node_finalize;

  /* Props */
  g_object_class_install_property (object_class, NODE_PROP_NODE,
      g_param_spec_object ("node", "node", "The node", WP_TYPE_PROXY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, NODE_PROP_DEVICE_DATA,
      g_param_spec_object ("device-data", "device-data",
      "The monitor device reservation data", WP_TYPE_RESERVE_DEVICE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpReserveNode *
wp_reserve_node_new (WpProxy *node, WpReserveDevice *device_data)
{
  return g_object_new (WP_TYPE_RESERVE_NODE,
      "node", node,
      "device-data", device_data,
      NULL);
}

static gboolean
timeout_release_callback (gpointer data)
{
  WpReserveNode *self = data;
  g_return_val_if_fail (self, G_SOURCE_REMOVE);

  wp_reserve_device_release (self->device_data);
  self->acquired = FALSE;
  return G_SOURCE_REMOVE;
}

void
wp_reserve_node_timeout_release (WpReserveNode *self, guint64 timeout_ms)
{
  g_autoptr (WpProxy) node = NULL;
  g_autoptr (WpCore) core = NULL;
  g_return_if_fail (WP_IS_RESERVE_NODE (self));

  node = g_weak_ref_get (&self->node);
  g_return_if_fail (node);
  core = wp_proxy_get_core (node);
  g_return_if_fail (core);

  /* Clear the current timeout release callback */
  if (self->timeout_source)
      g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  /* Add new timeout release callback */
  wp_core_timeout_add (core, &self->timeout_source, timeout_ms,
        timeout_release_callback, g_object_ref (self), g_object_unref);
}

void
wp_reserve_node_acquire (WpReserveNode *self)
{
  g_return_if_fail (WP_IS_RESERVE_NODE (self));

  /* Clear the current timeout release callback */
  if (self->timeout_source)
      g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  /* Don't do anything if already acquired */
  if (self->acquired)
    return;

  /* Acquire the device */
  wp_reserve_device_acquire (self->device_data);
  self->acquired = TRUE;
}
