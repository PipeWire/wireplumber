/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager/keys.h>

#define SI_FACTORY_NAME "si-monitor"

struct _WpSiMonitor
{
  WpSessionItem parent;

  /* configuration */
  WpSessionItem *endpoint;
  WpSession *session;
  gchar name[96];

  /* export */
  WpImplEndpoint *impl_endpoint;
};

static void si_monitor_endpoint_init (WpSiEndpointInterface * iface);
static void si_monitor_port_info_init (WpSiPortInfoInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiMonitor, si_monitor, WP, SI_MONITOR, WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiMonitor, si_monitor, WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ENDPOINT, si_monitor_endpoint_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_PORT_INFO, si_monitor_port_info_init))

static void
si_monitor_init (WpSiMonitor * self)
{
}

static void
si_monitor_reset (WpSessionItem * item)
{
  WpSiMonitor *self = WP_SI_MONITOR (item);

  /* deactivate first */
  wp_object_deactivate (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED);

  /* reset */
  g_clear_object (&self->endpoint);
  g_clear_object (&self->session);
  self->name[0] = '\0';

  WP_SESSION_ITEM_CLASS (si_monitor_parent_class)->reset (item);
}

static gboolean
si_monitor_configure (WpSessionItem * item, WpProperties * p)
{
  WpSiMonitor *self = WP_SI_MONITOR (item);
  g_autoptr (WpProperties) si_props = wp_properties_ensure_unique_owner (p);
  WpSessionItem *ep;
  WpProperties *ep_props = NULL;
  WpSession *session = NULL;
  const gchar *str;
  WpDirection direction;

  /* reset previous config */
  si_monitor_reset (item);

  str = wp_properties_get (si_props, "endpoint");
  if (!str || sscanf(str, "%p", &ep) != 1 || !WP_IS_SESSION_ITEM (ep))
    return FALSE;

  ep_props = wp_session_item_get_properties (ep);

  str = wp_properties_get (si_props, "name");
  if (!str) {
    str = wp_properties_get (ep_props, "name");
    if (!str)
      str = "Unknown";
    wp_properties_set (si_props, "name", str);
  }
  strncpy (self->name, str, sizeof (self->name) - 1);

  str = wp_properties_get (ep_props, "direction");
  if (!str || sscanf(str, "%u", &direction) != 1)
    return FALSE;
  if (direction != WP_DIRECTION_INPUT) {
    wp_warning_object (self, "only input endpoints are valid when configuring");
    return FALSE;
  }

  /* session is optional (only needed if we want to export) */
  str = wp_properties_get (si_props, "session");
  if (str && (sscanf(str, "%p", &session) != 1 || !WP_IS_SESSION (session)))
    return FALSE;
  if (!str)
    wp_properties_setf (si_props, "session", "%p", session);

  self->endpoint = g_object_ref (ep);
  if (session)
    self->session = g_object_ref (session);

  wp_properties_set (si_props, "si-factory-name", SI_FACTORY_NAME);
  wp_session_item_set_properties (WP_SESSION_ITEM (self),
      g_steal_pointer (&si_props));
  return TRUE;
}

static gpointer
si_monitor_get_associated_proxy (WpSessionItem * item,
    GType proxy_type)
{
  WpSiMonitor *self = WP_SI_MONITOR (item);

  if (proxy_type == WP_TYPE_NODE)
    return self->endpoint ? g_object_ref (self->endpoint) : NULL;
  if (proxy_type == WP_TYPE_SESSION)
    return self->session ? g_object_ref (self->session) : NULL;
  else if (proxy_type == WP_TYPE_ENDPOINT)
    return self->impl_endpoint ? g_object_ref (self->impl_endpoint) : NULL;

  return NULL;
}

static void
si_monitor_disable_active (WpSessionItem *si)
{
  WpSiMonitor *self = WP_SI_MONITOR (si);

  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
}

