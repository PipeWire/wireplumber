/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>
#include <spa/utils/names.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/param/props.h>

#include "convert.h"

enum {
  PROP_0,
  PROP_TARGET,
};

struct _WpAudioConvert
{
  WpAudioStream parent;

  /* Props */
  WpProxyNode *target;

  /* Proxies */
  WpProxy *link_proxy;
};

static GAsyncInitableIface *wp_audio_convert_parent_interface = NULL;
static void wp_audio_convert_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpAudioConvert, wp_audio_convert, WP_TYPE_AUDIO_STREAM,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_audio_convert_async_initable_init))

static void
on_audio_convert_running(WpAudioConvert *self)
{
  WpRemotePipewire *rp = wp_audio_stream_get_remote (WP_AUDIO_STREAM (self));
  enum pw_direction direction =
      wp_audio_stream_get_direction (WP_AUDIO_STREAM (self));
  g_autoptr (WpProperties) props = NULL;
  const struct pw_node_info *info = NULL, *target_info = NULL;

  /* Return if the node has already been linked */
  if (self->link_proxy)
    return;

  /* Get the info */
  info = wp_audio_stream_get_info (WP_AUDIO_STREAM (self));
  g_return_if_fail (info);
  target_info = wp_proxy_node_get_info (self->target);
  g_return_if_fail (target_info);

  /* Create new properties */
  props = wp_properties_new_empty ();

  /* Set the new properties */
  wp_properties_set (props, PW_KEY_LINK_PASSIVE, "true");
  if (direction == PW_DIRECTION_INPUT) {
    wp_properties_setf (props, PW_KEY_LINK_OUTPUT_NODE, "%d", info->id);
    wp_properties_setf (props, PW_KEY_LINK_OUTPUT_PORT, "%d", -1);
    wp_properties_setf (props, PW_KEY_LINK_INPUT_NODE, "%d", target_info->id);
    wp_properties_setf (props, PW_KEY_LINK_INPUT_PORT, "%d", -1);
  } else {
    wp_properties_setf (props, PW_KEY_LINK_OUTPUT_NODE, "%d", target_info->id);
    wp_properties_setf (props, PW_KEY_LINK_OUTPUT_PORT, "%d", -1);
    wp_properties_setf (props, PW_KEY_LINK_INPUT_NODE, "%d", info->id);
    wp_properties_setf (props, PW_KEY_LINK_INPUT_PORT, "%d", -1);
  }

  g_debug ("%p linking audio convert to target", self);

  /* Create the link */
  self->link_proxy = wp_remote_pipewire_create_object (rp, "link-factory",
      PW_TYPE_INTERFACE_Link, PW_VERSION_LINK_PROXY, props);
}

static void
wp_audio_convert_event_info (WpProxyNode * proxy, GParamSpec *spec,
    WpAudioConvert * self)
{
  const struct pw_node_info *info = wp_proxy_node_get_info (proxy);

  /* Handle the different states */
  switch (info->state) {
  case PW_NODE_STATE_IDLE:
    g_clear_object (&self->link_proxy);
    break;
  case PW_NODE_STATE_RUNNING:
    on_audio_convert_running (self);
    break;
  case PW_NODE_STATE_SUSPENDED:
    break;
  default:
    break;
  }
}

static void
on_audio_convert_proxy_done (WpProxy *proxy, GAsyncResult *res,
    WpAudioConvert *self)
{
  g_autoptr (GError) error = NULL;
  enum pw_direction direction =
      wp_audio_stream_get_direction (WP_AUDIO_STREAM (self));
  struct spa_audio_info_raw format;
  uint8_t buf[1024];
  struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  struct spa_pod *param;

  wp_proxy_sync_finish (proxy, res, &error);
  if (error) {
    g_message("WpAudioConvert:%p initial sync failed: %s", self, error->message);
    wp_audio_stream_init_task_finish (WP_AUDIO_STREAM (self),
        g_steal_pointer (&error));
    return;
  }

  g_debug ("%s:%p setting format", G_OBJECT_TYPE_NAME (self), self);

  /* Use the default format */
  format.format = SPA_AUDIO_FORMAT_F32P;
  format.flags = 1;
  format.rate = 48000;
  format.channels = 2;
  format.position[0] = SPA_AUDIO_CHANNEL_FL;
  format.position[1] = SPA_AUDIO_CHANNEL_FR;

  /* Emit the ports */
  param = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format, &format);
  param = spa_pod_builder_add_object(&pod_builder,
      SPA_TYPE_OBJECT_ParamPortConfig,  SPA_PARAM_PortConfig,
      SPA_PARAM_PORT_CONFIG_direction,  SPA_POD_Id(direction),
      SPA_PARAM_PORT_CONFIG_mode,       SPA_POD_Id(SPA_PARAM_PORT_CONFIG_MODE_dsp),
      SPA_PARAM_PORT_CONFIG_format,     SPA_POD_Pod(param));

  wp_audio_stream_set_port_config (WP_AUDIO_STREAM (self), param);
}

