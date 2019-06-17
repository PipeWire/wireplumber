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
#include <spa/pod/builder.h>
#include <spa/param/props.h>

#define MIN_QUANTUM_SIZE  64
#define MAX_QUANTUM_SIZE  1024

struct _WpPwAudioSoftdspEndpoint
{
  WpEndpoint parent;

  /* temporary method to select which endpoint
   * is going to be the default input/output */
  gboolean selected;
  
  /* Core */
  struct pw_core_proxy *core_proxy;

  /* Registry */
  struct pw_registry_proxy *registry_proxy;
  struct spa_hook registry_listener;

  /* Direction */
  enum pw_direction direction;

  /* Proxy */
  WpProxyNode *proxy_node;
  WpProxyPort *proxy_port;

  /* DSP port id */
  uint32_t dsp_port_id;

  /* Volume */
  gfloat master_volume;
  gboolean master_mute;

  /* TODO: This needs to use the new proxy API */
  struct pw_node_proxy *dsp_proxy;
  struct spa_hook dsp_listener;
  struct pw_node_info *dsp_info;
  struct pw_proxy *link_proxy;
};

enum {
  PROP_0,
  PROP_NODE_PROXY,
  PROP_PORT_PROXY,
};

enum {
  CONTROL_VOLUME = 0,
  CONTROL_MUTE,
  CONTROL_SELECTED,
};

G_DECLARE_FINAL_TYPE (WpPwAudioSoftdspEndpoint, endpoint,
    WP_PW, AUDIO_SOFTDSP_ENDPOINT, WpEndpoint)

G_DEFINE_TYPE (WpPwAudioSoftdspEndpoint, endpoint, WP_TYPE_ENDPOINT)

static gboolean
endpoint_prepare_link (WpEndpoint * ep, guint32 stream_id,
    WpEndpointLink * link, GVariant ** properties, GError ** error)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);
  GVariantBuilder b;
  
  /* Make sure dsp info is valid */
  if (!self->dsp_info)
    return FALSE;
  
  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}", "node-id",
      g_variant_new_uint32 (self->dsp_info->id));
  g_variant_builder_add (&b, "{sv}", "node-port-id",
      g_variant_new_uint32 (self->dsp_port_id));
  *properties = g_variant_builder_end (&b);

  return TRUE;
}

