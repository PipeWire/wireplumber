/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/keys.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("m-default-streams-api")

/*
 * Module Provides the APIs to query the default device streams. Module looks at
 * the default metadata to know the default devices, which are given as the
 * name of the node and the port group of the stream.
 */

enum {
  AUDIO_SINK,
  AUDIO_SOURCE,
  VIDEO_SOURCE,
  N_DEFAULT_STREAMS
};

static const gchar * DEFAULT_KEY[N_DEFAULT_STREAMS] = {
  [AUDIO_SINK] = "default.audio.sink",
  [AUDIO_SOURCE] = "default.audio.source",
  [VIDEO_SOURCE] = "default.video.source",
};

static const gchar * DEFAULT_TYPE_STR[N_DEFAULT_STREAMS] = {
  [AUDIO_SINK] = "Audio/Sink",
  [AUDIO_SOURCE] = "Audio/Source",
  [VIDEO_SOURCE] = "Video/Source",
};

static const gchar * DEFAULT_CONFIG_KEY[N_DEFAULT_STREAMS] = {
  [AUDIO_SINK] = "default.configured.audio.sink",
  [AUDIO_SOURCE] = "default.configured.audio.source",
  [VIDEO_SOURCE] = "default.configured.video.source",
};


typedef struct _WpDefaultStream WpDefaultStream;
struct _WpDefaultStream
{
  gchar *node_name;
  gchar *stream_name;
  gchar *config_node_name;
  gchar *config_stream_name;
};

struct _WpDefaultStreamsApi
{
  WpPlugin parent;

  WpDefaultStream defaults[N_DEFAULT_STREAMS];
  WpObjectManager *om;
};

enum {
  ACTION_GET_DEFAULT_STREAM,
  ACTION_GET_DEFAULT_CONFIGURED_NODE_NAME,
  ACTION_GET_DEFAULT_CONFIGURED_STREAM_NAME,
  ACTION_SET_DEFAULT_CONFIGURED_STREAM,
  SIGNAL_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

G_DECLARE_FINAL_TYPE (WpDefaultStreamsApi, wp_default_streams_api,
                      WP, DEFAULT_STREAMS_API, WpPlugin)
G_DEFINE_TYPE (WpDefaultStreamsApi, wp_default_streams_api, WP_TYPE_PLUGIN)

static void
wp_default_streams_api_init (WpDefaultStreamsApi * self)
{
}

static void
sync_changed_notification (WpCore * core, GAsyncResult * res,
    WpDefaultStreamsApi * self)
{
  g_autoptr (GError) error = NULL;
  if (!wp_core_sync_finish (core, res, &error)) {
    wp_warning_object (self, "core sync error: %s", error->message);
    return;
  }

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
  return;
}

static void
schedule_changed_notification (WpDefaultStreamsApi *self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);
  wp_core_sync_closure (core, NULL, g_cclosure_new_object (
      G_CALLBACK (sync_changed_notification), G_OBJECT (self)));
}

static void
on_metadata_changed (WpMetadata *m, guint32 subject,
    const gchar *key, const gchar *type, const gchar *value, gpointer d)
{
  WpDefaultStreamsApi * self = WP_DEFAULT_STREAMS_API (d);
  gchar *new_value = NULL;
  gchar *new_stream = NULL;

  if (subject != 0)
    return;

  for (gint i = 0; i < N_DEFAULT_STREAMS; i++) {
    if (!g_strcmp0 (key, DEFAULT_KEY[i])) {

      if (value && !g_strcmp0 (type, "Spa:String:JSON")) {
        g_autoptr (WpSpaJson) json = wp_spa_json_new_wrap_string (value);
        wp_spa_json_object_get (json, "name", "s", &new_value, NULL);
        wp_spa_json_object_get (json, "stream", "s", &new_stream, NULL);
      }

      wp_debug_object (m, "'%s' changed from '%s' (stream '%s') -> '%s' "
          "(stream '%s')", key, self->defaults[i].node_name,
          self->defaults[i].stream_name, new_value, new_stream);

      g_clear_pointer (&self->defaults[i].node_name, g_free);
      g_clear_pointer (&self->defaults[i].stream_name, g_free);
      self->defaults[i].node_name = new_value;
      self->defaults[i].stream_name = new_stream;

      schedule_changed_notification (self);
      break;
    } else if (!g_strcmp0 (key, DEFAULT_CONFIG_KEY[i])) {

      if (value && !g_strcmp0 (type, "Spa:String:JSON")) {
        g_autoptr (WpSpaJson) json = wp_spa_json_new_wrap_string (value);
        wp_spa_json_object_get (json, "name", "s", &new_value, NULL);
        wp_spa_json_object_get (json, "stream", "s", &new_stream, NULL);
      }

      wp_debug_object (m, "'%s' changed from '%s' (stream '%s') -> '%s' "
          "(stream '%s')", key, self->defaults[i].config_node_name,
          self->defaults[i].config_stream_name, new_value, new_stream);

      g_clear_pointer (&self->defaults[i].config_node_name, g_free);
      g_clear_pointer (&self->defaults[i].config_stream_name, g_free);
      self->defaults[i].config_node_name = new_value;
      self->defaults[i].config_stream_name = new_stream;
      break;
    }
  }
}

