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
  PROP_ADAPTER_ID,
  PROP_CONVERT,
};

struct _WpAudioAdapter
{
  WpAudioStream parent;

  /* The task to signal the proxy is initialized */
  GTask *init_task;
  gboolean init_abort;
  gboolean ports_done;

  /* Props */
  guint adapter_id;
  gboolean convert;

  /* Proxies */
  WpProxyNode *proxy;
};

static GAsyncInitableIface *wp_audio_adapter_parent_interface = NULL;
static void wp_audio_adapter_async_initable_init (gpointer iface,
    gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (WpAudioAdapter, wp_audio_adapter, WP_TYPE_AUDIO_STREAM,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                           wp_audio_adapter_async_initable_init))

typedef GObject* (*WpObjectNewFinishFunc)(GObject *initable, GAsyncResult *res,
    GError **error);

static GObject *
object_safe_new_finish(WpAudioAdapter * self, GObject *initable,
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
    g_warning ("WpAudioAdapter:%p Aborting construction", self);
    self->init_abort = TRUE;
    g_task_return_error (self->init_task, error);
    g_clear_object (&self->init_task);
    return NULL;
  }

  return object;
}

static void
on_audio_adapter_done(WpProxy *proxy, gpointer data)
{
  WpAudioAdapter *self = data;

  /* Emit the ports if not done and sync again */
  if (!self->ports_done) {
    enum pw_direction direction =
      wp_audio_stream_get_direction ( WP_AUDIO_STREAM (self));
    struct pw_node_proxy *pw_proxy = NULL;
    uint8_t buf[1024];
    struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    struct spa_pod *param;

    /* Emit the props param */
    pw_proxy = wp_proxy_get_pw_proxy(WP_PROXY(self->proxy));
    pw_node_proxy_enum_params (pw_proxy, 0, SPA_PARAM_Props, 0, -1, NULL);

    /* Emit the ports */
    if (self->convert) {
      param = spa_pod_builder_add_object(&pod_builder,
          SPA_TYPE_OBJECT_ParamPortConfig,  SPA_PARAM_PortConfig,
          SPA_PARAM_PORT_CONFIG_direction,  SPA_POD_Id(direction),
          SPA_PARAM_PORT_CONFIG_mode,       SPA_POD_Id(SPA_PARAM_PORT_CONFIG_MODE_convert));
    } else {
      struct spa_audio_info_raw format = *wp_proxy_node_get_format (self->proxy);
      param = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_Format, &format);
      param = spa_pod_builder_add_object(&pod_builder,
          SPA_TYPE_OBJECT_ParamPortConfig,  SPA_PARAM_PortConfig,
          SPA_PARAM_PORT_CONFIG_direction,  SPA_POD_Id(direction),
          SPA_PARAM_PORT_CONFIG_mode,       SPA_POD_Id(SPA_PARAM_PORT_CONFIG_MODE_dsp),
          SPA_PARAM_PORT_CONFIG_format,     SPA_POD_Pod(param));
    }
    pw_node_proxy_set_param(pw_proxy, SPA_PARAM_PortConfig, 0, param);

    /* Sync */
    self->ports_done = TRUE;
    wp_proxy_sync (WP_PROXY(self->proxy));
    return;
  }

  /* Don't do anything if the audio adapter has already been initialized */
  if (!self->init_task)
    return;

  /* Finish the creation of the audio adapter */
  g_task_return_boolean (self->init_task, TRUE);
  g_clear_object(&self->init_task);
}

static void
on_audio_adapter_proxy_created(GObject *initable, GAsyncResult *res,
    gpointer data)
{
  WpAudioAdapter *self = data;

  /* Get the adapter proxy */
  self->proxy = WP_PROXY_NODE (object_safe_new_finish (self, initable,
      res, (WpObjectNewFinishFunc)wp_proxy_node_new_finish));
  if (!self->proxy)
    return;

  /* Emit the EnumFormat param */
  wp_proxy_node_enum_params (self->proxy, 0, SPA_PARAM_EnumFormat, 0, -1, NULL);

  /* Register the done callback */
  g_signal_connect_object(self->proxy, "done", (GCallback)on_audio_adapter_done,
      self, 0);
  wp_proxy_sync (WP_PROXY(self->proxy));
}