static void
on_dsp_running(WpPwAudioSoftdspEndpoint *self)
{
  struct pw_properties *props;
  const struct pw_node_info *node_info = NULL;

  /* Return if the node has already been linked */
  if (self->link_proxy)
    return;

  /* Get the node info */
  node_info = wp_proxy_node_get_info(self->proxy_node);
  if (!node_info)
    return;

  /* Create new properties */
  props = pw_properties_new(NULL, NULL);

  /* Set the new properties */
  pw_properties_set(props, PW_LINK_PROP_PASSIVE, "true");
  if (self->direction == PW_DIRECTION_OUTPUT) {
    pw_properties_setf(props, PW_LINK_OUTPUT_NODE_ID, "%d", self->dsp_info->id);
    pw_properties_setf(props, PW_LINK_OUTPUT_PORT_ID, "%d", -1);
    pw_properties_setf(props, PW_LINK_INPUT_NODE_ID, "%d", node_info->id);
    pw_properties_setf(props, PW_LINK_INPUT_PORT_ID, "%d", -1);
  } else {
    pw_properties_setf(props, PW_LINK_OUTPUT_NODE_ID, "%d", node_info->id);
    pw_properties_setf(props, PW_LINK_OUTPUT_PORT_ID, "%d", -1);
    pw_properties_setf(props, PW_LINK_INPUT_NODE_ID, "%d", self->dsp_info->id);
    pw_properties_setf(props, PW_LINK_INPUT_PORT_ID, "%d", -1);
  }

  g_debug ("%p linking DSP to node", self);

  /* Create the link */
  self->link_proxy = pw_core_proxy_create_object(self->core_proxy,
      "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0);

  /* Clean up */
  pw_properties_free(props);
}

static void
on_dsp_idle (WpPwAudioSoftdspEndpoint *self)
{
  if (self->link_proxy != NULL) {
    g_debug ("%p unlinking DSP from node", self);
    pw_proxy_destroy (self->link_proxy);
    self->link_proxy = NULL;
  }
}

static void
dsp_node_event_info (void *data, const struct pw_node_info *info)
{
  WpPwAudioSoftdspEndpoint *self = data;

  /* Set dsp info */
  self->dsp_info = pw_node_info_update(self->dsp_info, info);

  /* Handle the different states */
  switch (info->state) {
  case PW_NODE_STATE_IDLE:
    on_dsp_idle (self);
    break;
  case PW_NODE_STATE_RUNNING:
    on_dsp_running (self);
    break;
  case PW_NODE_STATE_SUSPENDED:
    break;
  default:
    break;
  }
}

static void
dsp_node_event_param (void *object, int seq, uint32_t id,
    uint32_t index, uint32_t next, const struct spa_pod *param)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);

  switch (id) {
    case SPA_PARAM_Props:
    {
      struct spa_pod_prop *prop;
      struct spa_pod_object *obj = (struct spa_pod_object *) param;
      float volume = self->master_volume;
      bool mute = self->master_mute;

      SPA_POD_OBJECT_FOREACH(obj, prop) {
        switch (prop->key) {
        case SPA_PROP_volume:
          spa_pod_get_float(&prop->value, &volume);
          break;
        case SPA_PROP_mute:
          spa_pod_get_bool(&prop->value, &mute);
          break;
        default:
          break;
        }
      }

      g_debug ("WpEndpoint:%p param event, vol:(%lf -> %f) mute:(%d -> %d)",
          self, self->master_volume, volume, self->master_mute, mute);

      if (self->master_volume != volume) {
        self->master_volume = volume;
        wp_endpoint_notify_control_value (WP_ENDPOINT (self), CONTROL_VOLUME);
      }
      if (self->master_mute != mute) {
        self->master_mute = mute;
        wp_endpoint_notify_control_value (WP_ENDPOINT (self), CONTROL_MUTE);
      }

      break;
    }
    default:
      break;
  }
}

static const struct pw_node_proxy_events dsp_node_events = {
  PW_VERSION_NODE_PROXY_EVENTS,
  .info = dsp_node_event_info,
  .param = dsp_node_event_param,
};

static void
emit_audio_dsp_node (WpPwAudioSoftdspEndpoint *self)
{
  struct pw_properties *props;
  const char *dsp_name = NULL;
  uint8_t buf[1024];
  struct spa_pod_builder pod_builder = { 0, };
  struct spa_pod *param;
  uint32_t ids[1] = { SPA_PARAM_Props };
  const struct pw_node_info *node_info;
  const struct spa_audio_info_raw *port_format;
  struct spa_audio_info_raw format;
  
  /* Get the node info */
  node_info = wp_proxy_node_get_info(self->proxy_node);
  if (!node_info)
    return;

  /* Get the port format */
  port_format = wp_proxy_port_get_format(self->proxy_port);
  if (!port_format)
    return;
  format = *port_format;

  /* Get the properties */
  props = pw_properties_new_dict(node_info->props);
  if (!props)
    return;

  /* Get the DSP name */
  dsp_name = pw_properties_get(props, "device.nick");
  if (!dsp_name)
    dsp_name = node_info->name;

  /* Set the properties */
  pw_properties_set(props, "audio-dsp.name", dsp_name);
  pw_properties_setf(props, "audio-dsp.direction", "%d", self->direction);
  pw_properties_setf(props, "audio-dsp.maxbuffer", "%ld",
      MAX_QUANTUM_SIZE * sizeof(float));

  /* Set the DSP proxy and listener */
  self->dsp_proxy = pw_core_proxy_create_object(self->core_proxy, "audio-dsp",
      PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &props->dict, 0);
  pw_node_proxy_add_listener(self->dsp_proxy, &self->dsp_listener,
      &dsp_node_events, self);
  pw_node_proxy_subscribe_params (self->dsp_proxy, ids, SPA_N_ELEMENTS (ids));

  /* Set DSP proxy params */
  spa_pod_builder_init(&pod_builder, buf, sizeof(buf));
  param = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format, &format);
  param = spa_pod_builder_add_object(&pod_builder,
      SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
      SPA_PARAM_PROFILE_direction,  SPA_POD_Id(pw_direction_reverse(self->direction)),
      SPA_PARAM_PROFILE_format,     SPA_POD_Pod(param));
  pw_node_proxy_set_param((struct pw_node_proxy*)self->dsp_proxy,
      SPA_PARAM_Profile, 0, param);

  /* Clean up */
  pw_properties_free(props);
}

