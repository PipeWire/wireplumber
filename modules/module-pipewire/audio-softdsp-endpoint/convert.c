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
#include "../algorithms.h"

enum {
  PROP_0,
  PROP_TARGET,
  PROP_FORMAT,
};

struct _WpAudioConvert
{
  WpAudioStream parent;

  /* Props */
  WpAudioStream *target;
  struct spa_audio_info_raw format;

  /* Proxies */
  GPtrArray *link_proxies;
};

static GAsyncInitableIface *wp_audio_convert_parent_interface = NULL;
static void wp_audio_convert_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpAudioConvert, wp_audio_convert, WP_TYPE_AUDIO_STREAM,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_audio_convert_async_initable_init))

static void
create_link_cb (WpProperties *props, gpointer user_data)
{
  WpAudioConvert *self = WP_AUDIO_CONVERT (user_data);
  g_autoptr (WpCore) core = NULL;
  WpProxy *proxy;

  core = wp_audio_stream_get_core (WP_AUDIO_STREAM (self));
  g_return_if_fail (core);

  /* make the link passive, which means it will not keep
     the audioconvert node in the running state if the number of non-passive
     links (i.e. the ones linking another endpoint to this one) drops to 0 */
  wp_properties_set (props, PW_KEY_LINK_PASSIVE, "1");

  /* Create the link */
  proxy = wp_core_create_remote_object(core, "link-factory",
      PW_TYPE_INTERFACE_Link, PW_VERSION_LINK_PROXY, props);
  g_return_if_fail (proxy);
  g_ptr_array_add(self->link_proxies, proxy);
}

static void
on_audio_convert_running(WpAudioConvert *self)
{
  g_autoptr (GVariant) src_props = NULL;
  g_autoptr (GVariant) sink_props = NULL;
  g_autoptr (GError) error = NULL;
  enum pw_direction direction =
      wp_audio_stream_get_direction (WP_AUDIO_STREAM (self));

  g_debug ("%p linking audio convert to target", self);

  if (direction == PW_DIRECTION_INPUT) {
    wp_audio_stream_prepare_link (WP_AUDIO_STREAM (self), &src_props, &error);
    wp_audio_stream_prepare_link (self->target, &sink_props, &error);
  } else {
    wp_audio_stream_prepare_link (self->target, &src_props, &error);
    wp_audio_stream_prepare_link (WP_AUDIO_STREAM (self), &sink_props, &error);
  }

  multiport_link_create (src_props, sink_props, create_link_cb, self, &error);
}

static void
wp_audio_convert_event_info (WpProxyNode * proxy, GParamSpec *spec,
    WpAudioConvert * self)
{
  const struct pw_node_info *info = wp_proxy_node_get_info (proxy);

  /* Handle the different states */
  switch (info->state) {
  case PW_NODE_STATE_IDLE:
    g_ptr_array_set_size (self->link_proxies, 0);
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
  uint8_t buf[1024];
  struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  struct spa_pod *format;
  struct spa_pod *param;

  wp_proxy_sync_finish (proxy, res, &error);
  if (error) {
    g_message("WpAudioConvert:%p initial sync failed: %s", self, error->message);
    wp_audio_stream_init_task_finish (WP_AUDIO_STREAM (self),
        g_steal_pointer (&error));
    return;
  }

  g_debug ("%s:%p setting format", G_OBJECT_TYPE_NAME (self), self);

  format = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format,
      &self->format);

  /* Configure audioconvert to be both merger and splitter; this means it will
     have an equal number of input and output ports and just passthrough the
     same format, but with altered volume.
     In the future we need to consider writing a simpler volume node for this,
     as doing merge + split is heavy for our needs */
  param = spa_pod_builder_add_object(&pod_builder,
      SPA_TYPE_OBJECT_ParamPortConfig,  SPA_PARAM_PortConfig,
      SPA_PARAM_PORT_CONFIG_direction,  SPA_POD_Id(pw_direction_reverse(direction)),
      SPA_PARAM_PORT_CONFIG_mode,       SPA_POD_Id(SPA_PARAM_PORT_CONFIG_MODE_dsp),
      SPA_PARAM_PORT_CONFIG_format,     SPA_POD_Pod(format));
  wp_audio_stream_set_port_config (WP_AUDIO_STREAM (self), param);

  param = spa_pod_builder_add_object(&pod_builder,
      SPA_TYPE_OBJECT_ParamPortConfig,  SPA_PARAM_PortConfig,
      SPA_PARAM_PORT_CONFIG_direction,  SPA_POD_Id(direction),
      SPA_PARAM_PORT_CONFIG_mode,       SPA_POD_Id(SPA_PARAM_PORT_CONFIG_MODE_dsp),
      SPA_PARAM_PORT_CONFIG_format,     SPA_POD_Pod(format));
  wp_audio_stream_set_port_config (WP_AUDIO_STREAM (self), param);
  wp_audio_stream_finish_port_config (WP_AUDIO_STREAM (self));
}

