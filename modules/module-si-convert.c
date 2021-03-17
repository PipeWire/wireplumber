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

#include <spa/utils/names.h>
#include <spa/param/format.h>
#include <spa/param/audio/raw.h>
#include <spa/param/param.h>

#define SI_FACTORY_NAME "si-convert"

struct _WpSiConvert
{
  WpSessionItem parent;

  /* configuration */
  WpSessionItem *target;
  WpSession *session;
  gchar name[96];
  WpDirection direction;
  gboolean control_port;

  /* activate */
  WpNode *node;
  WpObjectManager *links_watch;
  WpSessionItem *link_to_target;

  /* export */
  WpImplEndpoint *impl_endpoint;
};

static void si_convert_endpoint_init (WpSiEndpointInterface * iface);
static void si_convert_port_info_init (WpSiPortInfoInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiConvert, si_convert, WP, SI_CONVERT, WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiConvert, si_convert, WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ENDPOINT, si_convert_endpoint_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_PORT_INFO, si_convert_port_info_init))

static void
si_convert_init (WpSiConvert * self)
{
}

static void
si_convert_reset (WpSessionItem * item)
{
  WpSiConvert *self = WP_SI_CONVERT (item);

  /* deactivate first */
  wp_object_deactivate (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED);

  /* reset */
  g_clear_object (&self->target);
  g_clear_object (&self->session);
  self->name[0] = '\0';
  self->direction = WP_DIRECTION_INPUT;
  self->control_port = FALSE;

  WP_SESSION_ITEM_CLASS (si_convert_parent_class)->reset (item);
}

static gboolean
si_convert_configure (WpSessionItem * item, WpProperties *p)
{
  WpSiConvert *self = WP_SI_CONVERT (item);
  g_autoptr (WpProperties) si_props = wp_properties_ensure_unique_owner (p);
  WpSessionItem *target;
  WpProperties *target_props = NULL;
  WpSession *session = NULL;
  const gchar *str;

  /* reset previous config */
  si_convert_reset (item);

  str = wp_properties_get (si_props, "name");
  if (!str)
    return FALSE;
  strncpy (self->name, str, sizeof (self->name) - 1);

  str = wp_properties_get (si_props, "target");
  if (!str || sscanf(str, "%p", &target) != 1 || !WP_IS_SESSION_ITEM (target))
    return FALSE;

  target_props = wp_session_item_get_properties (target);

  str = wp_properties_get (si_props, "direction");
  if (!str) {
    str = wp_properties_get (target_props, "direction");
    wp_properties_set (si_props, "direction", str);
  }
  if (!str || sscanf(str, "%u", &self->direction) != 1)
    return FALSE;

  str = wp_properties_get (si_props, "enable-control-port");
  if (str && sscanf(str, "%u", &self->control_port) != 1)
    return FALSE;
  if (!str)
    wp_properties_setf (si_props, "enable-control-port", "%u",
        self->control_port);

  /* session is optional (only needed if we want to export) */
  str = wp_properties_get (si_props, "session");
  if (str && (sscanf(str, "%p", &session) != 1 || !WP_IS_SESSION (session)))
    return FALSE;
  if (!str)
    wp_properties_setf (si_props, "session", "%p", session);

  self->target = g_object_ref (target);
  if (session)
    self->session = g_object_ref (session);

  wp_properties_set (si_props, "si-factory-name", SI_FACTORY_NAME);
  wp_session_item_set_properties (WP_SESSION_ITEM (self),
      g_steal_pointer (&si_props));
  return TRUE;
}

static gpointer
si_convert_get_associated_proxy (WpSessionItem * item, GType proxy_type)
{
  WpSiConvert *self = WP_SI_CONVERT (item);

  if (proxy_type == WP_TYPE_NODE)
    return self->node ? g_object_ref (self->node) : NULL;
  if (proxy_type == WP_TYPE_SESSION)
    return self->session ? g_object_ref (self->session) : NULL;
  else if (proxy_type == WP_TYPE_ENDPOINT)
    return self->impl_endpoint ? g_object_ref (self->impl_endpoint) : NULL;

  return NULL;
}

