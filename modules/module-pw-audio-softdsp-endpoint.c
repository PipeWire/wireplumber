/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * module-pw-audio-softdsp-endpoint provides a WpEndpoint implementation
 * that wraps an audio device node in pipewire and plugs a DSP node, as well
 * as optional merger+volume nodes that are used as entry points for the
 * various streams that this endpoint may have
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include "module-pipewire/port.h"

#define MIN_QUANTUM_SIZE  64
#define MAX_QUANTUM_SIZE  1024

struct _WpPwAudioSoftdspEndpoint {
  WpEndpoint parent;
  
  /* The core proxy */
  struct pw_core_proxy *core_proxy;

  /* Node proxy and listener */
  struct pw_proxy *node_proxy;
  struct spa_hook listener;
  struct spa_hook proxy_listener;

  /* Node info */
  struct pw_node_info *node_info;
  uint32_t media_type;
  uint32_t media_subtype;
  struct spa_audio_info_raw format;
  enum pw_direction direction;

  /* DSP proxy and listener */
  struct pw_proxy *dsp_proxy;
  struct spa_hook dsp_listener;
  
  /* DSP info */
  struct pw_node_info *dsp_info;

  /* Link proxy and listener */
  struct pw_proxy *link_proxy;

  /* The all port list reference */
  /* TODO: make it thread safe */
  struct spa_list *port_list;
};

G_DECLARE_FINAL_TYPE (WpPwAudioSoftdspEndpoint, endpoint,
    WP_PW, AUDIO_SOFTDSP_ENDPOINT, WpEndpoint)

G_DEFINE_TYPE (WpPwAudioSoftdspEndpoint, endpoint, WP_TYPE_ENDPOINT)

static void
endpoint_init (WpPwAudioSoftdspEndpoint * self)
{
}

static void
endpoint_finalize (GObject * object)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);

  /* Remove and destroy the node_proxy */
  if (self->node_proxy) {
    spa_hook_remove (&self->listener);
    pw_proxy_destroy ((struct pw_proxy *) self->node_proxy);
  }

  /* Remove and destroy the dsp_proxy */
  if (self->dsp_proxy) {
    spa_hook_remove (&self->dsp_listener);
    pw_proxy_destroy ((struct pw_proxy *) self->dsp_proxy);
  }

  G_OBJECT_CLASS (endpoint_parent_class)->finalize (object);
}

static gboolean
endpoint_prepare_link (WpEndpoint * ep, guint32 stream_id,
    WpEndpointLink * link, GVariant ** properties, GError ** error)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);
  WpPort *port = NULL, *node_port = NULL, *dsp_port = NULL;
  GVariantBuilder b;

  /* Find the node port */
  spa_list_for_each(port, self->port_list, l) {
    if (self->node_info->id == port->parent_id) {
      node_port = port;
      break;
    }
  }
  if (!node_port)
    return FALSE;
  
  /* Find the first dsp port with the same direction as the node port */
  spa_list_for_each(port, self->port_list, l) {
    if (self->dsp_info->id == port->parent_id
        && port->direction == node_port->direction) {
      dsp_port = port;
      break;
    }
  }
  if (!dsp_port)
    return FALSE;

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "node-id",
      g_variant_new_uint32 (self->dsp_info->id));
  g_variant_builder_add (&b, "{sv}", "node-port-id",
      g_variant_new_uint32 (dsp_port->id));
  *properties = g_variant_builder_end (&b);

  return TRUE;
}

static void
endpoint_class_init (WpPwAudioSoftdspEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpEndpointClass *endpoint_class = (WpEndpointClass *) klass;

  object_class->finalize = endpoint_finalize;

  endpoint_class->prepare_link = endpoint_prepare_link;
}