static void
wp_audio_convert_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpAudioConvert *self = WP_AUDIO_CONVERT (initable);
  g_autoptr (WpProxy) proxy = NULL;
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (WpCore) core = wp_audio_stream_get_core (WP_AUDIO_STREAM (self));
  WpProxyNode *node;

  /* Create the properties */
  node = wp_audio_stream_get_proxy_node (self->target);
  props = wp_properties_copy (wp_proxy_node_get_properties (node));

  wp_properties_setf (props, PW_KEY_OBJECT_PATH, "%s:%s",
      wp_properties_get(props, PW_KEY_OBJECT_PATH),
      wp_audio_stream_get_name (WP_AUDIO_STREAM (self)));
  wp_properties_setf (props, PW_KEY_NODE_NAME, "%s/%s/%s",
      SPA_NAME_AUDIO_CONVERT,
      wp_properties_get(props, PW_KEY_NODE_NAME),
      wp_audio_stream_get_name (WP_AUDIO_STREAM (self)));
  wp_properties_set (props, PW_KEY_MEDIA_CLASS, "Audio/Convert");
  wp_properties_set (props, "factory.name", SPA_NAME_AUDIO_CONVERT);

  /* Create the proxy */
  proxy = wp_core_create_remote_object (core, "spa-node-factory",
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
  case PROP_FORMAT: {
    const struct spa_audio_info_raw *f = g_value_get_pointer (value);
    if (f)
      self->format = *f;
    else
      g_warning ("WpAudioConvert:%p Format needs to be valid", self);
    break;
  }
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
  case PROP_FORMAT:
    g_value_set_pointer (value, &self->format);
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

  g_clear_pointer (&self->link_proxies, g_ptr_array_unref);
  g_clear_object (&self->target);

  G_OBJECT_CLASS (wp_audio_convert_parent_class)->finalize (object);
}

static void
wp_audio_convert_init (WpAudioConvert * self)
{
  self->link_proxies = g_ptr_array_new_with_free_func (g_object_unref);
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
      g_param_spec_object ("target", "target", "The target stream",
          WP_TYPE_AUDIO_STREAM,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_FORMAT,
      g_param_spec_pointer ("format", "format", "The accepted format",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
wp_audio_convert_new (WpEndpoint *endpoint, guint stream_id,
    const char *stream_name, enum pw_direction direction,
    WpAudioStream *target, const struct spa_audio_info_raw *format,
    GAsyncReadyCallback callback, gpointer user_data)
{
  g_async_initable_new_async (
      WP_TYPE_AUDIO_CONVERT, G_PRIORITY_DEFAULT, NULL, callback, user_data,
      "endpoint", endpoint,
      "id", stream_id,
      "name", stream_name,
      "direction", direction,
      "target", target,
      "format", format,
      NULL);
}
