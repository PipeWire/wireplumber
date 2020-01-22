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
#include "../algorithms.h"

enum {
  PROP_0,
  PROP_CONVERT,
};

struct _WpAudioAdapter
{
  WpAudioStream parent;

  /* Props */
  gboolean convert;

  /* THe raw format this adapter is configured */
  struct spa_audio_info_raw format;
};

static GAsyncInitableIface *wp_audio_adapter_parent_interface = NULL;
static void wp_audio_adapter_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpAudioAdapter, wp_audio_adapter, WP_TYPE_AUDIO_STREAM,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_audio_adapter_async_initable_init))

static void
on_proxy_enum_format_done (WpProxy *proxy, GAsyncResult *res,
    WpAudioAdapter *self)
{
  g_autoptr (GPtrArray) formats = NULL;
  g_autoptr (GError) error = NULL;
  enum pw_direction direction =
      wp_audio_stream_get_direction (WP_AUDIO_STREAM (self));
  uint8_t buf[1024];
  struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  struct spa_pod *param;

  formats = wp_proxy_enum_params_collect_finish (proxy, res, &error);
  if (error) {
    g_message("WpAudioAdapter:%p enum format error: %s", self, error->message);
    wp_audio_stream_init_task_finish (WP_AUDIO_STREAM (self),
        g_steal_pointer (&error));
    return;
  }

  if (!choose_sensible_raw_audio_format (formats, &self->format)) {
    uint32_t media_type, media_subtype;

    g_warning ("WpAudioAdapter:%p failed to choose a sensible audio format",
      self);

    /* fall back to spa_pod_fixate */
    if (formats->len == 0 ||
        !(param = g_ptr_array_index (formats, 0)) ||
        spa_format_parse (param, &media_type, &media_subtype) < 0 ||
        media_type != SPA_MEDIA_TYPE_audio ||
        media_subtype != SPA_MEDIA_SUBTYPE_raw) {
      g_message("WpAudioAdapter:%p node does not support audio/raw format", self);
      wp_audio_stream_init_task_finish (WP_AUDIO_STREAM (self),
          g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
              "node does not support audio/raw format"));
      return;
    }

    spa_pod_fixate (param);
    spa_format_audio_raw_parse (param, &self->format);
  }

  /* set the chosen device/client format on the node */
  param = spa_format_audio_raw_build (&pod_builder, SPA_PARAM_Format,
      &self->format);
  wp_proxy_set_param (proxy, SPA_PARAM_Format, 0, param);

  /* now choose the DSP format: keep the chanels but use F32 plannar @ 48K */
  self->format.format = SPA_AUDIO_FORMAT_F32P;
  self->format.rate = 48000;

  if (self->convert) {
    param = spa_pod_builder_add_object(&pod_builder,
        SPA_TYPE_OBJECT_ParamPortConfig,  SPA_PARAM_PortConfig,
        SPA_PARAM_PORT_CONFIG_direction,  SPA_POD_Id(direction),
        SPA_PARAM_PORT_CONFIG_mode,       SPA_POD_Id(SPA_PARAM_PORT_CONFIG_MODE_convert));
  } else {
    param = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format, &self->format);
    param = spa_pod_builder_add_object(&pod_builder,
        SPA_TYPE_OBJECT_ParamPortConfig,  SPA_PARAM_PortConfig,
        SPA_PARAM_PORT_CONFIG_direction,  SPA_POD_Id(direction),
        SPA_PARAM_PORT_CONFIG_mode,       SPA_POD_Id(SPA_PARAM_PORT_CONFIG_MODE_dsp),
        SPA_PARAM_PORT_CONFIG_format,     SPA_POD_Pod(param));
  }

  wp_audio_stream_set_port_config (WP_AUDIO_STREAM (self), param);
  wp_audio_stream_finish_port_config (WP_AUDIO_STREAM (self));
}

static void
wp_audio_adapter_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpAudioAdapter *self = WP_AUDIO_ADAPTER(initable);
  WpNode *proxy = wp_audio_stream_get_node (WP_AUDIO_STREAM (self));

  /* Call the parent interface */
  /* This will also augment the proxy and therefore bind it */
  wp_audio_adapter_parent_interface->init_async (initable, io_priority,
      cancellable, callback, data);

  wp_proxy_enum_params_collect (WP_PROXY (proxy), SPA_PARAM_EnumFormat, 0, -1,
      NULL, NULL, (GAsyncReadyCallback) on_proxy_enum_format_done, self);
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
wp_audio_adapter_new (WpBaseEndpoint *endpoint, guint stream_id,
    const char *stream_name, enum pw_direction direction, WpNode *node,
    gboolean convert,  GAsyncReadyCallback callback, gpointer user_data)
{
  g_async_initable_new_async (
      WP_TYPE_AUDIO_ADAPTER, G_PRIORITY_DEFAULT, NULL, callback, user_data,
      "endpoint", endpoint,
      "id", stream_id,
      "name", stream_name,
      "direction", direction,
      "node", node,
      "convert", convert,
      NULL);
}

struct spa_audio_info_raw *
wp_audio_adapter_get_format (WpAudioAdapter *self)
{
  g_return_val_if_fail (self, NULL);
  return &self->format;
}
