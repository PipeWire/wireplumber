/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include <pipewire/pipewire.h>

#include "limited-creation.h"

typedef struct _WpLimitedCreationPrivate WpLimitedCreationPrivate;
struct _WpLimitedCreationPrivate
{
  /* properties */
  GWeakRef device;

  WpObjectManager *sessions_om;
  WpObjectManager *nodes_om;
};

enum {
  PROP_0,
  PROP_DEVICE,
};

enum {
  SIGNAL_ENDPOINT_CREATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE_WITH_PRIVATE (WpLimitedCreation, wp_limited_creation,
    G_TYPE_OBJECT)

static void
on_nodes_changed (WpObjectManager *om, gpointer d)
{
  WpLimitedCreation *self = WP_LIMITED_CREATION (d);

  if (WP_LIMITED_CREATION_GET_CLASS (self)->nodes_changed)
    WP_LIMITED_CREATION_GET_CLASS (self)->nodes_changed (self);
}

static void
on_node_added (WpObjectManager *om, WpProxy *node, gpointer d)
{
  WpLimitedCreation *self = WP_LIMITED_CREATION (d);

  if (WP_LIMITED_CREATION_GET_CLASS (self)->node_added)
    WP_LIMITED_CREATION_GET_CLASS (self)->node_added (self, WP_NODE (node));
}

static void
on_node_removed (WpObjectManager *om, WpProxy *node, gpointer d)
{
  WpLimitedCreation *self = WP_LIMITED_CREATION (d);

  if (WP_LIMITED_CREATION_GET_CLASS (self)->node_removed)
    WP_LIMITED_CREATION_GET_CLASS (self)->node_removed (self, WP_NODE (node));
}

static void
wp_limited_creation_init (WpLimitedCreation * self)
{
  WpLimitedCreationPrivate *priv =
      wp_limited_creation_get_instance_private (self);

  g_weak_ref_init (&priv->device, NULL);
}

static void
wp_limited_creation_constructed (GObject *object)
{
  WpLimitedCreation *self = WP_LIMITED_CREATION (object);
  WpLimitedCreationPrivate *priv =
      wp_limited_creation_get_instance_private (self);
  g_autoptr (WpDevice) device = g_weak_ref_get (&priv->device);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (device));

  g_return_if_fail (device);
  g_return_if_fail (core);

  /* Create the sessions object manager */
  priv->sessions_om = wp_object_manager_new ();
  wp_object_manager_add_interest (priv->sessions_om, WP_TYPE_SESSION, NULL);
  wp_object_manager_request_object_features (priv->sessions_om, WP_TYPE_SESSION,
      WP_OBJECT_FEATURES_ALL);
  wp_core_install_object_manager (core, priv->sessions_om);

  /* Create the nodes object manager */
  priv->nodes_om = wp_object_manager_new ();
  wp_object_manager_add_interest (priv->nodes_om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_DEVICE_ID, "=i",
      wp_proxy_get_bound_id (WP_PROXY (device)), NULL);
  wp_object_manager_request_object_features (priv->nodes_om, WP_TYPE_NODE,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  g_signal_connect_object (priv->nodes_om, "objects-changed",
      G_CALLBACK (on_nodes_changed), self, 0);
  g_signal_connect_object (priv->nodes_om, "object-added",
      G_CALLBACK (on_node_added), self, 0);
  g_signal_connect_object (priv->nodes_om, "object-removed",
      G_CALLBACK (on_node_removed), self, 0);
  wp_core_install_object_manager (core, priv->nodes_om);

  G_OBJECT_CLASS (wp_limited_creation_parent_class)->constructed (object);
}

static void
wp_limited_creation_finalize (GObject * object)
{
  WpLimitedCreation *self = WP_LIMITED_CREATION (object);
  WpLimitedCreationPrivate *priv =
      wp_limited_creation_get_instance_private (self);

  g_clear_object (&priv->nodes_om);
  g_clear_object (&priv->sessions_om);
  g_weak_ref_clear (&priv->device);

  G_OBJECT_CLASS (wp_limited_creation_parent_class)->finalize (object);
}

static void
wp_limited_creation_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpLimitedCreation *self = WP_LIMITED_CREATION (object);
  WpLimitedCreationPrivate *priv =
      wp_limited_creation_get_instance_private (self);

  switch (property_id) {
  case PROP_DEVICE:
    g_weak_ref_set (&priv->device, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_limited_creation_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpLimitedCreation *self = WP_LIMITED_CREATION (object);
  WpLimitedCreationPrivate *priv =
      wp_limited_creation_get_instance_private (self);

  switch (property_id) {
  case PROP_DEVICE:
    g_value_take_object (value, g_weak_ref_get (&priv->device));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_limited_creation_class_init (WpLimitedCreationClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_limited_creation_constructed;
  object_class->finalize = wp_limited_creation_finalize;
  object_class->set_property = wp_limited_creation_set_property;
  object_class->get_property = wp_limited_creation_get_property;

  /* properties */
  g_object_class_install_property (object_class, PROP_DEVICE,
      g_param_spec_object ("device", "device",
          "The associated device", WP_TYPE_DEVICE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  /* Signals */
  signals[SIGNAL_ENDPOINT_CREATED] = g_signal_new ("endpoint-created",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_SESSION_ITEM);

}

WpDevice *
wp_limited_creation_get_device (WpLimitedCreation * self)
{
  WpLimitedCreationPrivate *priv;

  g_return_val_if_fail (WP_IS_LIMITED_CREATION (self), NULL);

  priv = wp_limited_creation_get_instance_private (self);

  return g_weak_ref_get (&priv->device);
}

WpNode *
wp_limited_creation_lookup_node (WpLimitedCreation *self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_NODE, &args);
  va_end (args);
  return wp_limited_creation_lookup_node_full (self, interest);
}

WpNode *
wp_limited_creation_lookup_node_full (WpLimitedCreation *self,
    WpObjectInterest * interest)
{
  WpLimitedCreationPrivate *priv;
  g_return_val_if_fail (WP_IS_LIMITED_CREATION (self), NULL);
  priv = wp_limited_creation_get_instance_private (self);
  return wp_object_manager_lookup_full (priv->nodes_om, interest);
}

WpSession *
wp_limited_creation_lookup_session (WpLimitedCreation *self, ...)
{
  WpObjectInterest *interest;
  va_list args;
  va_start (args, self);
  interest = wp_object_interest_new_valist (WP_TYPE_SESSION, &args);
  va_end (args);
  return wp_limited_creation_lookup_session_full (self, interest);
}

WpSession *
wp_limited_creation_lookup_session_full (WpLimitedCreation *self,
    WpObjectInterest * interest)
{
  WpLimitedCreationPrivate *priv;
  g_return_val_if_fail (WP_IS_LIMITED_CREATION (self), NULL);
  priv = wp_limited_creation_get_instance_private (self);
  return wp_object_manager_lookup_full (priv->sessions_om, interest);
}

void
wp_endpoint_creation_notify_endpoint_created (WpLimitedCreation * self,
    WpSessionItem *ep)
{
  g_signal_emit (self, signals[SIGNAL_ENDPOINT_CREATED], 0, ep);
}