static void
si_convert_disable_active (WpSessionItem *si)
{
  WpSiConvert *self = WP_SI_CONVERT (si);

  g_clear_object (&self->node);
  g_clear_object (&self->links_watch);
  g_clear_object (&self->link_to_target);
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
}

static void
si_convert_disable_exported (WpSessionItem *si)
{
  WpSiConvert *self = WP_SI_CONVERT (si);

  g_clear_object (&self->impl_endpoint);
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_EXPORTED);
}

static void
on_link_activated (WpSessionItem * item, GAsyncResult * res, WpSiConvert * self)
{
  g_autoptr (GError) error = NULL;
  if (!wp_object_activate_finish (WP_OBJECT (item), res, &error))
    wp_warning_object (item, "failed to activate link to the target node: %s",
        error->message);
}

static void
do_link_to_target (WpSiConvert *self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self->node));
  g_autoptr (WpSessionItem) link = wp_session_item_make (core,
      "si-standard-link");
  WpProperties *props = NULL;

  if (G_UNLIKELY (!link)) {
    wp_warning_object (self, "could not create link; is the module loaded?");
    return;
  }

  props = wp_properties_new_empty ();
  if (self->direction == WP_DIRECTION_INPUT) {
      /* Playback */
      wp_properties_setf (props, "out-endpoint", "%p", WP_SI_ENDPOINT (self));
      wp_properties_setf (props, "in-endpoint", "%p",
          WP_SI_ENDPOINT (self->target));
      wp_properties_set (props, "out-endpoint-port-context", "reverse");
  } else {
      /* Capture */
      wp_properties_setf (props, "out-endpoint", "%p",
          WP_SI_ENDPOINT (self->target));
      wp_properties_setf (props, "in-endpoint", "%p", WP_SI_ENDPOINT (self));
      wp_properties_set (props, "in-endpoint-port-context", "reverse");
  }

  /* always create passive links; that means that they won't hold the graph
     running if they are the only links left around */
  wp_properties_setf (props, "passive", "%u", TRUE);

  wp_session_item_configure (link, props);
  wp_object_activate (WP_OBJECT (link), WP_SESSION_ITEM_FEATURE_ACTIVE, NULL,
      (GAsyncReadyCallback) on_link_activated, self);
  self->link_to_target = g_steal_pointer (&link);
}

static void
on_links_changed (WpObjectManager * om, WpSiConvert * self)
{
  if (wp_object_manager_get_n_objects (om) == 0)
    g_clear_object (&self->link_to_target);
  else if (!self->link_to_target)
    do_link_to_target (self);
}

static void
si_convert_do_links_watch (WpSiConvert *self, WpTransition *transition)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_TUPLE);
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  GVariant *ports_v = NULL;

  /* get a list of our ports */
  for (it = wp_node_new_ports_iterator (self->node);
      wp_iterator_next (it, &val);
      g_value_unset (&val)) {
    WpPort *port = g_value_get_object (&val);
    if (wp_port_get_direction (port) != self->direction)
      continue;
    g_variant_builder_add (&b, "u", wp_proxy_get_bound_id (WP_PROXY (port)));
  }
  ports_v = g_variant_builder_end (&b);

  /* create the object manager */
  self->links_watch = wp_object_manager_new ();
  wp_object_manager_request_object_features (self->links_watch,
      WP_TYPE_LINK, WP_PROXY_FEATURE_BOUND);

  /* interested in links that have one of our ports in their
     'link.input.port' or 'link.output.port' global property */
  wp_object_manager_add_interest_full (self->links_watch, ({
    WpObjectInterest *interest = wp_object_interest_new_type (WP_TYPE_LINK);
    wp_object_interest_add_constraint (interest,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
        (self->direction == WP_DIRECTION_INPUT) ?
            PW_KEY_LINK_INPUT_PORT : PW_KEY_LINK_OUTPUT_PORT,
        WP_CONSTRAINT_VERB_IN_LIST,
        ports_v);
    interest;
  }));

  g_signal_connect_object (self->links_watch, "objects-changed",
      G_CALLBACK (on_links_changed), self, 0);
  wp_core_install_object_manager (core, self->links_watch);

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
}

