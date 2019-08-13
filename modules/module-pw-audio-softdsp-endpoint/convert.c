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

  /* The task to signal the audio convert is initialized */
  GTask *init_task;
  gboolean init_abort;

  /* Props */
  const struct pw_node_info *target;

  /* Proxies */
  WpProxyNode *proxy;
  WpProxyLink *link_proxy;
};

static GAsyncInitableIface *wp_audio_convert_parent_interface = NULL;
static void wp_audio_convert_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpAudioConvert, wp_audio_convert, WP_TYPE_AUDIO_STREAM,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_audio_convert_async_initable_init))

typedef GObject* (*WpObjectNewFinishFunc)(GObject *initable, GAsyncResult *res,
    GError **error);

static GObject *
object_safe_new_finish(WpAudioConvert * self, GObject *initable,
    GAsyncResult *res, WpObjectNewFinishFunc new_finish_func)
{
  GObject *object = NULL;
  GError *error = NULL;

  /* Return NULL if we are already aborting */
  if (self->init_abort)
    return NULL;

  /* Get the object */
  object = G_OBJECT (new_finish_func (initable, res, &error));
  g_return_val_if_fail (object, NULL);

  /* Check for error */
  if (error) {
    g_clear_object (&object);
    g_warning ("WpAudioConvert:%p Aborting construction", self);
    self->init_abort = TRUE;
    g_task_return_error (self->init_task, error);
    g_clear_object (&self->init_task);
    return NULL;
  }

  return object;
}

static void
on_audio_convert_done(WpProxy *proxy, gpointer data)
{
  WpAudioConvert *self = data;

  /* Don't do anything if the endpoint has already been initialized */
  if (!self->init_task)
      return;

  /* Finish the creation of the audio convert */
  g_task_return_boolean (self->init_task, TRUE);
  g_clear_object(&self->init_task);
}

static void
on_proxy_link_created(GObject *initable, GAsyncResult *res, gpointer data)
{
  WpAudioConvert *self = data;

  /* Get the link */
  self->link_proxy = WP_PROXY_LINK (object_safe_new_finish (self, initable,
      res, (WpObjectNewFinishFunc)wp_proxy_link_new_finish));
  g_return_if_fail (self->link_proxy);
}

static void
on_audio_convert_running(WpAudioConvert *self, WpRemotePipewire *rp)
{
  enum pw_direction direction =
      wp_audio_stream_get_direction ( WP_AUDIO_STREAM (self));
  struct pw_properties *props;
  const struct pw_node_info *info = NULL;
  struct pw_proxy *proxy = NULL;

  /* Return if the node has already been linked */
  if (self->link_proxy)
    return;

  /* Get the info */
  info = wp_proxy_node_get_info(self->proxy);
  g_return_if_fail (info);

  /* Create new properties */
  props = pw_properties_new(NULL, NULL);

  /* Set the new properties */
  pw_properties_set(props, PW_KEY_LINK_PASSIVE, "true");
  if (direction == PW_DIRECTION_INPUT) {
    pw_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%d", info->id);
    pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%d", -1);
    pw_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%d", self->target->id);
    pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%d", -1);
  } else {
    pw_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%d", self->target->id);
    pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%d", -1);
    pw_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%d", info->id);
    pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%d", -1);
  }

  g_debug ("%p linking audio convert to target", self);

  /* Create the link */
  proxy = wp_remote_pipewire_create_object(rp, "link-factory",
      PW_TYPE_INTERFACE_Link, &props->dict);
  wp_proxy_link_new (pw_proxy_get_id(proxy), proxy, on_proxy_link_created,
      self);

  /* Clean up */
  pw_properties_free(props);
}

static void
on_audio_convert_idle (WpAudioConvert *self, WpRemotePipewire *rp)
{
  /* Clear the proxy */
  g_clear_object (&self->link_proxy);
}

static void
on_audio_convert_proxy_created(GObject *initable, GAsyncResult *res,
    gpointer data)
{
  WpAudioConvert *self = data;
  enum pw_direction direction =
      wp_audio_stream_get_direction ( WP_AUDIO_STREAM (self));
  struct pw_node_proxy *pw_proxy = NULL;
  struct spa_audio_info_raw format;
  uint8_t buf[1024];
  struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  struct spa_pod *param;

  /* Get the convert proxy */
  self->proxy = WP_PROXY_NODE (object_safe_new_finish (self, initable,
      res, (WpObjectNewFinishFunc)wp_proxy_node_new_finish));
  if (!self->proxy)
    return;

  /* Get the pipewire proxy */
  pw_proxy = wp_proxy_get_pw_proxy(WP_PROXY(self->proxy));
  g_return_if_fail (pw_proxy);

  /* Emit the props param */
  pw_node_proxy_enum_params (pw_proxy, 0, SPA_PARAM_Props, 0, -1, NULL);

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
  pw_node_proxy_set_param(pw_proxy, SPA_PARAM_PortConfig, 0, param);

  /* Register a callback to know when all the convert ports have been emitted */
  g_signal_connect_object(self->proxy, "done", (GCallback)on_audio_convert_done,
      self, 0);
  wp_proxy_sync (WP_PROXY(self->proxy));
}