static void
endpoint_constructed (GObject * object)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);
  const gchar *media_class = wp_endpoint_get_media_class (WP_ENDPOINT (self));
  GVariantDict d;

  /* Set the direction */
  if (g_str_has_suffix (media_class, "Source")) {
    self->direction = PW_DIRECTION_INPUT;
  } else if (g_str_has_suffix (media_class, "Sink")) {
    self->direction = PW_DIRECTION_OUTPUT;
  } else {
    g_critical ("failed to parse direction");
  }

  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u", 0);
  g_variant_dict_insert (&d, "name", "s", "default");
  wp_endpoint_register_stream (WP_ENDPOINT (self), g_variant_dict_end (&d));

  self->master_volume = 1.0;
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u", CONTROL_VOLUME);
  g_variant_dict_insert (&d, "name", "s", "volume");
  g_variant_dict_insert (&d, "type", "s", "d");
  g_variant_dict_insert (&d, "range", "(dd)", 0.0, 1.0);
  g_variant_dict_insert (&d, "default-value", "d", self->master_volume);
  wp_endpoint_register_control (WP_ENDPOINT (self), g_variant_dict_end (&d));

  self->master_mute = FALSE;
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u", CONTROL_MUTE);
  g_variant_dict_insert (&d, "name", "s", "mute");
  g_variant_dict_insert (&d, "type", "s", "b");
  g_variant_dict_insert (&d, "default-value", "b", self->master_mute);
  wp_endpoint_register_control (WP_ENDPOINT (self), g_variant_dict_end (&d));

  self->selected = FALSE;
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "id", "u", CONTROL_SELECTED);
  g_variant_dict_insert (&d, "name", "s", "selected");
  g_variant_dict_insert (&d, "type", "s", "b");
  g_variant_dict_insert (&d, "default-value", "b", self->selected);
  wp_endpoint_register_control (WP_ENDPOINT (self), g_variant_dict_end (&d));

  G_OBJECT_CLASS (endpoint_parent_class)->constructed (object);
}

static void
endpoint_finalize (GObject * object)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);

  /* Unref the proxy node */
  if (self->proxy_node) {
    g_object_unref(self->proxy_node);
    self->proxy_node = NULL;
  }

  /* Unref the proxy port */
  if (self->proxy_port) {
    g_object_unref(self->proxy_port);
    self->proxy_port = NULL;
  }

  /* Clear the dsp info */
  if (self->dsp_info) {
    pw_node_info_free(self->dsp_info);
    self->dsp_info = NULL;
  }

  /* Destroy the dsp_proxy */
  if (self->dsp_proxy) {
    spa_hook_remove (&self->dsp_listener);
    pw_proxy_destroy ((struct pw_proxy *) self->dsp_proxy);
    self->dsp_proxy = NULL;
  }

  G_OBJECT_CLASS (endpoint_parent_class)->finalize (object);
}