static void
on_metadata_added (WpObjectManager *om, WpObject *obj,
    WpDefaultStreamsApi * self)
{
  if (WP_IS_METADATA (obj)) {
    g_autoptr (WpIterator) it = wp_metadata_new_iterator (WP_METADATA (obj), 0);
    g_auto (GValue) val = G_VALUE_INIT;

    for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      WpMetadataItem *mi = g_value_get_boxed (&val);
      guint32 subject = wp_metadata_item_get_subject (mi);
      const gchar *key = wp_metadata_item_get_key (mi);
      const gchar *type = wp_metadata_item_get_value_type (mi);
      const gchar *value = wp_metadata_item_get_value (mi);
      on_metadata_changed (WP_METADATA (obj), subject, key, type, value, self);
    }

    g_signal_connect_object (obj, "changed",
        G_CALLBACK (on_metadata_changed), self, 0);
  }
}

static void
on_om_installed (WpObjectManager * om, WpDefaultStreamsApi * self)
{
  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_default_streams_api_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpDefaultStreamsApi * self = WP_DEFAULT_STREAMS_API (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);

  /* Create the metadata object manager */
  self->om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->om, WP_TYPE_METADATA,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s", "default",
      NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_STREAM, NULL);
  wp_object_manager_request_object_features (self->om,
      WP_TYPE_METADATA, WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->om, "object-added",
      G_CALLBACK (on_metadata_added), self, 0);
  g_signal_connect_object (self->om, "installed",
      G_CALLBACK (on_om_installed), self, 0);
  wp_core_install_object_manager (core, self->om);
}

static void
wp_default_streams_api_disable (WpPlugin * plugin)
{
  WpDefaultStreamsApi * self = WP_DEFAULT_STREAMS_API (plugin);

  for (guint i = 0; i < N_DEFAULT_STREAMS; i++) {
    g_clear_pointer (&self->defaults[i].node_name, g_free);
    g_clear_pointer (&self->defaults[i].stream_name, g_free);
    g_clear_pointer (&self->defaults[i].config_node_name, g_free);
    g_clear_pointer (&self->defaults[i].config_stream_name, g_free);
  }
  g_clear_object (&self->om);
}

static gint
get_default_index (const gchar * media_class)
{
  for (gint i = 0; i < N_DEFAULT_STREAMS; i++)
    if (!g_strcmp0 (media_class, DEFAULT_TYPE_STR[i]))
      return i;
  return -1;
}

/* A default audio source can also be the monitor of a sink */
static gboolean
is_default_shape (gint i, WpStreamShape shape)
{
  switch (i) {
    case AUDIO_SINK:
      return shape == WP_STREAM_SHAPE_SINK;
    case AUDIO_SOURCE:
      return shape == WP_STREAM_SHAPE_SOURCE || shape == WP_STREAM_SHAPE_SINK;
    case VIDEO_SOURCE:
      return shape == WP_STREAM_SHAPE_SOURCE;
    default:
      return FALSE;
  }
}

/* Gets the stream of the default node with the default port group, or the
 * first stream of the default node if there is no such stream */
