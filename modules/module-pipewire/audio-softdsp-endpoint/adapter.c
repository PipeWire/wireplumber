/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/param/props.h>

#include "adapter.h"

enum {
  PROP_0,
  PROP_CONVERT,
};

struct _WpAudioAdapter
{
  WpAudioStream parent;

  /* Props */
  gboolean convert;
};

static GAsyncInitableIface *wp_audio_adapter_parent_interface = NULL;
static void wp_audio_adapter_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpAudioAdapter, wp_audio_adapter, WP_TYPE_AUDIO_STREAM,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_audio_adapter_async_initable_init))

static void
wp_audio_adapter_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpAudioAdapter *self = WP_AUDIO_ADAPTER(initable);
  enum pw_direction direction =
      wp_audio_stream_get_direction (WP_AUDIO_STREAM (self));
  uint8_t buf[1024];
  struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  struct spa_pod *param;
  struct spa_audio_info_raw fmt_raw;

  /* Call the parent interface */
  /* This will also augment the proxy and therefore bind it */
  wp_audio_adapter_parent_interface->init_async (initable, io_priority,
      cancellable, callback, data);

  /* Emit the ports */
  if (self->convert) {
    param = spa_pod_builder_add_object(&pod_builder,
        SPA_TYPE_OBJECT_ParamPortConfig,  SPA_PARAM_PortConfig,
        SPA_PARAM_PORT_CONFIG_direction,  SPA_POD_Id(direction),
        SPA_PARAM_PORT_CONFIG_mode,       SPA_POD_Id(SPA_PARAM_PORT_CONFIG_MODE_convert));
  } else {
    /* Use the default format */
    fmt_raw.format = SPA_AUDIO_FORMAT_F32P;
    fmt_raw.flags = 1;
    fmt_raw.rate = 48000;
    fmt_raw.channels = 2;
    fmt_raw.position[0] = SPA_AUDIO_CHANNEL_FL;
    fmt_raw.position[1] = SPA_AUDIO_CHANNEL_FR;
    param = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format, &fmt_raw);
    param = spa_pod_builder_add_object(&pod_builder,
        SPA_TYPE_OBJECT_ParamPortConfig,  SPA_PARAM_PortConfig,
        SPA_PARAM_PORT_CONFIG_direction,  SPA_POD_Id(direction),
        SPA_PARAM_PORT_CONFIG_mode,       SPA_POD_Id(SPA_PARAM_PORT_CONFIG_MODE_dsp),
        SPA_PARAM_PORT_CONFIG_format,     SPA_POD_Pod(param));
  }
  wp_audio_stream_set_port_config (WP_AUDIO_STREAM (self), param);
}

static void
wp_audio_adapter_async_initable_init (gpointer iface, gpointer iface_data)
{
  GAsyncInitableIface *ai_iface = iface;

  /* Set the parent interface */
  wp_audio_adapter_parent_interface = g_type_interface_peek_parent (iface);

  ai_iface->init_async = wp_audio_adapter_init_async;
}

static void
wp_audio_adapter_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpAudioAdapter *self = WP_AUDIO_ADAPTER (object);

  switch (property_id) {
  case PROP_CONVERT:
    self->convert = g_value_get_boolean(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_audio_adapter_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpAudioAdapter *self = WP_AUDIO_ADAPTER (object);

  switch (property_id) {
  case PROP_CONVERT:
    g_value_set_boolean (value, self->convert);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_audio_adapter_init (WpAudioAdapter * self)
{
}

static void
wp_audio_adapter_class_init (WpAudioAdapterClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->set_property = wp_audio_adapter_set_property;
  object_class->get_property = wp_audio_adapter_get_property;

  /* Install the properties */
  g_object_class_install_property (object_class, PROP_CONVERT,
      g_param_spec_boolean ("convert", "convert", "Do convert only or not",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
wp_audio_adapter_new (WpEndpoint *endpoint, guint stream_id,
    const char *stream_name, enum pw_direction direction, WpProxyNode *node,
    gboolean convert,  GAsyncReadyCallback callback, gpointer user_data)
{
  g_async_initable_new_async (
      WP_TYPE_AUDIO_ADAPTER, G_PRIORITY_DEFAULT, NULL, callback, user_data,
      "endpoint", endpoint,
      "id", stream_id,
      "name", stream_name,
      "direction", direction,
      "proxy-node", node,
      "convert", convert,
      NULL);
}