static void
wp_audio_convert_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpAudioConvert *self = WP_AUDIO_CONVERT (initable);
  g_autoptr (WpProxy) proxy = NULL;
  g_autoptr (WpProperties) props = NULL;
  WpRemotePipewire *remote =
      wp_audio_stream_get_remote (WP_AUDIO_STREAM (self));

  /* Create the properties */
  props = wp_properties_copy (wp_proxy_node_get_properties (self->target));
  wp_properties_set (props, PW_KEY_NODE_NAME,
      wp_audio_stream_get_name (WP_AUDIO_STREAM (self)));
  wp_properties_set (props, PW_KEY_MEDIA_CLASS, "Audio/Convert");
  wp_properties_set (props, "factory.name", SPA_NAME_AUDIO_CONVERT);

  /* Create the proxy */
  proxy = wp_remote_pipewire_create_object (remote, "spa-node-factory",
      PW_TYPE_INTERFACE_Node, PW_VERSION_NODE_PROXY, props);
  g_return_if_fail (proxy);

  g_object_set (self, "proxy-node", proxy, NULL);
  g_signal_connect_object (proxy, "notify::info",
      (GCallback) wp_audio_convert_event_info, self, 0);

  /* Call the parent interface */
  wp_audio_convert_parent_interface->init_async (initable, io_priority,
      cancellable, callback, data);

  /* Register a callback to be called after all the initialization is done */
  wp_proxy_sync (proxy, NULL,
      (GAsyncReadyCallback) on_audio_convert_proxy_done, self);
}

static void
wp_audio_convert_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  /* Set the parent interface */
  wp_audio_convert_parent_interface = g_type_interface_peek_parent (iface);

  ai_iface->init_async = wp_audio_convert_init_async;
}

static void
wp_audio_convert_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpAudioConvert *self = WP_AUDIO_CONVERT (object);

  switch (property_id) {
  case PROP_TARGET:
    self->target = g_value_dup_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_audio_convert_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpAudioConvert *self = WP_AUDIO_CONVERT (object);

  switch (property_id) {
  case PROP_TARGET:
    g_value_set_object (value, self->target);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_audio_convert_finalize (GObject * object)
{
  WpAudioConvert *self = WP_AUDIO_CONVERT (object);

  g_clear_object (&self->link_proxy);
  g_clear_object (&self->target);

  G_OBJECT_CLASS (wp_audio_convert_parent_class)->finalize (object);
}

static void
wp_audio_convert_init (WpAudioConvert * self)
{
}

static void
wp_audio_convert_class_init (WpAudioConvertClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_audio_convert_finalize;
  object_class->set_property = wp_audio_convert_set_property;
  object_class->get_property = wp_audio_convert_get_property;

  /* Install the properties */
  g_object_class_install_property (object_class, PROP_TARGET,
      g_param_spec_object ("target", "target", "The target device node",
          WP_TYPE_PROXY_NODE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
wp_audio_convert_new (WpEndpoint *endpoint, guint stream_id,
    const char *stream_name, enum pw_direction direction,
    WpProxyNode *target, GAsyncReadyCallback callback,
    gpointer user_data)
{
  g_async_initable_new_async (
      WP_TYPE_AUDIO_CONVERT, G_PRIORITY_DEFAULT, NULL, callback, user_data,
      "endpoint", endpoint,
      "id", stream_id,
      "name", stream_name,
      "direction", direction,
      "target", target,
      NULL);
}