static void
on_node_activate_done (WpObject * node, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiConvert *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (node, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  si_convert_do_links_watch (self, transition);
}

static void
on_impl_endpoint_activated (WpObject * object, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiConvert *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (object, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_EXPORTED, 0);
}

static void
si_convert_enable_active (WpSessionItem *si, WpTransition *transition)
{
  WpSiConvert *self = WP_SI_CONVERT (si);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpProperties) node_props = NULL;
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (WpProperties) target_props = NULL;
  g_autoptr (WpSpaPod) format = NULL;
  const gchar *str;
  guint32 channels;
  guint32 rate;

  if (!wp_session_item_is_configured (WP_SESSION_ITEM (self))) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "si-convert: item is not configured"));
    return;
  }

  /* Get the associated node */
  node = wp_session_item_get_associated_proxy (self->target, WP_TYPE_NODE);

  /* set channels & rate */
  target_props = wp_session_item_get_properties (self->target);
  str = wp_properties_get (target_props, "preferred-n-channels");
  if (!str || sscanf(str, "%u", &channels) != 1)
    channels = 2;
  rate = ({
    g_autoptr (WpProperties) props = wp_core_get_remote_properties (core);
    const gchar *rate_str = wp_properties_get (props, "default.clock.rate");
    rate_str ? atoi (rate_str) : 48000;
  });

  /* Create the convert properties based on the adapter properties */
  node_props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (node));
  props = wp_properties_new (
      PW_KEY_MEDIA_CLASS, "Audio/Convert",
      PW_KEY_FACTORY_NAME, SPA_NAME_AUDIO_CONVERT,
      /* the default mode is 'split', which breaks audio in this case */
      "factory.mode", "convert",
      NULL);
  wp_properties_setf (props, PW_KEY_OBJECT_PATH, "%s:%s",
      wp_properties_get (node_props, PW_KEY_OBJECT_PATH),
      self->name);
  wp_properties_setf (props, PW_KEY_NODE_NAME, "%s.%s.%s",
      SPA_NAME_AUDIO_CONVERT,
      wp_properties_get (node_props, PW_KEY_NODE_NAME),
      self->name);
  wp_properties_setf (props, PW_KEY_NODE_DESCRIPTION,
      "Converter volume for %s: %s",
      wp_properties_get (node_props, PW_KEY_NODE_DESCRIPTION), self->name);

  /* Create the node */
  self->node = wp_node_new_from_factory (core, "spa-node-factory",
      g_steal_pointer (&props));

  format = wp_spa_pod_new_object (
      "Spa:Pod:Object:Param:Format", "Format",
      "mediaType",    "K", "audio",
      "mediaSubtype", "K", "raw",
      "format",       "K", "F32P",
      "rate",         "i", rate,
      "channels",     "i", channels,
      NULL);

  /* Configure audioconvert to be both merger and splitter; this means it
    will have an equal number of input and output ports and just passthrough
    the same format, but with altered volume.
    In the future we need to consider writing a simpler volume node for this,
    as doing merge + split is heavy for our needs */
  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (self->node),
      "PortConfig", 0,
      wp_spa_pod_new_object (
          "Spa:Pod:Object:Param:PortConfig", "PortConfig",
          "direction",  "I", pw_direction_reverse (self->direction),
          "mode",       "K", "dsp",
          "format",     "P", format,
          NULL));

  wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (self->node),
      "PortConfig", 0,
      wp_spa_pod_new_object (
          "Spa:Pod:Object:Param:PortConfig", "PortConfig",
          "direction",  "I", self->direction,
          "mode",       "K", "dsp",
          "control",    "b", self->control_port,
          "format",     "P", format,
          NULL));

  wp_object_activate (WP_OBJECT (self->node),
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL | WP_NODE_FEATURE_PORTS, NULL,
      (GAsyncReadyCallback) on_node_activate_done, transition);
}

static void
si_convert_enable_exported (WpSessionItem *si, WpTransition *transition)
{
  WpSiConvert *self = WP_SI_CONVERT (si);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  self->impl_endpoint = wp_impl_endpoint_new (core, WP_SI_ENDPOINT (self));

  g_signal_connect_object (self->impl_endpoint, "pw-proxy-destroyed",
      G_CALLBACK (wp_session_item_handle_proxy_destroyed), self, 0);

  wp_object_activate (WP_OBJECT (self->impl_endpoint),
      WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_impl_endpoint_activated, transition);
}