static void on_dsp_running(WpPwAudioSoftdspEndpoint *self)
{
  struct pw_properties *props;

  /* Return if the node has already been linked */
  if (self->link_proxy)
    return;

  /* Create new properties */
  props = pw_properties_new(NULL, NULL);

  /* Set the new properties */
  pw_properties_set(props, PW_LINK_PROP_PASSIVE, "true");
  if (self->direction == PW_DIRECTION_OUTPUT) {
    pw_properties_setf(props, PW_LINK_OUTPUT_NODE_ID, "%d", self->dsp_info->id);
    pw_properties_setf(props, PW_LINK_OUTPUT_PORT_ID, "%d", -1);
    pw_properties_setf(props, PW_LINK_INPUT_NODE_ID, "%d", self->node_info->id);
    pw_properties_setf(props, PW_LINK_INPUT_PORT_ID, "%d", -1);
  } else {
    pw_properties_setf(props, PW_LINK_OUTPUT_NODE_ID, "%d", self->node_info->id);
    pw_properties_setf(props, PW_LINK_OUTPUT_PORT_ID, "%d", -1);
    pw_properties_setf(props, PW_LINK_INPUT_NODE_ID, "%d", self->dsp_info->id);
    pw_properties_setf(props, PW_LINK_INPUT_PORT_ID, "%d", -1);
  }

  /* Create the link */
  self->link_proxy = pw_core_proxy_create_object(self->core_proxy,
      "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0);

  /* Clean up */
  pw_properties_free(props);
}

static void dsp_node_event_info(void *data, const struct pw_node_info *info)
{
  WpPwAudioSoftdspEndpoint *self = data;

  /* Set dsp info */
  self->dsp_info = pw_node_info_update(self->dsp_info, info);
  
  /* Handle the different states */
  switch (info->state) {
  case PW_NODE_STATE_IDLE:
          break;
  case PW_NODE_STATE_RUNNING:
          on_dsp_running(self);
          break;
  case PW_NODE_STATE_SUSPENDED:
          break;
  default:
          break;
  }
}

static const struct pw_node_proxy_events dsp_node_events = {
  PW_VERSION_NODE_PROXY_EVENTS,
  .info = dsp_node_event_info,
};

static void emit_audio_dsp_node(WpPwAudioSoftdspEndpoint *self)
{
  struct pw_properties *props;
  const char *dsp_name = NULL;
  uint8_t buf[1024];
  struct spa_pod_builder pod_builder = { 0, };
  struct spa_pod *param;
  
  /* Return if the node has been already emitted */
  if (self->dsp_proxy)
    return;
  
  /* Get the properties */
  props = pw_properties_new_dict(self->node_info->props);
  if (!props)
    return;

  /* Get the DSP name */
  dsp_name = pw_properties_get(props, "device.nick");
  if (!dsp_name)
    dsp_name = self->node_info->name;

  /* Set the properties */
  pw_properties_set(props, "audio-dsp.name", dsp_name);
  pw_properties_setf(props, "audio-dsp.direction", "%d", self->direction);
  pw_properties_setf(props, "audio-dsp.maxbuffer", "%ld", MAX_QUANTUM_SIZE * sizeof(float));

  /* Set the DSP proxy and listener */
  self->dsp_proxy = pw_core_proxy_create_object(self->core_proxy, "audio-dsp",
      PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &props->dict, 0);
  pw_proxy_add_proxy_listener(self->dsp_proxy, &self->dsp_listener,
      &dsp_node_events, self);

  /* Set DSP proxy params */
  spa_pod_builder_init(&pod_builder, buf, sizeof(buf));
  param = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format, &self->format);
  param = spa_pod_builder_add_object(&pod_builder,
      SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
      SPA_PARAM_PROFILE_direction,  SPA_POD_Id(pw_direction_reverse(self->direction)),
      SPA_PARAM_PROFILE_format,     SPA_POD_Pod(param));
  pw_node_proxy_set_param((struct pw_node_proxy*)self->dsp_proxy,
      SPA_PARAM_Profile, 0, param);
  
  /* Clean up */    
  pw_properties_free(props);
}