static void
wp_audio_convert_event_info (WpAudioStream * as, gconstpointer i,
    WpRemotePipewire *rp)
{
  WpAudioConvert * self = WP_AUDIO_CONVERT (as);
  const struct pw_node_info *info = i;

  /* Handle the different states */
  switch (info->state) {
  case PW_NODE_STATE_IDLE:
    on_audio_convert_idle (self, rp);
    break;
  case PW_NODE_STATE_RUNNING:
    on_audio_convert_running (self, rp);
    break;
  case PW_NODE_STATE_SUSPENDED:
    break;
  default:
    break;
  }
}

static gpointer
wp_audio_convert_create_proxy (WpAudioStream * as, WpRemotePipewire *rp)
{
  WpAudioConvert * self = WP_AUDIO_CONVERT (as);
  const char *name = wp_audio_stream_get_name (as);
  struct pw_properties *props = NULL;
  struct pw_node_proxy *proxy = NULL;

  /* Create the properties */
  g_return_val_if_fail (self->target, NULL);
  props = pw_properties_new_dict(self->target->props);
  g_return_val_if_fail (props, NULL);
  pw_properties_set(props, PW_KEY_NODE_NAME, name);
  pw_properties_set(props, PW_KEY_MEDIA_CLASS, "Audio/Convert");
  pw_properties_set(props, "factory.name", SPA_NAME_AUDIO_CONVERT);

  /* Create the proxy async */
  proxy = wp_remote_pipewire_create_object(rp, "spa-node-factory",
      PW_TYPE_INTERFACE_Node, &props->dict);
  g_return_val_if_fail (proxy, NULL);
  wp_proxy_node_new(pw_proxy_get_id((struct pw_proxy *)proxy), proxy,
      on_audio_convert_proxy_created, self);

  /* Clean up */
  pw_properties_free(props);

  return proxy;
}

static gconstpointer
wp_audio_convert_get_info (WpAudioStream * as)
{
  WpAudioConvert * self = WP_AUDIO_CONVERT (as);

  /* Make sure proxy is valid */
  if (!self->proxy)
    return NULL;

  /* Return the info */
  return wp_proxy_node_get_info (self->proxy);
}

static void
wp_audio_convert_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpAudioConvert *self = WP_AUDIO_CONVERT (initable);

  /* Create the async task */
  self->init_task = g_task_new (initable, cancellable, callback, data);

  /* Call the parent interface */
  wp_audio_convert_parent_interface->init_async (initable, io_priority,
      cancellable, callback, data);
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
    self->target = g_value_get_pointer(value);
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
    g_value_set_pointer (value, (gpointer)self->target);
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

  /* Destroy the init task */
  g_clear_object(&self->init_task);

  /* Destroy the proxy */
  g_clear_object(&self->proxy);

  /* Destroy the link proxy */
  g_clear_object (&self->link_proxy);

  G_OBJECT_CLASS (wp_audio_convert_parent_class)->finalize (object);
}

static void
wp_audio_convert_init (WpAudioConvert * self)
{
  self->init_abort = FALSE;
}

static void
wp_audio_convert_class_init (WpAudioConvertClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpAudioStreamClass *audio_stream_class = (WpAudioStreamClass *) klass;

  object_class->finalize = wp_audio_convert_finalize;
  object_class->set_property = wp_audio_convert_set_property;
  object_class->get_property = wp_audio_convert_get_property;

  audio_stream_class->create_proxy = wp_audio_convert_create_proxy;
  audio_stream_class->get_info = wp_audio_convert_get_info;
  audio_stream_class->event_info = wp_audio_convert_event_info;

  /* Install the properties */
  g_object_class_install_property (object_class, PROP_TARGET,
      g_param_spec_pointer ("target", "target",
          "The target stream info",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
wp_audio_convert_new (WpEndpoint *endpoint, guint stream_id,
    const char *stream_name, enum pw_direction direction,
    const struct pw_node_info *target, GAsyncReadyCallback callback,
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

const struct pw_node_info *
wp_audio_convert_get_target (WpAudioConvert *self)
{
  return self->target;
}
