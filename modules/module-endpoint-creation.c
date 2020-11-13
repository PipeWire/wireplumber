/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include <pipewire/pipewire.h>

#include "module-endpoint-creation/generic-creation.h"
#include "module-endpoint-creation/limited-creation.h"
#include "module-endpoint-creation/limited-creation-bluez5.h"

struct _WpEndpointCreation
{
  WpPlugin parent;

  WpObjectManager *nodes_om;
  WpObjectManager *devices_om;

  GHashTable *limited_creations;
  WpGenericCreation *generic_creation;
};

enum {
  SIGNAL_ENDPOINT_CREATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DECLARE_FINAL_TYPE (WpEndpointCreation, wp_endpoint_creation, WP,
    ENDPOINT_CREATION, WpPlugin)
G_DEFINE_TYPE (WpEndpointCreation, wp_endpoint_creation, WP_TYPE_PLUGIN)

static void
on_endpoint_created (WpLimitedCreation *li, WpSessionItem *ep,
    WpEndpointCreation * self)
{
  g_signal_emit (self, signals[SIGNAL_ENDPOINT_CREATED], 0, ep);
}

static WpLimitedCreation *
create_device_limited_creation (WpEndpointCreation *self, WpProxy *device)
{
  const gchar *device_api = wp_pipewire_object_get_property (
      WP_PIPEWIRE_OBJECT (device), PW_KEY_DEVICE_API);

  /* Bluez5 */
  if (g_strcmp0 (device_api, "bluez5") == 0)
    return wp_limited_creation_bluez5_new (WP_DEVICE (device));

  /* Create future device limited creations here if needed */

  return NULL;
}

static void
on_device_added (WpObjectManager *om, WpProxy *proxy, gpointer d)
{
  WpEndpointCreation *self = WP_ENDPOINT_CREATION (d);
  guint32 device_id = wp_proxy_get_bound_id (proxy);

  g_autoptr (WpLimitedCreation) li = NULL;

  /* Get the limited creation for the deivce */
  li = create_device_limited_creation (self, proxy);
  if (!li)
    return;

  /* Handle the endpoint created signal */
  g_signal_connect_object (li, "endpoint-created",
      G_CALLBACK (on_endpoint_created), self, 0);

  /* Add the limited creation to the table */
  g_hash_table_insert (self->limited_creations, GUINT_TO_POINTER (device_id),
      g_steal_pointer (&li));
}

static void
on_device_removed (WpObjectManager *om, WpProxy *proxy, gpointer d)
{
  WpEndpointCreation *self = WP_ENDPOINT_CREATION (d);
  guint32 device_id = wp_proxy_get_bound_id (proxy);

  g_hash_table_remove (self->limited_creations, GUINT_TO_POINTER (device_id));
}

static gboolean
has_node_limited_creation (WpEndpointCreation *self, WpProxy *node)
{
  const gchar *device_id = wp_pipewire_object_get_property (
      WP_PIPEWIRE_OBJECT (node), PW_KEY_DEVICE_ID);
  if (!device_id)
    return FALSE;

  return g_hash_table_contains (self->limited_creations,
      GUINT_TO_POINTER (atoi (device_id)));
}

static void
on_node_added (WpObjectManager *om, WpProxy *proxy, gpointer d)
{
  WpEndpointCreation *self = WP_ENDPOINT_CREATION (d);

  /* Only notify generic creation if node does not have a limited creation */
  if (!has_node_limited_creation (self, proxy))
    wp_generic_creation_add_node (self->generic_creation, WP_NODE (proxy));
}

static void
on_node_removed (WpObjectManager *om, WpProxy *proxy, gpointer d)
{
  WpEndpointCreation *self = WP_ENDPOINT_CREATION (d);

  /* Only notify generic creation if node does not have a limited creation */
  if (!has_node_limited_creation (self, proxy))
    wp_generic_creation_remove_node (self->generic_creation, WP_NODE (proxy));
}

static void
wp_endpoint_creation_activate (WpPlugin * plugin)
{
  WpEndpointCreation *self = WP_ENDPOINT_CREATION (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (WP_PLUGIN (self));

  self->limited_creations = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, g_object_unref);
  self->generic_creation = wp_generic_creation_new (core);
  g_signal_connect_object (self->generic_creation, "endpoint-created",
      G_CALLBACK (on_endpoint_created), self, 0);

  /* Install devices object manager */
  self->devices_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->devices_om, WP_TYPE_DEVICE, NULL);
  wp_object_manager_request_object_features (self->devices_om,
      WP_TYPE_DEVICE, WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  g_signal_connect_object (self->devices_om, "object-added",
      G_CALLBACK (on_device_added), self, 0);
  g_signal_connect_object (self->devices_om, "object-removed",
      G_CALLBACK (on_device_removed), self, 0);
  wp_core_install_object_manager (core, self->devices_om);

  /* Install nodes object manager */
  self->nodes_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->nodes_om, WP_TYPE_NODE, NULL);
  wp_object_manager_request_object_features (self->nodes_om, WP_TYPE_NODE,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  g_signal_connect_object (self->nodes_om, "object-added",
      G_CALLBACK (on_node_added), self, 0);
  g_signal_connect_object (self->nodes_om, "object-removed",
      G_CALLBACK (on_node_removed), self, 0);
  wp_core_install_object_manager (core, self->nodes_om);
}

static void
wp_endpoint_creation_deactivate (WpPlugin * plugin)
{
  WpEndpointCreation *self = WP_ENDPOINT_CREATION (plugin);

  g_clear_object (&self->nodes_om);
  g_clear_object (&self->devices_om);
  g_clear_pointer (&self->limited_creations, g_hash_table_unref);
  g_clear_object (&self->generic_creation);
}

static void
wp_endpoint_creation_init (WpEndpointCreation * self)
{
}

static void
wp_endpoint_creation_class_init (WpEndpointCreationClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->activate = wp_endpoint_creation_activate;
  plugin_class->deactivate = wp_endpoint_creation_deactivate;

  /* Signals */
  signals[SIGNAL_ENDPOINT_CREATED] = g_signal_new ("endpoint-created",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_SESSION_ITEM);
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  wp_plugin_register (g_object_new (wp_endpoint_creation_get_type (),
      "name", "endpoint-creation",
      "module", module,
      NULL));
}