static void node_event_info(void *data, const struct pw_node_info *info)
{
  WpPwAudioSoftdspEndpoint *self = data;
  WpPort *port = NULL;

  /* Set the node info */
  self->node_info = pw_node_info_update(self->node_info, info);

  /* Find the node port */
  spa_list_for_each(port, self->port_list, l) {
    if (port->parent_id == self->node_info->id)
      break;
  }

  /* Set the format using the port format */
  self->format.format = port->format.format;
  self->format.flags = port->format.flags;
  self->format.rate = port->format.rate;
  self->format.channels = port->format.channels;
  for (int i = 0; i < port->format.channels; ++i)
    self->format.position[i] = port->format.position[i];

  /* Emit the audio DSP node */
  emit_audio_dsp_node(self);

  /* TODO: Handle the different states */
  switch (info->state) {
  case PW_NODE_STATE_IDLE:
    break;
  case PW_NODE_STATE_RUNNING:
    break;
  case PW_NODE_STATE_SUSPENDED:
    break;
  default:
    break;
  }
}

static void node_proxy_destroy(void *data)
{
  WpPwAudioSoftdspEndpoint *self = data;

  /* Clear node_info */
  if (self->node_info)
    pw_node_info_free(self->node_info);

  /* Clear dsp_info */
  if (self->dsp_info)
    pw_node_info_free(self->dsp_info);

  wp_endpoint_unregister (WP_ENDPOINT (self));
}

static const struct pw_proxy_events node_proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = node_proxy_destroy,
};

static const struct pw_node_proxy_events node_events = {
  PW_VERSION_NODE_PROXY_EVENTS,
  .info = node_event_info,
};

static gpointer
endpoint_factory (WpFactory * factory, GType type, GVariant * properties)
{
  WpCore *wp_core = NULL;
  struct pw_remote *remote;
  const gchar *name = NULL;
  const gchar *media_class = NULL;
  guint64 proxy, port_list;

  /* Make sure the type is not the base class type */
  if (type != WP_TYPE_ENDPOINT)
    return NULL;

  /* Get the WirePlumber core */
  wp_core = wp_factory_get_core(factory);
  if (!wp_core) {
    g_warning("failed to get wireplumbe core. Skipping...");
    return NULL;
  }

  /* Get the remote */
  remote = wp_core_get_global(wp_core, WP_GLOBAL_PW_REMOTE);
  if (!remote) {
    g_warning("failed to get core remote. Skipping...");
    return NULL;
  }

  /* Get the name and media-class */
  if (!g_variant_lookup (properties, "name", "&s", &name))
      return NULL;
  if (!g_variant_lookup (properties, "media-class", "&s", &media_class))
      return NULL;
  if (!g_variant_lookup (properties, "node-proxy", "t", &proxy))
      return NULL;
  if (!g_variant_lookup (properties, "port-list", "t", &port_list))
      return NULL;

  /* Create the softdsp endpoint object */
  WpPwAudioSoftdspEndpoint *ep = g_object_new (endpoint_get_type (),
      "name", name,
      "media-class", media_class,
      NULL);
  if (!ep)
    return NULL;

  /* Set the direction */
  if (g_str_has_suffix(media_class, "Source")) {
    ep->direction = PW_DIRECTION_INPUT;
  } else if (g_str_has_suffix(media_class, "Sink")) {
    ep->direction = PW_DIRECTION_OUTPUT;
  } else {
    g_warning("failed to parse direction. Skipping...");
    return NULL;
  }

  /* Set the port list reference */
  ep->port_list = (gpointer) port_list;

  /* Set the core proxy */
  ep->core_proxy = pw_remote_get_core_proxy(remote);
  if (!ep->core_proxy) {
    g_warning("failed to get core proxy. Skipping...");
    return NULL;
  }

  /* Set the node proxy and listener */
  ep->node_proxy = (gpointer) proxy;
  pw_proxy_add_listener (ep->node_proxy, &ep->listener, &node_proxy_events,
      ep);
  pw_proxy_add_proxy_listener(ep->node_proxy, &ep->proxy_listener,
      &node_events, ep);

  /* Return the object */
  return ep;
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  /* Register the softdsp endpoint */
  wp_factory_new (core, "pw-audio-softdsp-endpoint", endpoint_factory);
}