static WpStream *
wp_default_streams_api_get_default_stream (WpDefaultStreamsApi * self,
    const gchar * media_class)
{
  gint i = get_default_index (media_class);
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  WpStream *fallback = NULL;

  if (i == -1 || !self->defaults[i].node_name)
    return NULL;

  it = wp_object_manager_new_filtered_iterator (self->om,
      i == VIDEO_SOURCE ? WP_TYPE_VIDEO_STREAM : WP_TYPE_AUDIO_STREAM,
      WP_CONSTRAINT_TYPE_WP_PROPERTY,
      PW_KEY_NODE_NAME, "=s", self->defaults[i].node_name,
      NULL);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpStream *stream = g_value_get_object (&val);

    if (!is_default_shape (i, wp_stream_get_shape (stream)))
      continue;

    if (!self->defaults[i].stream_name ||
        !g_strcmp0 (self->defaults[i].stream_name,
            wp_stream_get_port_group (stream))) {
      g_clear_object (&fallback);
      return g_object_ref (stream);
    }

    if (!fallback)
      fallback = g_object_ref (stream);
  }

  return fallback;
}

static gchar *
wp_default_streams_api_get_default_configured_node_name (
    WpDefaultStreamsApi * self, const gchar * media_class)
{
  for (gint i = 0; i < N_DEFAULT_STREAMS; i++)
    if (!g_strcmp0 (media_class, DEFAULT_TYPE_STR[i]) &&
        self->defaults[i].config_node_name)
      return g_strdup (self->defaults[i].config_node_name);

  return NULL;
}

static gchar *
wp_default_streams_api_get_default_configured_stream_name (
    WpDefaultStreamsApi * self, const gchar * media_class)
{
  for (gint i = 0; i < N_DEFAULT_STREAMS; i++)
    if (!g_strcmp0 (media_class, DEFAULT_TYPE_STR[i]) &&
        self->defaults[i].config_stream_name)
      return g_strdup (self->defaults[i].config_stream_name);

  return NULL;
}

static gboolean
wp_default_streams_api_set_default_configured_stream (
    WpDefaultStreamsApi * self, const gchar * media_class, const gchar * name,
    const gchar * stream)
{
  g_autoptr (WpMetadata) m = wp_object_manager_lookup (self->om,
      WP_TYPE_METADATA, NULL);
  if (!m)
    return FALSE;

  for (gint i = 0; i < N_DEFAULT_STREAMS; i++) {
    if (!g_strcmp0 (media_class, DEFAULT_TYPE_STR[i])) {
      if (name) {
        g_autoptr (WpSpaJsonBuilder) b = wp_spa_json_builder_new_object ();
        g_autoptr (WpSpaJson) json = NULL;
        wp_spa_json_builder_add_property (b, "name");
        wp_spa_json_builder_add_string (b, name);
        if (stream) {
          wp_spa_json_builder_add_property (b, "stream");
          wp_spa_json_builder_add_string (b, stream);
        }
        json = wp_spa_json_builder_end (b);
        wp_metadata_set (m, 0, DEFAULT_CONFIG_KEY[i], "Spa:String:JSON",
            wp_spa_json_get_data (json));
      } else {
        wp_metadata_set (m, 0, DEFAULT_CONFIG_KEY[i], NULL, NULL);
      }
      return TRUE;
    }
  }

  return FALSE;
}

static void
wp_default_streams_api_class_init (WpDefaultStreamsApiClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->enable = wp_default_streams_api_enable;
  plugin_class->disable = wp_default_streams_api_disable;

  signals[ACTION_GET_DEFAULT_STREAM] = g_signal_new_class_handler (
      "get-default-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_default_streams_api_get_default_stream,
      NULL, NULL, NULL,
      WP_TYPE_STREAM, 1, G_TYPE_STRING);

  signals[ACTION_GET_DEFAULT_CONFIGURED_NODE_NAME] = g_signal_new_class_handler (
      "get-default-configured-node-name", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_default_streams_api_get_default_configured_node_name,
      NULL, NULL, NULL,
      G_TYPE_STRING, 1, G_TYPE_STRING);

  signals[ACTION_GET_DEFAULT_CONFIGURED_STREAM_NAME] =
      g_signal_new_class_handler (
      "get-default-configured-stream-name", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_default_streams_api_get_default_configured_stream_name,
      NULL, NULL, NULL,
      G_TYPE_STRING, 1, G_TYPE_STRING);

  signals[ACTION_SET_DEFAULT_CONFIGURED_STREAM] = g_signal_new_class_handler (
      "set-default-configured-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_default_streams_api_set_default_configured_stream,
      NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  signals[SIGNAL_CHANGED] = g_signal_new (
      "changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

WP_PLUGIN_EXPORT GObject *
wireplumber__module_init (WpCore * core, WpSpaJson * args, GError ** error)
{
  return G_OBJECT (g_object_new (wp_default_streams_api_get_type (),
      "name", "default-streams-api",
      "core", core,
      NULL));
}
