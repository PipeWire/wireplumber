/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/keys.h>
#include <pipewire/extensions/session-manager/keys.h>

enum {
  STEP_VERIFY_CONFIG = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_ENSURE_ADAPTER_ACTIVATED,
};

struct _WpSiMonitorEndpoint
{
  WpSessionItem parent;

  /* configuration */
  WpSessionItem *adapter;
  gchar name[96];
};

static void si_monitor_endpoint_endpoint_init (WpSiEndpointInterface * iface);
static void si_monitor_endpoint_stream_init (WpSiStreamInterface * iface);
static void si_monitor_endpoint_port_info_init (WpSiPortInfoInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiMonitorEndpoint, si_monitor_endpoint,
                     WP, SI_MONITOR_ENDPOINT, WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiMonitorEndpoint, si_monitor_endpoint, WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ENDPOINT, si_monitor_endpoint_endpoint_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_STREAM, si_monitor_endpoint_stream_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_PORT_INFO, si_monitor_endpoint_port_info_init))

static void
si_monitor_endpoint_init (WpSiMonitorEndpoint * self)
{
}

static void
si_monitor_endpoint_reset (WpSessionItem * item)
{
  WpSiMonitorEndpoint *self = WP_SI_MONITOR_ENDPOINT (item);

  /* unexport & deactivate first */
  WP_SESSION_ITEM_CLASS (si_monitor_endpoint_parent_class)->reset (item);

  g_clear_object (&self->adapter);
  self->name[0] = '\0';

  wp_session_item_clear_flag (item, WP_SI_FLAG_CONFIGURED);
}

static gpointer
si_monitor_endpoint_get_associated_proxy (WpSessionItem * item,
    GType proxy_type)
{
  WpSiMonitorEndpoint *self = WP_SI_MONITOR_ENDPOINT (item);

  if (proxy_type == WP_TYPE_NODE)
    return wp_session_item_get_associated_proxy (self->adapter, proxy_type);

  return WP_SESSION_ITEM_CLASS (si_monitor_endpoint_parent_class)->
      get_associated_proxy (item, proxy_type);
}

static GVariant *
si_monitor_endpoint_get_configuration (WpSessionItem * item)
{
  WpSiMonitorEndpoint *self = WP_SI_MONITOR_ENDPOINT (item);
  GVariantBuilder b;

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "adapter", g_variant_new_uint64 ((guint64) self->adapter));
  return g_variant_builder_end (&b);
}

static gboolean
si_monitor_endpoint_configure (WpSessionItem * item, GVariant * args)
{
  WpSiMonitorEndpoint *self = WP_SI_MONITOR_ENDPOINT (item);
  guint64 adapter_i;
  g_autoptr (GVariant) adapter_config = NULL;
  WpDirection direction = WP_DIRECTION_INPUT;
  const gchar *name = "Unknown";

  if (wp_session_item_get_flags (item) & (WP_SI_FLAG_ACTIVATING | WP_SI_FLAG_ACTIVE))
    return FALSE;

  /* reset previous config */
  si_monitor_endpoint_reset (WP_SESSION_ITEM (self));

  /* get the adapter */
  if (!g_variant_lookup (args, "adapter", "t", &adapter_i))
    return FALSE;
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (GUINT_TO_POINTER (adapter_i)), FALSE);
  self->adapter = g_object_ref (GUINT_TO_POINTER (adapter_i));

  /* get the adapter direction */
  adapter_config = wp_session_item_get_configuration (self->adapter);
  if (!g_variant_lookup (adapter_config, "direction", "y", &direction)) {
    wp_warning_object (self, "could not get adapter direction");
    return FALSE;
  }

  /* make sure the direction is always input */
  if (direction != WP_DIRECTION_INPUT) {
    wp_warning_object (self, "only input adapters are valid when configuring");
    return FALSE;
  }

  /* set the name */
  g_variant_lookup (adapter_config, "name", "&s", &name);
  g_snprintf (self->name, sizeof (self->name) - 1, "monitor.%s", name);

  wp_session_item_set_flag (item, WP_SI_FLAG_CONFIGURED);
  return TRUE;
}

static guint
si_monitor_endpoint_activate_get_next_step (WpSessionItem * item,
     WpTransition * transition, guint step)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      return STEP_VERIFY_CONFIG;

    case STEP_VERIFY_CONFIG:
      return STEP_ENSURE_ADAPTER_ACTIVATED;

    case STEP_ENSURE_ADAPTER_ACTIVATED:
      return WP_TRANSITION_STEP_NONE;

    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static void