static gpointer
wp_audio_adapter_create_proxy (WpAudioStream * as, WpRemotePipewire *rp)
{
  WpAudioAdapter * self = WP_AUDIO_ADAPTER (as);
  struct pw_node_proxy *proxy = NULL;

  /* Create the adapter proxy by binding it */
  proxy = wp_remote_pipewire_proxy_bind (rp, self->adapter_id,
      PW_TYPE_INTERFACE_Node);
  g_return_val_if_fail (proxy, NULL);
  wp_proxy_node_new(self->adapter_id, proxy, on_audio_adapter_proxy_created,
      self);

  return proxy;
}

static gconstpointer
wp_audio_adapter_get_info (WpAudioStream * as)
{
  WpAudioAdapter * self = WP_AUDIO_ADAPTER (as);
  g_return_val_if_fail (self->proxy, NULL);

  /* Return the info */
  return wp_proxy_node_get_info (self->proxy);
}

static void
wp_audio_adapter_init_async (GAsyncInitable *initable, int io_priority,
    GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data)
{
  WpAudioAdapter *self = WP_AUDIO_ADAPTER(initable);

  /* Create the async task */
  self->init_task = g_task_new (initable, cancellable, callback, data);

  /* Call the parent interface */
  wp_audio_adapter_parent_interface->init_async (initable, io_priority,
      cancellable, callback, data);
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
  case PROP_ADAPTER_ID:
    self->adapter_id = g_value_get_uint(value);
    break;
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
  case PROP_ADAPTER_ID:
    g_value_set_uint (value, self->adapter_id);
    break;
  case PROP_CONVERT:
    g_value_set_boolean (value, self->convert);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_audio_adapter_finalize (GObject * object)
{
  WpAudioAdapter *self = WP_AUDIO_ADAPTER(object);

  /* Destroy the proxy */
  g_clear_object(&self->proxy);

  G_OBJECT_CLASS (wp_audio_adapter_parent_class)->finalize (object);
}

static void
wp_audio_adapter_init (WpAudioAdapter * self)
{
  self->init_abort = FALSE;
  self->ports_done = FALSE;
}

static void
wp_audio_adapter_class_init (WpAudioAdapterClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpAudioStreamClass *audio_stream_class = (WpAudioStreamClass *) klass;

  object_class->finalize = wp_audio_adapter_finalize;
  object_class->set_property = wp_audio_adapter_set_property;
  object_class->get_property = wp_audio_adapter_get_property;

  audio_stream_class->create_proxy = wp_audio_adapter_create_proxy;
  audio_stream_class->get_info = wp_audio_adapter_get_info;

  /* Install the properties */
  g_object_class_install_property (object_class, PROP_ADAPTER_ID,
      g_param_spec_uint ("adapter-id", "adapter-id", "The Id of the adapter", 0,
          G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_CONVERT,
      g_param_spec_boolean ("convert", "convert", "Do convert only or not",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
wp_audio_adapter_new (WpEndpoint *endpoint, guint stream_id,
    const char *stream_name, enum pw_direction direction, guint adapter_id,
    gboolean convert,  GAsyncReadyCallback callback, gpointer user_data)
{
  g_async_initable_new_async (
      WP_TYPE_AUDIO_ADAPTER, G_PRIORITY_DEFAULT, NULL, callback, user_data,
      "endpoint", endpoint,
      "id", stream_id,
      "name", stream_name,
      "direction", direction,
      "adapter-id", adapter_id,
      "convert", convert,
      NULL);
}

guint
wp_audio_adapter_get_adapter_id (WpAudioAdapter *self)
{
  return self->adapter_id;
}

gboolean
wp_audio_adapter_is_convert (WpAudioAdapter *self)
{
  return self->convert;
}