static void
endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);

  switch (property_id) {
  case PROP_NODE_PROXY:
    g_clear_object(&self->proxy_node);
    self->proxy_node = g_value_get_object(value);
    break;
  case PROP_PORT_PROXY:
    g_clear_object(&self->proxy_port);
    self->proxy_port = g_value_get_object(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (object);

  switch (property_id) {
  case PROP_NODE_PROXY:
    g_value_set_object (value, self->proxy_node);
    break;
  case PROP_PORT_PROXY:
    g_value_set_object (value, self->proxy_port);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static GVariant *
endpoint_get_control_value (WpEndpoint * ep, guint32 control_id)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);

  switch (control_id) {
    case CONTROL_VOLUME:
      return g_variant_new_double (self->master_volume);
    case CONTROL_MUTE:
      return g_variant_new_boolean (self->master_mute);
    case CONTROL_SELECTED:
      return g_variant_new_boolean (self->selected);
    default:
      g_warning ("Unknown control id %u", control_id);
      return NULL;
  }
}

static gboolean
endpoint_set_control_value (WpEndpoint * ep, guint32 control_id,
    GVariant * value)
{
  WpPwAudioSoftdspEndpoint *self = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  float volume;
  bool mute;

  if (!self->dsp_proxy) {
    g_debug("WpEndpoint:%p too early to set control, dsp is not created yet",
        self);
    return FALSE;
  }

  switch (control_id) {
    case CONTROL_VOLUME:
      volume = g_variant_get_double (value);

      g_debug("WpEndpoint:%p set volume control (%u) value, vol:%f", self,
          control_id, volume);

      pw_node_proxy_set_param (self->dsp_proxy,
          SPA_PARAM_Props, 0,
          spa_pod_builder_add_object (&b,
              SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
              SPA_PROP_volume, SPA_POD_Float(volume),
              NULL));
      break;

    case CONTROL_MUTE:
      mute = g_variant_get_boolean (value);

      g_debug("WpEndpoint:%p set mute control (%u) value, mute:%d", self,
          control_id, mute);

      pw_node_proxy_set_param (self->dsp_proxy,
          SPA_PARAM_Props, 0,
          spa_pod_builder_add_object (&b,
              SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
              SPA_PROP_mute, SPA_POD_Bool(mute),
              NULL));
      break;

    case CONTROL_SELECTED:
      self->selected = g_variant_get_boolean (value);
      wp_endpoint_notify_control_value (ep, CONTROL_SELECTED);
      break;

    default:
      g_warning ("Unknown control id %u", control_id);
      return FALSE;
  }

  return TRUE;
}

static void
endpoint_init (WpPwAudioSoftdspEndpoint * self)
{
}

static void
endpoint_class_init (WpPwAudioSoftdspEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpEndpointClass *endpoint_class = (WpEndpointClass *) klass;

  object_class->constructed = endpoint_constructed;
  object_class->finalize = endpoint_finalize;
  object_class->set_property = endpoint_set_property;
  object_class->get_property = endpoint_get_property;

  endpoint_class->prepare_link = endpoint_prepare_link;
  endpoint_class->get_control_value = endpoint_get_control_value;
  endpoint_class->set_control_value = endpoint_set_control_value;

  g_object_class_install_property (object_class, PROP_NODE_PROXY,
      g_param_spec_object ("node-proxy", "node-proxy",
          "Pointer to the node proxy of the device", WP_TYPE_PROXY_NODE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_PORT_PROXY,
      g_param_spec_object ("port-proxy", "port-proxy",
          "Pointer to the port ptoxy of the device", WP_TYPE_PROXY_PORT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
handle_port(WpPwAudioSoftdspEndpoint *self, uint32_t id, uint32_t parent_id,
    const struct spa_dict *props)
{
  const char *direction_prop = NULL;
  enum pw_direction direction;

  /* Make sure the dsp port is not already set*/
  if (self->dsp_port_id != 0)
    return;

  /* Make sure the port has porperties */
  if (!props)
    return;

  /* Only handle ports owned by this endpoint */
  if (!self->dsp_info || self->dsp_info->id != parent_id)
    return;

  /* Get the direction property */
  direction_prop = spa_dict_lookup(props, "port.direction");
  if (!direction_prop)
    return;
  direction =
      !strcmp(direction_prop, "out") ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT;

  /* Only handle ports with the oposit direction of the endpoint */
  if (self->direction == direction)
    return;
  
  /* Set the dsp port id */
  self->dsp_port_id = id;
}

static void
registry_global(void *data, uint32_t id, uint32_t parent_id,
		uint32_t permissions, uint32_t type, uint32_t version,
		const struct spa_dict *props)
{
  WpPwAudioSoftdspEndpoint *self = data;

  switch (type) {
  case PW_TYPE_INTERFACE_Port:
    handle_port(self, id, parent_id, props);
    break;

  default:
    break;
  }
}

static const struct pw_registry_proxy_events registry_events = {
  PW_VERSION_REGISTRY_PROXY_EVENTS,
  .global = registry_global,
};

static gpointer
endpoint_factory (WpFactory * factory, GType type, GVariant * properties)
{
  WpCore *wp_core = NULL;
  struct pw_remote *remote;
  const gchar *name = NULL;
  const gchar *media_class = NULL;
  guint64 proxy_node, proxy_port;

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
  if (!g_variant_lookup (properties, "proxy-node", "t", &proxy_node))
      return NULL;
  if (!g_variant_lookup (properties, "proxy-port", "t", &proxy_port))
      return NULL;

  /* Create the softdsp endpoint object */
  WpPwAudioSoftdspEndpoint *ep = g_object_new (endpoint_get_type (),
      "name", name,
      "media-class", media_class,
      "node-proxy", (gpointer) proxy_node,
      "port-proxy", (gpointer) proxy_port,
      NULL);
  if (!ep)
    return NULL;

  /* Set the core proxy */
  ep->core_proxy = pw_remote_get_core_proxy(remote);
  if (!ep->core_proxy) {
    g_warning("failed to get core proxy. Skipping...");
    return NULL;
  }
  
  /* Add registry listener */
  ep->registry_proxy = pw_core_proxy_get_registry (ep->core_proxy,
      PW_TYPE_INTERFACE_Registry, PW_VERSION_REGISTRY, 0);
  pw_registry_proxy_add_listener(ep->registry_proxy, &ep->registry_listener,
      &registry_events, ep);

  /* Emit the audio DSP node */
  emit_audio_dsp_node(ep);

  /* Return the object */
  return ep;
}

static void
global_endpoint_notify_control_value (WpEndpoint * ep, guint control_id,
    WpCore * core)
{
  WpPwAudioSoftdspEndpoint *sdspep = WP_PW_AUDIO_SOFTDSP_ENDPOINT (ep);
  g_autoptr (GPtrArray) a = NULL;
  int i;

  /* when an endpoint becomes "selected", unselect
   * all other endpoints of the same media class */
  if (control_id == CONTROL_SELECTED && sdspep->selected) {
    g_debug ("selected: %p", ep);
    a = wp_endpoint_find (core, wp_endpoint_get_media_class (ep));

    for (i = 0; i < a->len; i++) {
      WpEndpoint *other = g_ptr_array_index (a, i);
      if (!WP_PW_IS_AUDIO_SOFTDSP_ENDPOINT (ep)
          || other == ep
          || !WP_PW_AUDIO_SOFTDSP_ENDPOINT (other)->selected)
        continue;

      g_debug ("unselecting %p", other);
      WP_PW_AUDIO_SOFTDSP_ENDPOINT (other)->selected = FALSE;
      wp_endpoint_notify_control_value (other, CONTROL_SELECTED);
    }
  }
}

static void
global_endpoint_added (WpCore *core, GQuark key, WpEndpoint *ep, gpointer data)
{
  if (WP_PW_IS_AUDIO_SOFTDSP_ENDPOINT (ep)) {
    g_debug ("connecting to notify-control-value for %p", ep);
    g_signal_connect (ep, "notify-control-value",
        (GCallback) global_endpoint_notify_control_value, core);
  }
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  /* Register the softdsp endpoint */
  wp_factory_new (core, "pw-audio-softdsp-endpoint", endpoint_factory);

  g_signal_connect (core, "global-added::endpoint",
      (GCallback) global_endpoint_added, NULL);
}