si_monitor_endpoint_activate_execute_step (WpSessionItem * item,
    WpTransition * transition, guint step)
{
  WpSiMonitorEndpoint *self = WP_SI_MONITOR_ENDPOINT (item);

  switch (step) {
    case STEP_VERIFY_CONFIG:
      if (G_UNLIKELY (!(wp_session_item_get_flags (item) & WP_SI_FLAG_CONFIGURED))) {
        wp_transition_return_error (transition,
            g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
                "si-monitor-endpoint: cannot activate item without it "
                "being configured first"));
      }
      wp_transition_advance (transition);
      break;

    case STEP_ENSURE_ADAPTER_ACTIVATED:
      if (!(wp_session_item_get_flags (self->adapter) & WP_SI_FLAG_ACTIVE)) {
        wp_transition_return_error (transition,
            g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
                "si-monitor-endpoint: cannot activate item without its adapter "
                "being activated first"));
      }
      wp_transition_advance (transition);
      break;

    default:
      g_return_if_reached ();
  }
}

static void
si_monitor_endpoint_class_init (WpSiMonitorEndpointClass * klass)
{
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  si_class->reset = si_monitor_endpoint_reset;
  si_class->get_associated_proxy = si_monitor_endpoint_get_associated_proxy;
  si_class->configure = si_monitor_endpoint_configure;
  si_class->get_configuration = si_monitor_endpoint_get_configuration;
  si_class->activate_get_next_step =
      si_monitor_endpoint_activate_get_next_step;
  si_class->activate_execute_step =
      si_monitor_endpoint_activate_execute_step;
}

static GVariant *
si_monitor_endpoint_get_registration_info (WpSiEndpoint * item)
{
  WpSiMonitorEndpoint *self = WP_SI_MONITOR_ENDPOINT (item);
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(ssya{ss})"));
  g_variant_builder_add (&b, "s", self->name);
  g_variant_builder_add (&b, "s", "Audio/Source");
  g_variant_builder_add (&b, "y", WP_DIRECTION_OUTPUT);
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpProperties *
si_monitor_endpoint_get_properties (WpSiEndpoint * item)
{
  WpSiMonitorEndpoint *self = WP_SI_MONITOR_ENDPOINT (item);
  WpProperties *properties;
  g_autofree gchar *description = NULL;
  guint32 endpoint_id = 0;

  properties = wp_si_endpoint_get_properties (WP_SI_ENDPOINT (self->adapter));
  description = g_strdup_printf ("Monitor of %s",
      wp_properties_get (properties, "endpoint.description"));
  wp_properties_set (properties, "endpoint.description", description);

  endpoint_id = wp_session_item_get_associated_proxy_id (self->adapter,
      WP_TYPE_ENDPOINT);
  if (endpoint_id)
    wp_properties_setf (properties, PW_KEY_ENDPOINT_MONITOR, "%u", endpoint_id);

  return properties;
}

static guint
si_monitor_endpoint_get_n_streams (WpSiEndpoint * item)
{
  return 1;
}

static WpSiStream *
si_monitor_endpoint_get_stream (WpSiEndpoint * item, guint index)
{
  g_return_val_if_fail (index == 0, NULL);
  return WP_SI_STREAM (item);
}

static void
si_monitor_endpoint_endpoint_init (WpSiEndpointInterface * iface)
{
  iface->get_registration_info = si_monitor_endpoint_get_registration_info;
  iface->get_properties = si_monitor_endpoint_get_properties;
  iface->get_n_streams = si_monitor_endpoint_get_n_streams;
  iface->get_stream = si_monitor_endpoint_get_stream;
}

static GVariant *
si_monitor_endpoint_get_stream_registration_info (WpSiStream * self)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(sa{ss})"));
  g_variant_builder_add (&b, "s", "default");
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpSiEndpoint *
si_monitor_endpoint_get_stream_parent_endpoint (WpSiStream * self)
{
  return WP_SI_ENDPOINT (g_object_ref (self));
}

static void
si_monitor_endpoint_stream_init (WpSiStreamInterface * iface)
{
  iface->get_registration_info =
      si_monitor_endpoint_get_stream_registration_info;
  iface->get_parent_endpoint =
      si_monitor_endpoint_get_stream_parent_endpoint;
}

static GVariant *
si_monitor_endpoint_get_ports (WpSiPortInfo * item, const gchar * context)
{
  WpSiMonitorEndpoint *self = WP_SI_MONITOR_ENDPOINT (item);

  return wp_si_port_info_get_ports (WP_SI_PORT_INFO (self->adapter), "monitor");
}

static void
si_monitor_endpoint_port_info_init (WpSiPortInfoInterface * iface)
{
  iface->get_ports = si_monitor_endpoint_get_ports;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a(ssymv)"));
  g_variant_builder_add (&b, "(ssymv)", "adapter", "t",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);

  wp_si_factory_register (core, wp_si_factory_new_simple (
      "si-monitor-endpoint", si_monitor_endpoint_get_type (),
      g_variant_builder_end (&b)));
  return TRUE;
}