static void
si_monitor_disable_exported (WpSessionItem *si)
{
  WpSiMonitor *self = WP_SI_MONITOR (si);

  g_clear_object (&self->impl_endpoint);
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_EXPORTED);
}

static void
si_monitor_enable_active (WpSessionItem *si, WpTransition *transition)
{
  WpSiMonitor *self = WP_SI_MONITOR (si);

  if (!wp_session_item_is_configured (si)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "si-monitor: item is not configured"));
    return;
  }

  if (!(wp_object_get_active_features (WP_OBJECT (self->endpoint)) &
      WP_SESSION_ITEM_FEATURE_ACTIVE)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "si-monitor: endpoint is not activated"));
    return;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
}

static void
on_impl_endpoint_activated (WpObject * object, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiMonitor *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (object, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_EXPORTED, 0);
}

static void
si_monitor_enable_exported (WpSessionItem *si, WpTransition *transition)
{
  WpSiMonitor *self = WP_SI_MONITOR (si);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  self->impl_endpoint = wp_impl_endpoint_new (core, WP_SI_ENDPOINT (self));

  g_signal_connect_object (self->impl_endpoint, "pw-proxy-destroyed",
      G_CALLBACK (wp_session_item_handle_proxy_destroyed), self, 0);

  wp_object_activate (WP_OBJECT (self->impl_endpoint),
      WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_impl_endpoint_activated, transition);
}

static GVariant *
si_monitor_get_registration_info (WpSiEndpoint * item)
{
  WpSiMonitor *self = WP_SI_MONITOR (item);
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(ssya{ss})"));
  g_variant_builder_add (&b, "s", self->name);
  g_variant_builder_add (&b, "s", "Audio/Source");
  g_variant_builder_add (&b, "y", WP_DIRECTION_OUTPUT);
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpProperties *
si_monitor_get_properties (WpSiEndpoint * item)
{
  WpSiMonitor *self = WP_SI_MONITOR (item);
  WpProperties *properties;
  g_autofree gchar *description = NULL;
  guint32 endpoint_id = 0;

  properties = wp_si_endpoint_get_properties (WP_SI_ENDPOINT (self->endpoint));
  description = g_strdup_printf ("Monitor of %s",
      wp_properties_get (properties, "endpoint.description"));
  wp_properties_set (properties, "endpoint.description", description);

  endpoint_id = wp_session_item_get_associated_proxy_id (self->endpoint,
      WP_TYPE_ENDPOINT);
  if (endpoint_id)
    wp_properties_setf (properties, PW_KEY_ENDPOINT_MONITOR, "%u", endpoint_id);

  return properties;
}

static void
si_monitor_endpoint_init (WpSiEndpointInterface * iface)
{
  iface->get_registration_info = si_monitor_get_registration_info;
  iface->get_properties = si_monitor_get_properties;
}

static GVariant *
si_monitor_get_ports (WpSiPortInfo * item, const gchar * context)
{
  WpSiMonitor *self = WP_SI_MONITOR (item);

  return wp_si_port_info_get_ports (WP_SI_PORT_INFO (self->endpoint),
      "monitor");
}

static void
si_monitor_port_info_init (WpSiPortInfoInterface * iface)
{
  iface->get_ports = si_monitor_get_ports;
}

static void
si_monitor_class_init (WpSiMonitorClass * klass)
{
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  si_class->reset = si_monitor_reset;
  si_class->configure = si_monitor_configure;
  si_class->get_associated_proxy = si_monitor_get_associated_proxy;
  si_class->disable_active = si_monitor_disable_active;
  si_class->disable_exported = si_monitor_disable_exported;
  si_class->enable_active = si_monitor_enable_active;
  si_class->enable_exported = si_monitor_enable_exported;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_si_factory_register (core, wp_si_factory_new_simple (SI_FACTORY_NAME,
      si_monitor_get_type ()));
  return TRUE;
}