static GVariant *
si_convert_get_registration_info (WpSiEndpoint * item)
{
  WpSiConvert *self = WP_SI_CONVERT (item);
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(ssya{ss})"));
  g_variant_builder_add (&b, "s", self->name);
  g_variant_builder_add (&b, "s", "Audio/Convert");
  g_variant_builder_add (&b, "y", (guchar) self->direction);
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpProperties *
si_convert_get_properties (WpSiEndpoint * item)
{
  WpSiConvert *self = WP_SI_CONVERT (item);
  WpProperties *result = wp_properties_new_empty ();

  wp_properties_set (result, "endpoint.priority", NULL);
  wp_properties_setf (result, "endpoint.description", "%s", "Audio Converter");
  wp_properties_setf (result, PW_KEY_ENDPOINT_AUTOCONNECT, "%d", FALSE);
  wp_properties_set (result, PW_KEY_ENDPOINT_CLIENT_ID, NULL);

  /* associate with the node */
  wp_properties_setf (result, PW_KEY_NODE_ID, "%d",
      wp_proxy_get_bound_id (WP_PROXY (self->node)));

  return result;
}

static void
si_convert_endpoint_init (WpSiEndpointInterface * iface)
{
  iface->get_registration_info = si_convert_get_registration_info;
  iface->get_properties = si_convert_get_properties;
}

static GVariant *
si_convert_get_ports (WpSiPortInfo * item, const gchar * context)
{
  WpSiConvert *self = WP_SI_CONVERT (item);
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_ARRAY);
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  WpDirection direction = self->direction;
  guint32 node_id;

  /* context can only be either NULL or "reverse" */
  if (!g_strcmp0 (context, "reverse")) {
    direction = (self->direction == WP_DIRECTION_INPUT) ?
        WP_DIRECTION_OUTPUT : WP_DIRECTION_INPUT;
  }
  else if (context != NULL) {
    /* on any other context, return an empty list of ports */
    return g_variant_new_array (G_VARIANT_TYPE ("(uuu)"), NULL, 0);
  }

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a(uuu)"));
  node_id = wp_proxy_get_bound_id (WP_PROXY (self->node));

  for (it = wp_node_new_ports_iterator (self->node);
       wp_iterator_next (it, &val);
       g_value_unset (&val))
  {
    WpPort *port = g_value_get_object (&val);
    g_autoptr (WpProperties) props = NULL;
    const gchar *channel;
    guint32 port_id, channel_id = 0;

    if (wp_port_get_direction (port) != direction)
      continue;

    port_id = wp_proxy_get_bound_id (WP_PROXY (port));

    /* try to find the audio channel; if channel is NULL, this will silently
       leave the channel_id to its default value, 0 */
    props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (port));
    channel = wp_properties_get (props, PW_KEY_AUDIO_CHANNEL);
    if (channel) {
      WpSpaIdValue idval = wp_spa_id_value_from_short_name (
          "Spa:Enum:AudioChannel", channel);
      if (idval)
        channel_id = wp_spa_id_value_number (idval);
    }

    g_variant_builder_add (&b, "(uuu)", node_id, port_id, channel_id);
  }

  return g_variant_builder_end (&b);
}

static void
si_convert_port_info_init (WpSiPortInfoInterface * iface)
{
  iface->get_ports = si_convert_get_ports;
}

static void
si_convert_class_init (WpSiConvertClass * klass)
{
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  si_class->reset = si_convert_reset;
  si_class->configure = si_convert_configure;
  si_class->get_associated_proxy = si_convert_get_associated_proxy;
  si_class->disable_active = si_convert_disable_active;
  si_class->disable_exported = si_convert_disable_exported;
  si_class->enable_active = si_convert_enable_active;
  si_class->enable_exported = si_convert_enable_exported;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_si_factory_register (core, wp_si_factory_new_simple (SI_FACTORY_NAME,
      si_convert_get_type ()));
  return TRUE;
}
