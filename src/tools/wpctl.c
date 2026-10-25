/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <stdio.h>
#include <locale.h>
#include <libintl.h>
#include <spa/utils/defs.h>
#include <spa/utils/string.h>
#include <spa/param/param.h>
#include <spa/pod/iter.h>
#include <pipewire/pipewire.h>
#include <pipewire/keys.h>
#include <pipewire/extensions/session-manager/keys.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wpctl")

static const gchar *DEFAULT_NODE_MEDIA_CLASSES[] = {
  "Audio/Sink",
  "Audio/Source",
  "Video/Source",
};

typedef struct _WpCtl WpCtl;
struct _WpCtl
{
  GOptionContext *context;
  GMainLoop *loop;
  WpCore *core;
  WpObjectManager *om;
  guint pending_plugins;
  gint exit_code;
  gboolean streams_requested;
};

typedef enum {
  LIST_MEDIA_ALL = 0,
  LIST_MEDIA_AUDIO,
  LIST_MEDIA_VIDEO,
} ListMediaType;

typedef enum {
  LIST_OBJECT_ALL = 0,
  LIST_OBJECT_DEVICES,
  LIST_OBJECT_SINKS,
  LIST_OBJECT_SOURCES,
} ListObjectType;

static struct {
  union {
    struct {
      gboolean display_nicknames;
      gboolean display_names;
    } status;

    struct {
      ListMediaType media_type;
      ListObjectType object_type;
    } list;

    struct {
      guint64 id;
      gboolean show_referenced;
      gboolean show_associated;
    } inspect;
    struct {
      guint64 id;
      gchar *stream;
    } set_default;
    struct {
      guint64 id;
      gfloat volume;
      gdouble limit;
      gboolean is_pid;
      gchar type;
      gchar *stream;
    } set_volume;

    struct {
      guint64 id;
      gchar *stream;
    } get_volume;

    struct {
      guint64 id;
      guint mute;
      gboolean is_pid;
      gchar *stream;
    } set_mute;

    struct {
      guint64 id;
    } get_profile;

    struct {
      guint64 id;
      gint index;
    } set_profile;

    struct {
      guint64 id;
    } get_route;

    struct {
      guint64 id;
      gint index;
    } set_route;

    struct {
      guint64 id;
    } clear_default;

    struct {
      const gchar *key;
      const gchar *val;
      gboolean delete;
      gboolean save;
      gboolean reset;
    } settings;

    struct {
      guint64 id;
      const char *level;
    } set_log_level;

    struct {
      gboolean wp_config;
      gboolean pw_config;
      gboolean all;
      gboolean no_restart;
      gboolean dry_run;
      gboolean yes;
    } reset;

  };
} cmdline;

G_DEFINE_QUARK (wpctl-error, wpctl_error_domain)

static void
wp_ctl_clear (WpCtl * self)
{
  g_clear_object (&self->om);
  g_clear_object (&self->core);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_clear_pointer (&self->context, g_option_context_free);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (WpCtl, wp_ctl_clear)

static void
async_quit (WpCore *core, GAsyncResult *res, WpCtl * self)
{
  g_main_loop_quit (self->loop);
}

#define DEFAULT_AUDIO_SINK_ID ((guint64)1 << 32)
#define DEFAULT_AUDIO_SOURCE_ID ((guint64)1 << 33)
#define DEFAULT_VIDEO_SOURCE_ID ((guint64)1 << 34)

static gboolean
parse_id (gboolean allow_def_audio, gboolean allow_def_video, gchar *arg, guint64 *result_id, GError **error)
{
  if (allow_def_audio && (g_strcmp0(arg, "@DEFAULT_SINK@") == 0 ||
      g_strcmp0(arg, "@DEFAULT_AUDIO_SINK@") == 0)) {
    *result_id = DEFAULT_AUDIO_SINK_ID;
  } else if (allow_def_audio && (g_strcmp0(arg, "@DEFAULT_SOURCE@") == 0 ||
      g_strcmp0(arg, "@DEFAULT_AUDIO_SOURCE@") == 0)) {
    *result_id = DEFAULT_AUDIO_SOURCE_ID;
  } else if (allow_def_video && g_strcmp0(arg, "@DEFAULT_VIDEO_SOURCE@") == 0) {
    *result_id = DEFAULT_VIDEO_SOURCE_ID;
  } else {
    long id = strtol (arg, NULL, 10);
    if (id <= 0 || id >= G_MAXUINT32) {
      g_set_error (error, wpctl_error_domain_quark(), 0,
          "'%s' is not a valid number", arg);
      return FALSE;
    }
    *result_id = id;
  }

  return TRUE;
}

static const gchar *
get_default_id_media_class (guint64 id)
{
  if (id == DEFAULT_AUDIO_SINK_ID)
    return "Audio/Sink";
  else if (id == DEFAULT_AUDIO_SOURCE_ID)
    return "Audio/Source";
  else if (id == DEFAULT_VIDEO_SOURCE_ID)
    return "Video/Source";
  return NULL;
}

static WpStream *
get_default_stream (WpPlugin *def_streams_api, const gchar *media_class,
    GError **error)
{
  WpStream *stream = NULL;

  if (!def_streams_api) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "Default streams API is not loaded\n");
    return NULL;
  }

  g_signal_emit_by_name (def_streams_api, "get-default-stream", media_class,
      &stream);
  if (!stream) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "No default stream for '%s'", media_class);
    return NULL;
  }

  return stream;
}

static gboolean
translate_id (WpPlugin *def_streams_api, guint64 id, guint32 *res,
    GError **error)
{
  const gchar *media_class = get_default_id_media_class (id);
  g_autoptr (WpStream) stream = NULL;

  if (!media_class) {
    /* SPA_ID_INVALID is a special case used by some parse_positional() to
       indicate that no specific ID was given. It needs to be checked because
       currently SPA_ID_INVALID == G_MAXUINT32. */
    if ((id <= 0 || id >= G_MAXUINT32) && id != SPA_ID_INVALID) {
      g_set_error (error, wpctl_error_domain_quark(), 0,
          "'%ld' is not a valid ID", id);
      return FALSE;
    }

    *res = (guint32)id;
    return TRUE;
  }

  stream = get_default_stream (def_streams_api, media_class, error);
  if (!stream)
    return FALSE;

  *res = wp_stream_get_node_bound_id (stream);
  return TRUE;
}

static gboolean
run_nodes_by_pid (WpObjectManager *om, guint32 pid,
    WpIteratorFoldFunc func, gpointer data)
{
  gboolean res = TRUE;
  g_autoptr (WpIterator) client_it = NULL;
  g_auto (GValue) client_val = G_VALUE_INIT;
  client_it = wp_object_manager_new_filtered_iterator (om,
      WP_TYPE_CLIENT, WP_CONSTRAINT_TYPE_PW_PROPERTY,
      PW_KEY_APP_PROCESS_ID, "=u", pid, NULL);
  for (; wp_iterator_next (client_it, &client_val); g_value_unset (&client_val)) {
    WpPipewireObject *client = g_value_get_object (&client_val);
    guint32 client_id = wp_proxy_get_bound_id (WP_PROXY (client));
    g_autoptr (WpIterator) node_it = NULL;
    node_it = wp_object_manager_new_filtered_iterator (om,
        WP_TYPE_NODE, WP_CONSTRAINT_TYPE_PW_PROPERTY,
        PW_KEY_CLIENT_ID, "=u", client_id, NULL);
    if (!wp_iterator_fold (node_it, func, NULL, data))
      res = FALSE;
  }

  return res;
}

/* volume streams */

static void
add_node_audio_streams (WpObjectManager *om, guint32 node_id,
    const gchar *port_group, GPtrArray *streams)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  it = wp_object_manager_new_filtered_iterator (om, WP_TYPE_AUDIO_STREAM, NULL);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpStream *stream = g_value_get_object (&val);

    if (wp_stream_get_node_bound_id (stream) != node_id)
      continue;
    if (port_group && g_strcmp0 (port_group, wp_stream_get_port_group (stream)))
      continue;

    g_ptr_array_add (streams, g_object_ref (stream));
  }
}

struct volume_streams_context
{
  WpObjectManager *om;
  const gchar *port_group;
  GPtrArray *streams;
};

static gboolean
add_node_audio_streams_cb (const GValue *node_val, GValue *ret, gpointer data)
{
  struct volume_streams_context *context = data;
  WpProxy *node = g_value_get_object (node_val);

  add_node_audio_streams (context->om, wp_proxy_get_bound_id (node),
      context->port_group, context->streams);
  return TRUE;
}

/* Gets the audio streams of the node, or of the nodes of the PID, with the
 * given port group, or all of them if no port group is given. A default ID
 * without port group only gets the default stream. */
static GPtrArray *
get_volume_streams (WpCtl *self, guint64 id, const gchar *port_group,
    gboolean is_pid, GError **error)
{
  g_autoptr (WpPlugin) def_streams_api =
      wp_plugin_find (self->core, "default-streams-api");
  g_autoptr (GPtrArray) streams =
      g_ptr_array_new_with_free_func (g_object_unref);
  const gchar *media_class = get_default_id_media_class (id);
  guint32 node_id = (guint32) id;

  if (media_class) {
    g_autoptr (WpStream) stream =
        get_default_stream (def_streams_api, media_class, error);
    if (!stream)
      return NULL;

    node_id = wp_stream_get_node_bound_id (stream);
    if (!port_group)
      g_ptr_array_add (streams, g_steal_pointer (&stream));
    else
      add_node_audio_streams (self->om, node_id, port_group, streams);
  } else if (is_pid) {
    struct volume_streams_context context = { self->om, port_group, streams };
    run_nodes_by_pid (self->om, node_id, add_node_audio_streams_cb, &context);
  } else {
    add_node_audio_streams (self->om, node_id, port_group, streams);
  }

  if (streams->len == 0) {
    if (port_group)
      g_set_error (error, wpctl_error_domain_quark(), 0,
          "%s %u has no audio stream '%s'", is_pid ? "PID" : "node", node_id,
          port_group);
    else
      g_set_error (error, wpctl_error_domain_quark(), 0,
          "%s %u has no audio streams", is_pid ? "PID" : "node", node_id);
    return NULL;
  }

  return g_steal_pointer (&streams);
}

static void
print_stream_unsupported (WpStream *stream, const gchar *what)
{
  const gchar *port_group = wp_stream_get_port_group (stream);

  fprintf (stderr, "Stream '%s' of node %u does not support %s\n",
      port_group ? port_group : "(none)", wp_stream_get_node_bound_id (stream),
      what);
}

/* status */

static void on_plugin_ready (WpCtl * ctl);

static void
on_manager_activated (WpObject * o, GAsyncResult * res, WpCtl * ctl)
{
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (o, res, &error)) {
    fprintf (stderr, "%s\n", error->message);
    ctl->exit_code = 1;
    g_main_loop_quit (ctl->loop);
    return;
  }

  wp_core_register_object (ctl->core, g_object_ref (o));
  on_plugin_ready (ctl);
}

static void
on_stream_manager_activated (WpObject * o, GAsyncResult * res, WpCtl * ctl)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (WpStreamLinkManager) stream_link_manager = NULL;

  if (!wp_object_activate_finish (o, res, &error)) {
    fprintf (stderr, "%s\n", error->message);
    ctl->exit_code = 1;
    g_main_loop_quit (ctl->loop);
    return;
  }

  wp_core_register_object (ctl->core, g_object_ref (o));
  on_plugin_ready (ctl);

  /* the stream links are made from the streams, so they need to exist */
  stream_link_manager = wp_stream_link_manager_new (ctl->core, NULL);
  wp_object_activate (WP_OBJECT (stream_link_manager), WP_OBJECT_FEATURES_ALL,
      NULL, (GAsyncReadyCallback) on_manager_activated, ctl);
}

/* the streams are only known by the stream manager */
static void
request_streams (WpCtl * self)
{
  g_autoptr (WpStreamManager) stream_manager = NULL;

  if (self->streams_requested)
    return;
  self->streams_requested = TRUE;

  stream_manager = wp_stream_manager_new (self->core);
  self->pending_plugins++;
  wp_object_activate (WP_OBJECT (stream_manager), WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_manager_activated, self);
}

/* the default streams are needed to translate the default IDs */
static void
request_default_streams (WpCtl * self, guint64 id)
{
  if (id == DEFAULT_AUDIO_SINK_ID || id == DEFAULT_AUDIO_SOURCE_ID ||
      id == DEFAULT_VIDEO_SOURCE_ID)
    request_streams (self);
}

static gboolean
status_prepare (WpCtl * self, GError ** error)
{
  g_autoptr (WpStreamManager) stream_manager = wp_stream_manager_new (self->core);

  /* the streams and stream links are only known by their managers */
  self->pending_plugins += 2;
  wp_object_activate (WP_OBJECT (stream_manager), WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_stream_manager_activated, self);

  wp_object_manager_add_interest (self->om, WP_TYPE_CLIENT, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_DEVICE, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_NODE, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_PORT, NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_GLOBAL_PROXY,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  wp_object_manager_add_interest (self->om, WP_TYPE_METADATA, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_STREAM, NULL);
  return TRUE;
}

#define TREE_INDENT_LINE " │  "
#define TREE_INDENT_NODE " ├─ "
#define TREE_INDENT_END  " └─ "
#define TREE_INDENT_EMPTY "    "

struct print_context
{
  WpCtl *self;
  guint32 default_nodes[G_N_ELEMENTS (DEFAULT_NODE_MEDIA_CLASSES)];
  WpPlugin *mixer_api;
};

static void
print_controls (WpPlugin *mixer_api, WpStream *stream)
{
  g_autoptr (GVariant) dict = NULL;

  if (mixer_api)
    g_signal_emit_by_name (mixer_api, "get-volume", stream, &dict);

  if (dict) {
    gboolean mute = FALSE;
    gdouble volume = 1.0;
    if (g_variant_lookup (dict, "mute", "b", &mute) &&
        g_variant_lookup (dict, "volume", "d", &volume))
      printf (" [vol: %.2f%s", volume, mute ? " MUTED]" : "]");
  }
  printf ("\n");
}

static void
print_stream_links (WpCtl *self, const gchar *indent, WpStream *stream)
{
  g_autoptr (WpStreamLinkManager) slm = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  g_autoptr (GEnumClass) klass = g_type_class_ref (WP_TYPE_LINK_STATE);

  slm = wp_stream_link_manager_find (self->core, NULL);
  if (!slm)
    return;

  it = wp_stream_link_manager_new_links_iterator (slm, stream);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpStreamLink *sl = g_value_get_object (&val);
    g_autoptr (WpStream) output = NULL;
    g_autoptr (WpIterator) lit = NULL;
    g_auto (GValue) lval = G_VALUE_INIT;
    gboolean is_monitor = FALSE;
    gboolean is_output;

    output = wp_stream_link_get_output_stream (sl, &is_monitor);
    is_output = (output == stream);

    lit = wp_stream_link_new_links_iterator (sl);
    for (; wp_iterator_next (lit, &lval); g_value_unset (&lval)) {
      WpLink *link = g_value_get_object (&lval);
      g_autoptr (WpPipewireObject) port = NULL;
      g_autoptr (WpPipewireObject) peer = NULL;
      guint32 out_port_id = SPA_ID_INVALID, in_port_id = SPA_ID_INVALID;
      const gchar *port_name = NULL, *peer_name = NULL;
      GEnumValue *state;

      wp_link_get_linked_object_ids (link, NULL, &out_port_id, NULL,
          &in_port_id);
      port = wp_object_manager_lookup (self->om, WP_TYPE_PORT,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u",
          is_output ? out_port_id : in_port_id, NULL);
      peer = wp_object_manager_lookup (self->om, WP_TYPE_PORT,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u",
          is_output ? in_port_id : out_port_id, NULL);
      if (port)
        port_name = wp_pipewire_object_get_property (port, PW_KEY_PORT_NAME);
      if (peer)
        peer_name = wp_pipewire_object_get_property (peer, PW_KEY_PORT_ALIAS);
      state = g_enum_get_value (klass, wp_link_get_state (link, NULL));

      printf ("%s      %4u. %-15s %c %s\t[%s%s]\n", indent,
          wp_proxy_get_bound_id (WP_PROXY (link)),
          port_name ? port_name : "(unknown)", is_output ? '>' : '<',
          peer_name ? peer_name : "(unknown)",
          state ? state->value_nick : "unknown",
          is_monitor ? ", monitor" : "");
    }
  }
}

static gboolean
is_stream_node (WpPipewireObject *obj)
{
  const gchar *media_class =
      wp_pipewire_object_get_property (obj, PW_KEY_MEDIA_CLASS);
  return media_class && g_str_has_prefix (media_class, "Stream/");
}

static const gchar *
get_node_display_name (WpPipewireObject *obj, gboolean nicknames,
    gboolean names)
{
  gboolean is_stream = is_stream_node (obj);
  const gchar *name = NULL;

  if (is_stream)
    name = wp_pipewire_object_get_property (obj, PW_KEY_APP_NAME);
  else if (nicknames)
    name = wp_pipewire_object_get_property (obj, PW_KEY_NODE_NICK);
  else if (names)
    name = wp_pipewire_object_get_property (obj, PW_KEY_NODE_NAME);

  if (!name)
    name = wp_pipewire_object_get_property (obj,
        is_stream ? PW_KEY_NODE_NAME : PW_KEY_NODE_DESCRIPTION);
  if (!name)
    name = wp_pipewire_object_get_property (obj, PW_KEY_NODE_NAME);
  return name;
}

static void
print_node_streams (WpCtl *self, WpPipewireObject *node, GType type,
    WpStreamShape shape, WpStream *default_stream, WpPlugin *mixer_api,
    const gchar *indent)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  guint32 node_id = wp_proxy_get_bound_id (WP_PROXY (node));
  const gchar *name = get_node_display_name (node,
      cmdline.status.display_nicknames, cmdline.status.display_names);

  it = wp_object_manager_new_filtered_iterator (self->om, type, NULL);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpStream *stream = g_value_get_object (&val);
    const gchar *port_group;
    g_autofree gchar *label = NULL;
    gboolean is_default;

    if (wp_stream_get_node_bound_id (stream) != node_id ||
        wp_stream_get_shape (stream) != shape)
      continue;

    port_group = wp_stream_get_port_group (stream);
    label = g_strdup_printf ("%s - %s", port_group ? port_group : "(none)",
        name);
    is_default = (stream == default_stream);
    printf ("%s%c %4u. %-35s", indent, is_default ? '*' : ' ', node_id, label);
    print_controls (mixer_api, stream);

    /* the links are shown on the streams of client nodes only, so that each
     * link is not shown twice */
    if (is_stream_node (node))
      print_stream_links (self, indent, stream);
  }
}

static void
print_device (const GValue *item, gpointer data)
{
  WpPipewireObject *obj = g_value_get_object (item);
  guint32 id = wp_proxy_get_bound_id (WP_PROXY (obj));
  const gchar *api = wp_pipewire_object_get_property (obj, PW_KEY_DEVICE_API);
  const gchar *name = NULL;

  if (cmdline.status.display_nicknames)
    name = wp_pipewire_object_get_property (obj, PW_KEY_DEVICE_NICK);
  else if (cmdline.status.display_names)
    name = wp_pipewire_object_get_property (obj, PW_KEY_DEVICE_NAME);

  if (!name)
    name = wp_pipewire_object_get_property (obj, PW_KEY_DEVICE_DESCRIPTION);

  printf (TREE_INDENT_LINE "  %4u. %-35s [%s]\n", id, name, api);
}

/* Nodes with several streams have no media class, so the media is also taken
 * from the properties of their streams */
static gboolean
node_has_media (WpCtl *self, WpPipewireObject *node, const gchar *media_type)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  guint32 id = wp_proxy_get_bound_id (WP_PROXY (node));
  const gchar *media_class =
      wp_pipewire_object_get_property (node, PW_KEY_MEDIA_CLASS);

  if (media_class && strstr (media_class, media_type))
    return TRUE;

  it = wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_STREAM, NULL);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpStream *stream = g_value_get_object (&val);
    g_autoptr (WpProperties) props = NULL;
    const gchar *stream_type, *stream_class;

    if (wp_stream_get_node_bound_id (stream) != id)
      continue;

    props = wp_stream_get_properties (stream);
    stream_type = wp_properties_get (props, PW_KEY_MEDIA_TYPE);
    stream_class = wp_properties_get (props, PW_KEY_MEDIA_CLASS);
    if (g_strcmp0 (stream_type, media_type) == 0 ||
        (stream_class && strstr (stream_class, media_type)))
      return TRUE;
  }

  return FALSE;
}

static gboolean
node_has_any_media (WpCtl *self, WpPipewireObject *obj)
{
  return node_has_media (self, obj, "Audio") ||
      node_has_media (self, obj, "Video");
}

static void
print_node (const GValue *item, gpointer data)
{
  WpPipewireObject *obj = g_value_get_object (item);
  struct print_context *context = data;
  guint32 id = wp_proxy_get_bound_id (WP_PROXY (obj));
  gboolean is_default = FALSE;

  if (!node_has_any_media (context->self, obj))
    return;

  for (guint i = 0; i < G_N_ELEMENTS (context->default_nodes); i++)
    is_default |= (context->default_nodes[i] == id);

  printf (TREE_INDENT_EMPTY "%c %4u. %s\n", is_default ? '*' : ' ', id,
      get_node_display_name (obj, cmdline.status.display_nicknames,
          cmdline.status.display_names));
}

static void
print_default_stream (const gchar *indent, guint index, WpPipewireObject *node,
    const gchar *name, const gchar *stream)
{
  g_autofree gchar *media_class =
      g_strconcat (DEFAULT_NODE_MEDIA_CLASSES[index], ":", NULL);

  if (!name && node)
    name = wp_pipewire_object_get_property (node, PW_KEY_NODE_NAME);

  printf ("%s  %4u. %-13s %s - %s\n", indent, index, media_class,
      stream ? stream : "(none)", name ? name : "(unknown)");
}

static void
status_run (WpCtl * self)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  g_autoptr (WpPlugin) def_streams_api = NULL;
  struct print_context context = { .self = self };

  def_streams_api = wp_plugin_find (self->core, "default-streams-api");
  context.mixer_api = wp_plugin_find (self->core, "mixer-api");

  /* server + clients */
  printf ("PipeWire '%s' [%s, %s@%s, cookie:%u]\n",
      wp_core_get_remote_name (self->core),
      wp_core_get_remote_version (self->core),
      wp_core_get_remote_user_name (self->core),
      wp_core_get_remote_host_name (self->core),
      wp_core_get_remote_cookie (self->core));

  printf (TREE_INDENT_NODE "Clients:\n");
  it = wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_CLIENT, NULL);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpProxy *client = g_value_get_object (&val);
    g_autoptr (WpProperties) properties =
        wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (client));

    printf (TREE_INDENT_LINE "  %4u. %-35s [%s, %s@%s, pid:%s]\n",
        wp_proxy_get_bound_id (client),
        wp_properties_get (properties, PW_KEY_APP_NAME),
        wp_properties_get (properties, PW_KEY_CORE_VERSION),
        wp_properties_get (properties, PW_KEY_APP_PROCESS_USER),
        wp_properties_get (properties, PW_KEY_APP_PROCESS_HOST),
        wp_properties_get (properties, PW_KEY_APP_PROCESS_ID));
  }
  g_clear_pointer (&it, wp_iterator_unref);
  printf (TREE_INDENT_LINE "\n");

  /* nodes; a node can have both audio and video streams */
  printf (TREE_INDENT_END "Nodes:\n");
  for (guint i = 0; i < G_N_ELEMENTS (DEFAULT_NODE_MEDIA_CLASSES); i++) {
    g_autoptr (WpStream) stream = NULL;

    context.default_nodes[i] = -1;
    if (def_streams_api)
      g_signal_emit_by_name (def_streams_api, "get-default-stream",
          DEFAULT_NODE_MEDIA_CLASSES[i], &stream);
    if (stream)
      context.default_nodes[i] = wp_stream_get_node_bound_id (stream);
  }
  it = wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_NODE, NULL);
  wp_iterator_foreach (it, print_node, (gpointer) &context);
  printf ("\n");

  /* streams, by media and shape */
  const struct {
    const gchar *name;
    GType type;
  } MEDIA[] = {
    { "Audio", WP_TYPE_AUDIO_STREAM },
    { "Video", WP_TYPE_VIDEO_STREAM },
  };
  const struct {
    const gchar *name;
    WpStreamShape shape;
    /* the default node media class suffix, if the shape can have defaults */
    const gchar *default_class;
  } SHAPES[] = {
    { "Sinks", WP_STREAM_SHAPE_SINK, "Sink" },
    { "Sources", WP_STREAM_SHAPE_SOURCE, "Source" },
    { "Inputs", WP_STREAM_SHAPE_INPUT, NULL },
    { "Outputs", WP_STREAM_SHAPE_OUTPUT, NULL },
  };

  for (guint i = 0; i < G_N_ELEMENTS (MEDIA); i++) {
    g_autoptr (WpIterator) dev_it = NULL;
    g_autofree gchar *media_type_glob =
        g_strdup_printf ("*%s*", MEDIA[i].name);

    printf ("%s\n", MEDIA[i].name);

    printf (TREE_INDENT_NODE "Devices:\n");
    dev_it = wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_DEVICE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_MEDIA_CLASS, "#s", media_type_glob,
        NULL);
    wp_iterator_foreach (dev_it, print_device, self);
    printf (TREE_INDENT_LINE "\n");

    for (guint j = 0; j < G_N_ELEMENTS (SHAPES); j++) {
      gboolean is_last = (j == G_N_ELEMENTS (SHAPES) - 1);
      const gchar *indent = is_last ? TREE_INDENT_EMPTY : TREE_INDENT_LINE;
      g_auto (GValue) node_val = G_VALUE_INIT;
      g_autoptr (WpStream) default_stream = NULL;

      if (def_streams_api && SHAPES[j].default_class) {
        g_autofree gchar *media_class = g_strdup_printf ("%s/%s",
            MEDIA[i].name, SHAPES[j].default_class);
        g_signal_emit_by_name (def_streams_api, "get-default-stream",
            media_class, &default_stream);
      }

      printf ("%s%s:\n", is_last ? TREE_INDENT_END : TREE_INDENT_NODE,
          SHAPES[j].name);
      wp_iterator_reset (it);
      for (; wp_iterator_next (it, &node_val); g_value_unset (&node_val))
        print_node_streams (self, g_value_get_object (&node_val),
            MEDIA[i].type, SHAPES[j].shape, default_stream,
            context.mixer_api, indent);
      printf ("%s\n", is_last ? "" : TREE_INDENT_LINE);
    }
  }
  g_clear_pointer (&it, wp_iterator_unref);

  /* Default */
  printf ("Default\n");

  printf (TREE_INDENT_NODE "Current:\n");
  if (def_streams_api) {
    for (guint i = 0; i < G_N_ELEMENTS (DEFAULT_NODE_MEDIA_CLASSES); i++) {
      g_autoptr (WpStream) stream = NULL;
      g_autoptr (WpPipewireObject) node = NULL;

      g_signal_emit_by_name (def_streams_api, "get-default-stream",
          DEFAULT_NODE_MEDIA_CLASSES[i], &stream);
      if (!stream)
        continue;

      node = wp_object_manager_lookup (self->om, WP_TYPE_NODE,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u",
          wp_stream_get_node_bound_id (stream), NULL);
      print_default_stream (TREE_INDENT_LINE, i, node, NULL,
          wp_stream_get_port_group (stream));
    }
  }
  printf (TREE_INDENT_LINE "\n");

  printf (TREE_INDENT_END "Configured:\n");
  if (def_streams_api) {
    for (guint i = 0; i < G_N_ELEMENTS (DEFAULT_NODE_MEDIA_CLASSES); i++) {
      g_autofree gchar *name = NULL;
      g_autofree gchar *stream = NULL;
      g_autoptr (WpPipewireObject) node = NULL;

      g_signal_emit_by_name (def_streams_api,
          "get-default-configured-node-name",
          DEFAULT_NODE_MEDIA_CLASSES[i], &name);
      g_signal_emit_by_name (def_streams_api,
          "get-default-configured-stream-name",
          DEFAULT_NODE_MEDIA_CLASSES[i], &stream);
      if (!name)
        continue;

      node = wp_object_manager_lookup (self->om, WP_TYPE_NODE,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_NAME, "=s", name, NULL);
      print_default_stream (TREE_INDENT_EMPTY, i, node, name, stream);
    }
  }

  g_clear_object (&context.mixer_api);
  g_main_loop_quit (self->loop);
}

/* list */

static gboolean
list_parse_positional (gint argc, gchar ** argv, GError **error)
{
  cmdline.list.media_type = LIST_MEDIA_ALL;
  cmdline.list.object_type = LIST_OBJECT_ALL;

  if (argc < 3)
    return TRUE;

  if (g_strcmp0 (argv[2], "audio") == 0) {
    cmdline.list.media_type = LIST_MEDIA_AUDIO;
  } else if (g_strcmp0 (argv[2], "video") == 0) {
    cmdline.list.media_type = LIST_MEDIA_VIDEO;
  } else {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "'%s' is not a valid list option", argv[2]);
    return FALSE;
  }

  if (argc < 4)
    return TRUE;

  if (g_strcmp0 (argv[3], "devices") == 0) {
    cmdline.list.object_type = LIST_OBJECT_DEVICES;
  } else if (g_strcmp0 (argv[3], "sinks") == 0) {
    cmdline.list.object_type = LIST_OBJECT_SINKS;
  } else if (g_strcmp0 (argv[3], "sources") == 0) {
    cmdline.list.object_type = LIST_OBJECT_SOURCES;
  } else {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "'%s' is not a valid list option", argv[3]);
    return FALSE;
  }

  return TRUE;
}

static gboolean
list_prepare (WpCtl * self, GError ** error)
{
  /* the sinks and sources are listed by stream */
  request_streams (self);

  wp_object_manager_add_interest (self->om, WP_TYPE_DEVICE, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_NODE, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_METADATA, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_STREAM, NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_GLOBAL_PROXY,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  return TRUE;
}

struct list_context
{
  WpCtl *self;
  WpStream *default_stream;
  WpStreamShape shape;
  const gchar *media_type;
  const gchar *object_type;
};

static void
list_print_device (const GValue *item, gpointer data)
{
  WpPipewireObject *obj = g_value_get_object (item);
  struct list_context *context = data;
  guint32 id = wp_proxy_get_bound_id (WP_PROXY (obj));
  const gchar *name = wp_pipewire_object_get_property (obj, PW_KEY_DEVICE_NAME);
  printf ("%u\t%s\t%s/%s\t \n", id, name, context->media_type, context->object_type);
}

static void
list_print_stream (const GValue *item, gpointer data)
{
  WpStream *stream = g_value_get_object (item);
  struct list_context *context = data;
  guint32 id = wp_stream_get_node_bound_id (stream);
  const gchar *port_group = wp_stream_get_port_group (stream);
  g_autoptr (WpPipewireObject) node = NULL;
  gboolean is_default;
  const gchar *name;

  if (wp_stream_get_shape (stream) != context->shape)
    return;

  /* nodes of a link group are internal to their filter */
  node = wp_object_manager_lookup (context->self->om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", id,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_LINK_GROUP, "-",
      NULL);
  if (!node)
    return;

  is_default = (stream == context->default_stream);
  name = wp_pipewire_object_get_property (node, PW_KEY_NODE_NAME);
  printf ("%u\t%s\t%s/%s\t%c", id, name, context->media_type,
      context->object_type, is_default ? '*' : ' ');
  if (port_group)
    printf ("\t%s", port_group);
  printf ("\n");
}

static void
list_print_streams (WpCtl * self, struct list_context *context,
    WpPlugin *def_streams_api, const gchar *media_type, GType type,
    WpStreamShape shape)
{
  g_autoptr (WpIterator) it = NULL;
  g_autofree gchar *media_class = g_strdup_printf ("%s/%s", media_type,
      shape == WP_STREAM_SHAPE_SINK ? "Sink" : "Source");

  context->default_stream = NULL;
  context->object_type =
      shape == WP_STREAM_SHAPE_SINK ? "sink" : "source";
  if (def_streams_api)
    g_signal_emit_by_name (def_streams_api, "get-default-stream", media_class,
        &context->default_stream);

  context->shape = shape;
  it = wp_object_manager_new_filtered_iterator (self->om, type, NULL);
  wp_iterator_foreach (it, list_print_stream, context);
  g_clear_object (&context->default_stream);
}

static const struct {
  const gchar *title_name;
  const gchar *lower_name;
  ListMediaType value;
  GType (*stream_type) (void);
} list_media_types[] = {
  { "Audio", "audio", LIST_MEDIA_AUDIO, wp_audio_stream_get_type },
  { "Video", "video", LIST_MEDIA_VIDEO, wp_video_stream_get_type },
};

static void
list_run (WpCtl * self)
{
  struct list_context context = { .self = self };
  g_autoptr (WpPlugin) def_streams_api = wp_plugin_find (self->core, "default-streams-api");
  const ListMediaType media_filter = cmdline.list.media_type;
  const ListObjectType object_filter = cmdline.list.object_type;

  for (guint i = 0; i < G_N_ELEMENTS (list_media_types); i++) {
    if (media_filter != LIST_MEDIA_ALL &&
        media_filter != list_media_types[i].value)
        continue;

    const gchar *media_type = list_media_types[i].title_name;
    context.media_type = list_media_types[i].lower_name;
    gchar media_type_glob[16];

    g_snprintf (media_type_glob, sizeof(media_type_glob), "*%s*", media_type);

    /* Devices */
    if (object_filter == LIST_OBJECT_ALL || object_filter == LIST_OBJECT_DEVICES) {
      context.object_type = "device";
      g_autoptr (WpIterator) it = wp_object_manager_new_filtered_iterator (self->om,
          WP_TYPE_DEVICE,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_MEDIA_CLASS, "#s", media_type_glob,
          NULL);
      wp_iterator_foreach (it, list_print_device, &context);
    }

    /* Sinks */
    if (object_filter == LIST_OBJECT_ALL || object_filter == LIST_OBJECT_SINKS)
      list_print_streams (self, &context, def_streams_api, media_type,
          list_media_types[i].stream_type (), WP_STREAM_SHAPE_SINK);

    /* Sources */
    if (object_filter == LIST_OBJECT_ALL || object_filter == LIST_OBJECT_SOURCES)
      list_print_streams (self, &context, def_streams_api, media_type,
          list_media_types[i].stream_type (), WP_STREAM_SHAPE_SOURCE);
  }

  g_main_loop_quit (self->loop);
}

/* get-volume  */

static gboolean
get_volume_parse_positional (gint argc, gchar ** argv, GError **error)
{
  if (argc < 3) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "ID is required");
    return FALSE;
  } else {
    return parse_id (true, false, argv[2], &cmdline.get_volume.id, error);
  }
}

static gboolean
get_volume_prepare (WpCtl * self, GError ** error)
{
  /* the volume is set on the audio streams of the node */
  request_streams (self);
  wp_object_manager_add_interest (self->om, WP_TYPE_STREAM, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_NODE, NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_GLOBAL_PROXY,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  return TRUE;
}

/* the port group is shown when there are several streams, so that the output
 * of a single stream stays the same */
static void
do_print_volume (WpCtl * self, WpStream *stream, gboolean show_stream)
{
  g_autoptr (WpPlugin) mixer_api = wp_plugin_find (self->core, "mixer-api");
  GVariant *variant = NULL;
  gboolean mute = FALSE;
  gdouble volume = 1.0;

  g_signal_emit_by_name (mixer_api, "get-volume", stream, &variant);
  if (!variant) {
    print_stream_unsupported (stream, "volume");
    return;
  }
  g_variant_lookup (variant, "volume", "d", &volume);
  g_variant_lookup (variant, "mute", "b", &mute);
  g_clear_pointer (&variant, g_variant_unref);

  if (show_stream) {
    const gchar *port_group = wp_stream_get_port_group (stream);
    printf ("%s: ", port_group ? port_group : "(none)");
  }
  printf ("Volume: %.2f%s", volume, mute ? " [MUTED]\n" : "\n");
}

static void
get_volume_run (WpCtl * self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GPtrArray) streams = NULL;

  streams = get_volume_streams (self, cmdline.get_volume.id,
      cmdline.get_volume.stream, FALSE, &error);
  if (!streams) {
    fprintf (stderr, "%s\n", error->message);
    goto out;
  }

  for (guint i = 0; i < streams->len; i++)
    do_print_volume (self, g_ptr_array_index (streams, i), streams->len > 1);

out:
  g_main_loop_quit (self->loop);
}

/* inspect */

static gboolean
inspect_parse_positional (gint argc, gchar ** argv, GError **error)
{
  if (argc < 3) {
    g_set_error (error, wpctl_error_domain_quark(), 0, "ID is required");
    return FALSE;
  }

  return parse_id (true, true, argv[2], &cmdline.inspect.id, error);
}

static gboolean
inspect_prepare (WpCtl * self, GError ** error)
{
  /* the streams of a node are shown with its associated objects */
  if (cmdline.inspect.show_associated) {
    request_streams (self);
    wp_object_manager_add_interest (self->om, WP_TYPE_STREAM, NULL);
  }
  request_default_streams (self, cmdline.inspect.id);

  /* collect all objects */
  wp_object_manager_add_interest (self->om, WP_TYPE_GLOBAL_PROXY, NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_GLOBAL_PROXY,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  return TRUE;
}

/* indent holds the tree lines of the parent objects, and last is set if the
 * object is the last one of its parent, so that no tree line is drawn below it */
static inline void
inspect_prefix_line (guint nest_level, const gchar *indent, gboolean last,
    gboolean node)
{
  if (nest_level == 0)
    return;
  if (node)
    printf ("%s" TREE_INDENT_EMPTY "%s", indent,
        last ? TREE_INDENT_END : TREE_INDENT_NODE);
  else
    printf ("%s" TREE_INDENT_EMPTY "%s", indent,
        last ? TREE_INDENT_EMPTY : TREE_INDENT_LINE);
}

static inline gchar *
inspect_new_child_indent (guint nest_level, const gchar *indent, gboolean last)
{
  if (nest_level == 0)
    return g_strdup ("");
  return g_strconcat (indent, TREE_INDENT_EMPTY,
      last ? TREE_INDENT_EMPTY : TREE_INDENT_LINE, NULL);
}

struct {
  const gchar *key;
  const gchar *type;
} assoc_keys[] = {
  { PW_KEY_CLIENT_ID, "Client" },
  { PW_KEY_DEVICE_ID, "Device" },
  { PW_KEY_LINK_INPUT_NODE, NULL },
  { PW_KEY_LINK_INPUT_PORT, NULL },
  { PW_KEY_LINK_OUTPUT_NODE, NULL },
  { PW_KEY_LINK_OUTPUT_PORT, NULL },
  { PW_KEY_LINK_ID, "Link" },
  { PW_KEY_NODE_ID, "Node" },
  { PW_KEY_PORT_ID, "Port" },
  { PW_KEY_SESSION_ID, "Session" },
};

static inline gboolean
key_is_object_reference (const gchar *key)
{
  for (guint i = 0; i < G_N_ELEMENTS (assoc_keys); i++)
    if (!g_strcmp0 (key, assoc_keys[i].key))
      return TRUE;
  return FALSE;
}

static inline const gchar *
get_association_key (WpProxy * proxy)
{
  for (guint i = 0; i < G_N_ELEMENTS (assoc_keys); i++) {
    if (assoc_keys[i].type &&
        strstr (WP_PROXY_GET_CLASS (proxy)->pw_iface_type, assoc_keys[i].type))
      return assoc_keys[i].key;
  }
  return NULL;
}

struct property_item {
  const gchar *key;
  const gchar *value;
};

static gint
property_item_compare (gconstpointer a, gconstpointer b)
{
  return g_strcmp0 (
      ((struct property_item *) a)->key,
      ((struct property_item *) b)->key);
}

static GArray *
new_sorted_property_items (WpProperties * properties)
{
  GArray *array = g_array_new (FALSE, FALSE, sizeof (struct property_item));
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  /* copy key/value pointers to an array for sorting */
  for (it = wp_properties_new_iterator (properties);
        wp_iterator_next (it, &item);
        g_value_unset (&item)) {
    WpPropertiesItem *pi = g_value_get_boxed (&item);
    struct property_item prop_item = {
      .key = wp_properties_item_get_key (pi),
      .value = wp_properties_item_get_value (pi),
    };
    g_array_append_val (array, prop_item);
  }

  g_array_sort (array, property_item_compare);
  return array;
}

static void inspect_print_object (WpCtl * self, WpProxy * proxy,
    guint nest_level, const gchar *indent, gboolean last);

static inline gboolean
stream_has_any_port (WpStream * stream, guint32 port_id)
{
  return wp_stream_has_port (stream, port_id) ||
      wp_stream_has_monitor_port (stream, port_id) ||
      wp_stream_has_control_port (stream, port_id);
}

static gboolean
node_streams_have_port (WpCtl * self, guint32 node_id, guint32 port_id)
{
  g_autoptr (WpIterator) it =
      wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_STREAM, NULL);
  g_auto (GValue) item = G_VALUE_INIT;

  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpStream *stream = g_value_get_object (&item);
    if (wp_stream_get_node_bound_id (stream) == node_id &&
        stream_has_any_port (stream, port_id))
      return TRUE;
  }
  return FALSE;
}

static void
inspect_print_stream (WpCtl * self, WpStream * stream, guint nest_level,
    const gchar *indent, gboolean last)
{
  g_autofree gchar *child_indent =
      inspect_new_child_indent (nest_level, indent, last);
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  g_autoptr (GPtrArray) ports = g_ptr_array_new_with_free_func (g_object_unref);
  g_autoptr (WpProperties) properties = wp_stream_get_properties (stream);
  g_autoptr (GArray) array = new_sorted_property_items (properties);
  const gchar *port_group = wp_stream_get_port_group (stream);

  inspect_prefix_line (nest_level, indent, last, TRUE);
  printf ("stream %s, type %s\n", port_group ? port_group : "(none)",
      G_OBJECT_TYPE_NAME (stream));

  for (guint i = 0; i < array->len; i++) {
    struct property_item *prop_item =
        &g_array_index (array, struct property_item, i);

    inspect_prefix_line (nest_level, indent, last, FALSE);
    printf ("    %s = \"%s\"\n", prop_item->key, prop_item->value);
  }

  /* print the ports of the stream */
  inspect_prefix_line (nest_level, indent, last, FALSE);
  printf ("\n");
  inspect_prefix_line (nest_level, indent, last, FALSE);
  printf ("Associated Objects:\n");
  it = wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_PORT,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_ID, "=u",
      wp_stream_get_node_bound_id (stream), NULL);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpProxy *port = g_value_get_object (&item);
    if (stream_has_any_port (stream, wp_proxy_get_bound_id (port)))
      g_ptr_array_add (ports, g_object_ref (port));
  }

  for (guint i = 0; i < ports->len; i++)
    inspect_print_object (self, g_ptr_array_index (ports, i), nest_level + 1,
        child_indent, i == ports->len - 1);
}

static void
inspect_print_object (WpCtl * self, WpProxy * proxy, guint nest_level,
    const gchar *indent, gboolean last)
{
  g_autofree gchar *child_indent =
      inspect_new_child_indent (nest_level, indent, last);
  g_autoptr (WpProperties) properties =
      WP_IS_PIPEWIRE_OBJECT (proxy) ?
      wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (proxy)) :
      wp_properties_new_empty ();
  g_autoptr (WpProperties) global_p =
      wp_global_proxy_get_global_properties (WP_GLOBAL_PROXY (proxy));
  g_autoptr (GArray) array = NULL;

  /* print basic object info */
  inspect_prefix_line (nest_level, indent, last, TRUE);
  printf ("id %u, type %s\n",
      wp_proxy_get_bound_id (proxy),
      WP_PROXY_GET_CLASS (proxy)->pw_iface_type);

  /* merge the two property sets */
  properties = wp_properties_ensure_unique_owner (properties);
  wp_properties_add (properties, global_p);
  wp_properties_set (properties, "object.id", NULL);

  array = new_sorted_property_items (properties);

  /* print */
  for (guint i = 0; i < array->len; i++) {
    struct property_item *prop_item =
        &g_array_index (array, struct property_item, i);
    gboolean is_global =
        (wp_properties_get (global_p, prop_item->key) != NULL);

    inspect_prefix_line (nest_level, indent, last, FALSE);
    printf ("  %c %s = \"%s\"\n", is_global ? '*' : ' ',
        prop_item->key, prop_item->value);

    /* if the property is referencing an object, print the object */
    if (cmdline.inspect.show_referenced && nest_level == 0 &&
        key_is_object_reference (prop_item->key))
    {
      guint32 id = (guint32) strtol (prop_item->value, NULL, 10);
      g_autoptr (WpProxy) refer_proxy =
          wp_object_manager_lookup (self->om, WP_TYPE_GLOBAL_PROXY,
              WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", id, NULL);

      /* it is the only object under its property */
      if (refer_proxy)
        inspect_print_object (self, refer_proxy, nest_level + 1,
            child_indent, TRUE);
    }
  }

  /* print associated objects */
  if (cmdline.inspect.show_associated && nest_level == 0) {
    const gchar *lookup_key = get_association_key (proxy);
    if (lookup_key) {
      g_autoptr (WpIterator) it =
          wp_object_manager_new_filtered_iterator (self->om,
              WP_TYPE_PIPEWIRE_OBJECT, WP_CONSTRAINT_TYPE_PW_PROPERTY,
              lookup_key, "=u", wp_proxy_get_bound_id (proxy), NULL);
      g_auto (GValue) item = G_VALUE_INIT;
      g_autoptr (GPtrArray) assoc_objects =
          g_ptr_array_new_with_free_func (g_object_unref);

      inspect_prefix_line (nest_level, indent, last, FALSE);
      printf ("\n");
      inspect_prefix_line (nest_level, indent, last, TRUE);
      printf ("Associated Objects:\n");

      for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
        WpProxy *assoc_proxy = g_value_get_object (&item);

        /* the ports of a stream are printed with that stream */
        if (WP_IS_PORT (assoc_proxy) && node_streams_have_port (self,
                wp_proxy_get_bound_id (proxy),
                wp_proxy_get_bound_id (assoc_proxy)))
          continue;

        g_ptr_array_add (assoc_objects, g_object_ref (assoc_proxy));
      }

      /* the streams are not pipewire objects, so look them up separately */
      if (WP_IS_NODE (proxy)) {
        g_autoptr (WpIterator) stream_it =
            wp_object_manager_new_filtered_iterator (self->om,
                WP_TYPE_STREAM, NULL);

        for (; wp_iterator_next (stream_it, &item); g_value_unset (&item)) {
          WpStream *stream = g_value_get_object (&item);
          if (wp_stream_get_node_bound_id (stream) ==
              wp_proxy_get_bound_id (proxy))
            g_ptr_array_add (assoc_objects, g_object_ref (stream));
        }
      }

      for (guint i = 0; i < assoc_objects->len; i++) {
        gpointer obj = g_ptr_array_index (assoc_objects, i);
        gboolean obj_last = (i == assoc_objects->len - 1);

        if (WP_IS_STREAM (obj))
          inspect_print_stream (self, obj, nest_level + 1, child_indent,
              obj_last);
        else
          inspect_print_object (self, obj, nest_level + 1, child_indent,
              obj_last);
      }
    }
  }
}

static void
inspect_run (WpCtl * self)
{
  g_autoptr (WpProxy) proxy = NULL;
  g_autoptr (WpPlugin) def_streams_api = wp_plugin_find (self->core, "default-streams-api");
  g_autoptr (GError) error = NULL;
  guint32 id;


  if (!translate_id (def_streams_api, cmdline.inspect.id, &id, &error)) {
    fprintf(stderr, "Translate ID error: %s\n\n", error->message);
    goto out_err;
  }

  proxy = wp_object_manager_lookup (self->om, WP_TYPE_GLOBAL_PROXY,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", id, NULL);
  if (!proxy) {
    fprintf (stderr, "Object '%d' not found\n", id);
    goto out_err;
  }

  inspect_print_object (self, proxy, 0, "", FALSE);

out:
  g_main_loop_quit (self->loop);
  return;

out_err:
  self->exit_code = 3;
  goto out;
}

/* set-default */
static gboolean
set_default_parse_positional (gint argc, gchar ** argv, GError **error)
{
  if (argc < 3) {
    g_set_error (error, wpctl_error_domain_quark(), 0, "ID is required");
    return FALSE;
  }

  return parse_id (false, false, argv[2], &cmdline.set_default.id, error);
}

static gboolean
set_default_prepare (WpCtl * self, GError ** error)
{
  /* the default media classes are taken from the streams of the node */
  request_streams (self);
  wp_object_manager_add_interest (self->om, WP_TYPE_STREAM, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      "object.id", "=u", (guint32) cmdline.set_default.id,
      NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_NODE,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  return TRUE;
}

static gint
get_default_media_class_index (const gchar * media_class)
{
  if (!media_class || g_str_has_suffix (media_class, "/Internal"))
    return -1;

  for (guint i = 0; i < G_N_ELEMENTS (DEFAULT_NODE_MEDIA_CLASSES); i++)
    if (g_str_has_prefix (media_class, DEFAULT_NODE_MEDIA_CLASSES[i]))
      return i;
  return -1;
}

static void
set_default_run (WpCtl * self)
{
  g_autoptr (WpPlugin) def_streams_api = NULL;
  g_autoptr (WpProxy) proxy = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  WpStream *streams[G_N_ELEMENTS (DEFAULT_NODE_MEDIA_CLASSES)] = { NULL, };
  guint32 id;
  g_autoptr (GError) error = NULL;
  const gchar *name;
  gboolean found = FALSE;

  def_streams_api = wp_plugin_find (self->core, "default-streams-api");
  if (!def_streams_api) {
    fprintf (stderr, "Default streams API not loaded\n");
    goto out;
  }

  if (!translate_id (def_streams_api, cmdline.set_default.id, &id, &error)) {
    fprintf (stderr, "Translate ID error: %s\n\n", error->message);
    goto out;
  }

  proxy = wp_object_manager_lookup (self->om, WP_TYPE_NODE, NULL);
  if (!proxy) {
    fprintf (stderr, "Node '%d' not found\n", id);
    goto out;
  }

  name = wp_pipewire_object_get_property (WP_PIPEWIRE_OBJECT (proxy),
      PW_KEY_NODE_NAME);
  if (!name) {
    fprintf (stderr, "node %u does not have a valid node.name\n", id);
    goto out;
  }

  /* without a port group, the first stream of each default media class is
   * set as the default stream of that class */
  it = wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_STREAM, NULL);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpStream *stream = g_value_get_object (&val);
    gint i;

    if (wp_stream_get_node_bound_id (stream) != id)
      continue;
    if (cmdline.set_default.stream && g_strcmp0 (cmdline.set_default.stream,
            wp_stream_get_port_group (stream)))
      continue;

    i = get_default_media_class_index (
        wp_stream_object_get_property (stream, PW_KEY_MEDIA_CLASS));
    if (i >= 0 && !streams[i])
      streams[i] = g_object_ref (stream);
  }

  for (guint i = 0; i < G_N_ELEMENTS (DEFAULT_NODE_MEDIA_CLASSES); i++) {
    gboolean res = FALSE;

    if (!streams[i])
      continue;
    found = TRUE;

    g_signal_emit_by_name (def_streams_api, "set-default-configured-stream",
        DEFAULT_NODE_MEDIA_CLASSES[i], name,
        wp_stream_get_port_group (streams[i]), &res);
    if (!res) {
      fprintf (stderr, "failed to set default %s stream of node %u\n",
          DEFAULT_NODE_MEDIA_CLASSES[i], id);
      goto out;
    }
  }

  if (!found) {
    if (cmdline.set_default.stream)
      fprintf (stderr, "node %u has no device stream '%s'\n", id,
          cmdline.set_default.stream);
    else
      fprintf (stderr, "node %u has no device streams\n", id);
    goto out;
  }

  for (guint i = 0; i < G_N_ELEMENTS (streams); i++)
    g_clear_object (&streams[i]);
  wp_core_sync (self->core, NULL, (GAsyncReadyCallback) async_quit, self);
  return;

out:
  for (guint i = 0; i < G_N_ELEMENTS (streams); i++)
    g_clear_object (&streams[i]);
  self->exit_code = 3;
  g_main_loop_quit (self->loop);
}

/* set-volume */

static gboolean
set_volume_parse_positional (gint argc, gchar ** argv, GError **error)
{
  g_autoptr (GRegex) regex = NULL;
  g_autoptr (GMatchInfo) info = NULL;

  if (argc < 4) {
    g_set_error_literal (error, wpctl_error_domain_quark(), 0,
        "ID and VOL[%][-/+] are required");
    return FALSE;
  }

  regex = g_regex_new ("^(\\d*\\.?\\d*)(%?)([-+]?)$", 0, 0, NULL);

  if (g_regex_match(regex, argv[3], 0, &info)) {
    gchar *str = g_match_info_fetch (info, 1);
    cmdline.set_volume.volume = strtof (str, NULL);
    cmdline.set_volume.type = 'a';
    g_free (str);

    str = g_match_info_fetch (info, 2);
    if (g_strcmp0 (str, "%") == 0)
      cmdline.set_volume.volume /= 100;
    g_free (str);

    str = g_match_info_fetch (info, 3);
    if (g_strcmp0 (str, "-") == 0) {
      cmdline.set_volume.volume = -(cmdline.set_volume.volume);
      cmdline.set_volume.type = 's';
    } else if (g_strcmp0 (str, "+") == 0) {
      cmdline.set_volume.type = 's';
    }
    g_free (str);
  } else {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "Invalid volume argument. See wpctl set-volume --help");
    return FALSE;
  }

  return parse_id (!cmdline.set_volume.is_pid, false, argv[2],
      &cmdline.set_volume.id, error);
}

static gboolean
set_volume_prepare (WpCtl * self, GError ** error)
{
  /* the volume is set on the audio streams of the node */
  request_streams (self);
  wp_object_manager_add_interest (self->om, WP_TYPE_STREAM, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_NODE, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_CLIENT, NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_GLOBAL_PROXY,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  return TRUE;
}

static gboolean
do_set_volume (WpCtl * self, WpStream *stream)
{
  g_autoptr (WpPlugin) mixer_api = wp_plugin_find (self->core, "mixer-api");
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
  GVariant *variant = NULL;
  gboolean res = FALSE;
  gdouble curr_volume = 1.0;
  gdouble new_volume = cmdline.set_volume.volume;

  if (cmdline.set_volume.type == 's') {
    g_signal_emit_by_name (mixer_api, "get-volume", stream, &variant);
    if (!variant) {
      print_stream_unsupported (stream, "volume");
      return FALSE;
    }
    g_variant_lookup (variant, "volume", "d", &curr_volume);
    g_clear_pointer (&variant, g_variant_unref);

    new_volume += curr_volume;
  }
  if (new_volume < 0) {
    new_volume = 0.0;
  }
  if (cmdline.set_volume.limit > 0) {
    if (new_volume > cmdline.set_volume.limit) {
      new_volume = cmdline.set_volume.limit;
    }
  }

  g_variant_builder_add (&b, "{sv}", "volume",
      g_variant_new_double (new_volume));
  variant = g_variant_builder_end (&b);

  g_signal_emit_by_name (mixer_api, "set-volume", stream, variant, &res);
  if (!res) {
    print_stream_unsupported (stream, "volume");
    return FALSE;
  }

  return TRUE;
}

static void
set_volume_run (WpCtl * self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GPtrArray) streams = NULL;
  gboolean res = TRUE;

  streams = get_volume_streams (self, cmdline.set_volume.id,
      cmdline.set_volume.stream, cmdline.set_volume.is_pid, &error);
  if (!streams) {
    fprintf (stderr, "%s\n", error->message);
    goto out;
  }

  for (guint i = 0; i < streams->len; i++)
    res &= do_set_volume (self, g_ptr_array_index (streams, i));
  if (!res)
    goto out;

  wp_core_sync (self->core, NULL, (GAsyncReadyCallback) async_quit, self);
  return;

out:
  self->exit_code = 3;
  g_main_loop_quit (self->loop);
}

/* set-mute */

static gboolean
set_mute_parse_positional (gint argc, gchar ** argv, GError **error)
{
  if (argc < 4) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "ID and one of '1', '0' or 'toggle' are required");
    return FALSE;
  }

  if (!g_strcmp0 (argv[3], "1"))
    cmdline.set_mute.mute = 1;
  else if (!g_strcmp0 (argv[3], "0"))
    cmdline.set_mute.mute = 0;
  else if (!g_strcmp0 (argv[3], "toggle"))
    cmdline.set_mute.mute = 2;
  else {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "'%s' is not a valid mute option", argv[3]);
    return FALSE;
  }

  return parse_id (!cmdline.set_mute.is_pid, false, argv[2],
      &cmdline.set_mute.id, error);
}

static gboolean
set_mute_prepare (WpCtl * self, GError ** error)
{
  /* the mute is set on the audio streams of the node */
  request_streams (self);
  wp_object_manager_add_interest (self->om, WP_TYPE_STREAM, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_NODE, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_CLIENT, NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_GLOBAL_PROXY,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  return TRUE;
}

static gboolean
do_set_mute (WpCtl * self, WpStream *stream)
{
  g_autoptr (WpPlugin) mixer_api = wp_plugin_find (self->core, "mixer-api");
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
  GVariant *variant = NULL;
  gboolean res = FALSE;
  gboolean mute = FALSE;

  g_signal_emit_by_name (mixer_api, "get-volume", stream, &variant);
  if (!variant) {
    print_stream_unsupported (stream, "mute");
    return FALSE;
  }

  g_variant_lookup (variant, "mute", "b", &mute);
  g_clear_pointer (&variant, g_variant_unref);

  if (cmdline.set_mute.mute == 2)
    mute = !mute;
  else
    mute = !!cmdline.set_mute.mute;

  g_variant_builder_add (&b, "{sv}", "mute", g_variant_new_boolean (mute));
  variant = g_variant_builder_end (&b);

  g_signal_emit_by_name (mixer_api, "set-volume", stream, variant, &res);

  return TRUE;
}

static void
set_mute_run (WpCtl * self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GPtrArray) streams = NULL;
  gboolean res = TRUE;

  streams = get_volume_streams (self, cmdline.set_mute.id,
      cmdline.set_mute.stream, cmdline.set_mute.is_pid, &error);
  if (!streams) {
    fprintf (stderr, "%s\n", error->message);
    goto out;
  }

  for (guint i = 0; i < streams->len; i++)
    res &= do_set_mute (self, g_ptr_array_index (streams, i));
  if (!res)
    goto out;

  wp_core_sync (self->core, NULL, (GAsyncReadyCallback) async_quit, self);
  return;

out:
  self->exit_code = 3;
  g_main_loop_quit (self->loop);
}

/*
 * Return a human-readable SPA availability status.
 * SPA_PARAM_AVAILABILITY_unknown is not equivalent to unavailable.
 */
static const gchar *
availability_to_string (guint32 availability)
{
  switch (availability) {
  case SPA_PARAM_AVAILABILITY_yes:
    return "available";
  case SPA_PARAM_AVAILABILITY_no:
    return "unavailable";
  default:
    return "unknown";
  }
}

static const gchar *
direction_to_label (guint32 direction)
{
  switch (direction) {
  case SPA_DIRECTION_INPUT:
    return "[INPUT]";
  case SPA_DIRECTION_OUTPUT:
    return "[OUTPUT]";
  default:
    return "[UNKNOWN]";
  }
}

static const gchar *
direction_to_arrow (guint32 direction)
{
  switch (direction) {
  case SPA_DIRECTION_INPUT:
    return "└─<─ ";
  case SPA_DIRECTION_OUTPUT:
    return "└─>─ ";
  default:
    return "└─ ";
  }
}

/* Return the same device description that the default status view uses. */
static const gchar *
get_device_display_name (WpPipewireObject *device)
{
  const gchar *name =
      wp_pipewire_object_get_property (device, PW_KEY_DEVICE_DESCRIPTION);

  if (!name)
    name = wp_pipewire_object_get_property (device, PW_KEY_DEVICE_NICK);
  if (!name)
    name = wp_pipewire_object_get_property (device, PW_KEY_DEVICE_NAME);

  return name ? name : "(unnamed)";
}

/*
 * Read the currently selected Profile.index from cached device parameters.
 * Return -1 if no selected profile is exposed or parsing fails.
 */
static gint32
get_active_profile_index (WpPipewireObject *device)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  gint32 active_index = -1;

  it = wp_pipewire_object_enum_params_sync (device, "Profile", NULL);

  for (; it && wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSpaPod *pod = g_value_get_boxed (&val);
    gint32 index = -1;

    if (wp_spa_pod_get_object (pod, NULL,
            "index", "i", &index,
            NULL))
      active_index = index;
  }

  return active_index;
}

/* Find the user-facing description of a profile by its backend index. */
static gchar *
get_profile_display_name (WpPipewireObject *device, gint32 wanted_index)
{
  g_autoptr (WpIterator) it =
      wp_pipewire_object_enum_params_sync (device, "EnumProfile", NULL);
  g_auto (GValue) val = G_VALUE_INIT;

  for (; it && wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSpaPod *pod = g_value_get_boxed (&val);
    gint32 index = -1;
    const gchar *name = NULL;
    const gchar *description = NULL;

    if (wp_spa_pod_get_object (pod, NULL,
            "index", "i", &index,
            "name", "?s", &name,
            "description", "?s", &description,
            NULL) && index == wanted_index)
      return g_strdup (description ? description :
          (name ? name : "(unnamed)"));
  }

  return NULL;
}

/* get-profile */

static gboolean
get_profile_parse_positional (gint argc, gchar **argv, GError **error)
{
  if (argc < 3) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "DEVICE_ID is required");
    return FALSE;
  }

  /* Profiles belong to Device objects; default node aliases are not valid. */
  return parse_id (FALSE, FALSE, argv[2], &cmdline.get_profile.id, error);
}

static gboolean
get_profile_prepare (WpCtl *self, GError **error)
{
  wp_object_manager_add_interest (self->om, WP_TYPE_DEVICE, NULL);

  wp_object_manager_request_object_features (self->om, WP_TYPE_DEVICE,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL |
      WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROFILE);

  return TRUE;
}

static void
get_profile_run (WpCtl *self)
{
  g_autoptr (WpDevice) device = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  guint32 id = (guint32) cmdline.get_profile.id;
  gint32 active_index;
  guint count = 0;

  device = wp_object_manager_lookup (self->om, WP_TYPE_DEVICE,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", id, NULL);
  if (!device) {
    fprintf (stderr, "Device '%u' not found\n", id);
    self->exit_code = 3;
    goto out;
  }

  WpPipewireObject *obj = WP_PIPEWIRE_OBJECT (device);
  active_index = get_active_profile_index (obj);
  it = wp_pipewire_object_enum_params_sync (obj, "EnumProfile", NULL);
  if (!it) {
    fprintf (stderr, "Device '%u' does not expose cached profiles\n", id);
    self->exit_code = 3;
    goto out;
  }

  printf ("Profiles for device %u (%s):\n", id,
      get_device_display_name (obj));
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSpaPod *pod = g_value_get_boxed (&val);
    gint32 index = -1;
    const gchar *name = NULL;
    const gchar *description = NULL;
    guint32 available = SPA_PARAM_AVAILABILITY_unknown;
    gint32 priority = G_MININT32;

    if (!wp_spa_pod_get_object (pod, NULL,
            "index", "i", &index,
            "name", "?s", &name,
            "description", "?s", &description,
            "available", "?I", &available,
            "priority", "?i", &priority,
            NULL))
      continue;

    printf (" %c %d  %s [%s]  prio=",
        index == active_index ? '*' : ' ', index,
        description ? description : (name ? name : "(unnamed)"),
        availability_to_string (available));
    if (priority != G_MININT32)
      printf ("%d", priority);
    else
      printf ("?");
    printf ("\n");
    count++;
  }

  if (count == 0)
    printf ("  (no profiles enumerated)\n");
  if (active_index < 0)
    printf ("Active profile: not reported\n");

out:
  g_main_loop_quit (self->loop);
}

/* set-profile */

static gboolean
set_profile_parse_positional (gint argc, gchar ** argv, GError **error)
{
  if (argc < 4) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "ID and INDEX are required");
    return FALSE;
  }

  cmdline.set_profile.index = atoi (argv[3]);
  return parse_id (true, true, argv[2], &cmdline.set_profile.id, error);
}

static gboolean
set_profile_prepare (WpCtl * self, GError ** error)
{
  request_default_streams (self, cmdline.set_profile.id);
  wp_object_manager_add_interest (self->om, WP_TYPE_GLOBAL_PROXY, NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_GLOBAL_PROXY,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  return TRUE;
}

static void
set_profile_run (WpCtl * self)
{
  g_autoptr (WpPlugin) def_streams_api = wp_plugin_find (self->core, "default-streams-api");
  g_autoptr (WpPipewireObject) proxy = NULL;
  g_autoptr (GError) error = NULL;
  guint32 node_id;

  if (!translate_id (def_streams_api, cmdline.set_profile.id, &node_id,
          &error)) {
    fprintf(stderr, "Translate ID error: %s\n\n", error->message);
    goto out;
  }

  proxy = wp_object_manager_lookup (self->om, WP_TYPE_GLOBAL_PROXY,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "object.id", "=u", node_id, NULL);
  if (!proxy) {
    fprintf (stderr, "Object '%d' not found\n", node_id);
    goto out;
  }
  wp_pipewire_object_set_param (proxy, "Profile", 0,
      wp_spa_pod_new_object (
        "Spa:Pod:Object:Param:Profile", "Profile",
        "index", "i", cmdline.set_profile.index,
        "save", "b", TRUE,
        NULL));
  wp_core_sync (self->core, NULL, (GAsyncReadyCallback) async_quit, self);
  return;

out:
  self->exit_code = 3;
  g_main_loop_quit (self->loop);
}

/* get-route */

/* Parse decimal PipeWire properties such as "device.id" safely. */
static gboolean
parse_uint32_property (const gchar *str, guint32 *result)
{
  gchar *end = NULL;
  guint64 value;

  if (!str || !g_ascii_isdigit (str[0]))
    return FALSE;

  value = g_ascii_strtoull (str, &end, 10);
  if (*end != '\0' || value > G_MAXUINT32)
    return FALSE;

  *result = (guint32) value;
  return TRUE;
}

/*
 * Check if an optional SPA int array property contains the requested index.
 * A missing array is treated as 'unrestricted', by design.
 * A present but malformed array is rejected.
 */
static gboolean
route_array_matches (WpSpaPod *route, const gchar *property,
    gint32 wanted)
{
  g_autoptr (WpSpaPod) values = NULL;
  const struct spa_pod_array *array;
  const gint32 *entry;

  if (!wp_spa_pod_get_object (route, NULL,
          property, "?P", &values, NULL))
    return FALSE;

  if (!values)
    return TRUE;
  if (!wp_spa_pod_is_array (values))
    return FALSE;

  array = (const struct spa_pod_array *)
      wp_spa_pod_get_spa_pod (values);
  if (array->body.child.type != SPA_TYPE_Int ||
      array->body.child.size != sizeof (gint32))
    return FALSE;

  SPA_POD_ARRAY_FOREACH (array, entry) {
    if (*entry == wanted)
      return TRUE;
  }
  return FALSE;
}

/* Print optional SPA array values, such as devices=[19] or profiles=[240,237]. */
static void
print_int_array (WpSpaPod *route, const gchar *property)
{
  g_autoptr (WpSpaPod) values = NULL;
  const struct spa_pod_array *array;
  const gint32 *entry;
  gboolean first = TRUE;

  if (!wp_spa_pod_get_object (route, NULL,
          property, "?P", &values, NULL) || !values ||
      !wp_spa_pod_is_array (values)) {
    printf ("?");
    return;
  }
  array = (const struct spa_pod_array *)
      wp_spa_pod_get_spa_pod (values);
  if (array->body.child.type != SPA_TYPE_Int ||
      array->body.child.size != sizeof (gint32)) {
    printf ("?");
    return;
  }

  printf ("[");
  SPA_POD_ARRAY_FOREACH (array, entry) {
    printf ("%s%d", first ? "" : ",", *entry);
    first = FALSE;
  }
  printf ("]");
}

typedef struct {
  gint32 index;   /* selected Route.index */
  gint32 device;  /* logical card.profile.device index */
  gint32 profile; /* optional Route.profile; -1 when not reported */
} ActiveRoute;

/* A selected route is identified by the pair (index, logical device). */
static GArray *
collect_active_routes (WpPipewireObject *device)
{
  GArray *routes = g_array_new (FALSE, FALSE, sizeof (ActiveRoute));
  g_autoptr (WpIterator) it =
      wp_pipewire_object_enum_params_sync (device, "Route", NULL);
  g_auto (GValue) val = G_VALUE_INIT;

  for (; it && wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSpaPod *pod = g_value_get_boxed (&val);
    ActiveRoute selected = { .index = -1, .device = -1, .profile = -1 };

    if (wp_spa_pod_get_object (pod, NULL,
            "index", "i", &selected.index,
            "device", "i", &selected.device,
            "profile", "?i", &selected.profile,
            NULL) && selected.index >= 0 && selected.device >= 0)
      g_array_append_val (routes, selected);
  }
  return routes;
}

/* Look up the direction associated with a reported Route assignment. */
static guint32
get_active_route_direction (WpPipewireObject *device,
    const ActiveRoute *active, gint32 active_profile)
{
  g_autoptr (WpIterator) it =
      wp_pipewire_object_enum_params_sync (device, "EnumRoute", NULL);
  g_auto (GValue) val = G_VALUE_INIT;
  gint32 profile = active->profile >= 0 ? active->profile : active_profile;

  for (; it && wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSpaPod *pod = g_value_get_boxed (&val);
    gint32 index = -1;
    guint32 direction = SPA_ID_INVALID;

    if (!wp_spa_pod_get_object (pod, NULL,
            "index", "i", &index,
            "direction", "?I", &direction,
            NULL) || index != active->index ||
        !route_array_matches (pod, "devices", active->device) ||
        (profile >= 0 &&
            !route_array_matches (pod, "profiles", profile)))
      continue;

    return direction;
  }

  return SPA_ID_INVALID;
}

/*
 * Add context from active nodes that belong to this profile-device index.
 * More than one node may refer to the same index, so print every match.
 */
static void
print_logical_device_nodes (WpCtl *self, guint32 device_id,
    gint32 logical_device)
{
  g_autofree gchar *device_id_str = g_strdup_printf ("%u", device_id);
  g_autofree gchar *logical_device_str =
      g_strdup_printf ("%d", logical_device);
  g_autoptr (WpIterator) it = wp_object_manager_new_filtered_iterator (
      self->om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_DEVICE_ID, "=s", device_id_str,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "card.profile.device", "=s",
      logical_device_str,
      NULL);
  g_auto (GValue) val = G_VALUE_INIT;
  gboolean first = TRUE;

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpPipewireObject *node = g_value_get_object (&val);
    const gchar *media_class =
        wp_pipewire_object_get_property (node, PW_KEY_MEDIA_CLASS);
    const gchar *name = get_node_display_name (node, FALSE, FALSE);
    guint32 node_id = wp_proxy_get_bound_id (WP_PROXY (node));

    if (first) {
      printf ("[");
      if (media_class)
        printf ("%s,", media_class);
    } else {
      printf (";");
    }
    printf (" node %u:", node_id);
    if (name)
      printf (" %s", name);
    first = FALSE;
  }

  if (!first)
    printf ("]");
}

/*
 * An EnumRoute choice is marked active only when a reported Route assignment
 * applies to the CURRENT profile and the correct logical device. A Route
 * parameter may remain reported after set-profile changes the profile.
 *
 * 'profiles' is an association list (not Route.profile). Both conditions
 * matter: a matching route index alone does not establish current activity.
 */
static gboolean
route_is_active (WpSpaPod *route, gint32 route_index,
    const GArray *selected, gint32 filter_device, gint32 active_profile)
{
  /* Without a reported active profile we cannot assert current activity. */
  if (active_profile < 0 ||
      !route_array_matches (route, "profiles", active_profile))
    return FALSE;

  for (guint i = 0; i < selected->len; i++) {
    const ActiveRoute *active = &g_array_index (selected, ActiveRoute, i);
    if (active->index != route_index)
      continue;
    if (filter_device >= 0 && active->device != filter_device)
      continue;
    if (active->profile >= 0 && active->profile != active_profile)
      continue;
    if (route_array_matches (route, "devices", active->device))
      return TRUE;
  }
  return FALSE;
}

static gboolean
get_route_parse_positional (gint argc, gchar **argv, GError **error)
{
  if (argc < 3) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "DEVICE_OR_NODE_ID is required");
    return FALSE;
  }

  /* Default sink/source aliases resolve to node IDs. */
  return parse_id (TRUE, TRUE, argv[2], &cmdline.get_route.id, error);
}

static gboolean
get_route_prepare (WpCtl *self, GError **error)
{
  wp_object_manager_add_interest (self->om, WP_TYPE_NODE, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_DEVICE, NULL);

  wp_object_manager_request_object_features (self->om, WP_TYPE_NODE,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);

  wp_object_manager_request_object_features (self->om, WP_TYPE_DEVICE,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL |
      WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROFILE |
      WP_PIPEWIRE_OBJECT_FEATURE_PARAM_ROUTE);
  return TRUE;
}

static void
get_route_run (WpCtl *self)
{
  g_autoptr (WpPlugin) def_nodes_api =
      wp_plugin_find (self->core, "default-nodes-api");
  g_autoptr (WpDevice) device = NULL;
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (GArray) active_routes = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  g_autoptr (GError) error = NULL;
  guint32 id, device_id = 0, logical_id = 0;
  gint32 filtered_logical_device = -1;
  gint32 active_profile = -1;
  guint shown = 0;

  if (!translate_id (def_nodes_api, cmdline.get_route.id, &id, &error)) {
    fprintf (stderr, "ID error: %s\n", error->message);
    goto fail;
  }

  /* Try a device ID first: this is the unfiltered route inventory view. */
  device = wp_object_manager_lookup (self->om, WP_TYPE_DEVICE,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", id, NULL);

  if (!device) {
    /* Otherwise the supplied ID must identify a playback/capture node. */
    node = wp_object_manager_lookup (self->om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", id, NULL);
    if (!node) {
      fprintf (stderr, "No device or node with ID '%u'\n", id);
      goto fail;
    }

    WpPipewireObject *node_obj = WP_PIPEWIRE_OBJECT (node);
    if (!parse_uint32_property (
            wp_pipewire_object_get_property (node_obj, "device.id"),
            &device_id) ||
        !parse_uint32_property (
            wp_pipewire_object_get_property (node_obj, "card.profile.device"),
            &logical_id) ||
        logical_id > G_MAXINT32) {
      fprintf (stderr,
          "Node %u has no usable device.id/card.profile.device\n", id);
      goto fail;
    }
    filtered_logical_device = (gint32) logical_id;

    device = wp_object_manager_lookup (self->om, WP_TYPE_DEVICE,
        WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", device_id, NULL);
    if (!device) {
      fprintf (stderr, "Parent Device %u not found\n", device_id);
      goto fail;
    }
  } else {
    device_id = id;
  }

  WpPipewireObject *dev_obj = WP_PIPEWIRE_OBJECT (device);
  active_profile = get_active_profile_index (dev_obj);
  active_routes = collect_active_routes (dev_obj);

  if (node)
    printf ("Routes for node %u (parent device %u: %s):\n", id, device_id,
        get_device_display_name (dev_obj));
  else
    printf ("Routes for device %u (%s):\n", device_id,
        get_device_display_name (dev_obj));

  if (active_profile >= 0) {
    g_autofree gchar *active_profile_name =
        get_profile_display_name (dev_obj, active_profile);

    if (active_profile_name)
      printf ("Active profile index: %d (%s)\n", active_profile,
          active_profile_name);
    else
      printf ("Active profile index: %d\n", active_profile);
  } else {
    printf ("Active profile: not reported\n");
  }

  printf ("Reported Route assignments:\n");
  guint configured = 0;
  for (guint i = 0; i < active_routes->len; i++) {
    const ActiveRoute *active =
        &g_array_index (active_routes, ActiveRoute, i);
    guint32 direction;

    if (filtered_logical_device >= 0 &&
        active->device != filtered_logical_device)
      continue;
    direction = get_active_route_direction (dev_obj, active, active_profile);
    printf ("  logical device %d\t", active->device);
    print_logical_device_nodes (self, device_id, active->device);
    printf ("\n\t%sroute %d", direction_to_arrow (direction), active->index);
    if (active->profile >= 0) {
      g_autofree gchar *profile_name =
          get_profile_display_name (dev_obj, active->profile);
      const gchar *not_current =
          active_profile >= 0 && active->profile != active_profile ?
              ", not current" : "";

      if (profile_name)
        printf ("\t(profile %d: %s%s)", active->profile, profile_name,
            not_current);
      else
        printf ("\t(profile %d%s)", active->profile, not_current);
    }
    printf ("\n");
    configured++;
  }
  if (configured == 0)
    printf ("  (none reported)\n");

  it = wp_pipewire_object_enum_params_sync (dev_obj, "EnumRoute", NULL);
  if (!it) {
    fprintf (stderr, "No cached EnumRoute parameters on Device %u\n",
        device_id);
    goto fail;
  }

  printf ("Available route choices:\n");
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSpaPod *pod = g_value_get_boxed (&val);
    gint32 index = -1;
    const gchar *name = NULL;
    const gchar *description = NULL;
    guint32 available = SPA_PARAM_AVAILABILITY_unknown;
    guint32 direction = SPA_ID_INVALID;
    gint32 priority = G_MININT32;

    if (!wp_spa_pod_get_object (pod, NULL,
            "index", "i", &index,
            "name", "?s", &name,
            "description", "?s", &description,
            "available", "?I", &available,
            "direction", "?I", &direction,
            "priority", "?i", &priority,
            NULL))
      continue;

    if (filtered_logical_device >= 0) {
      /* Node view: match set-route's logical device context. */
      if (!route_array_matches (pod, "devices", filtered_logical_device))
        continue;
      if (active_profile >= 0 &&
          !route_array_matches (pod, "profiles", active_profile))
        continue;
    }

    printf (" %c %d  %-8s %s [%s]  prio=",
        route_is_active (pod, index, active_routes,
            filtered_logical_device, active_profile) ? '*' : ' ',
        index, direction_to_label (direction),
        description ? description : (name ? name : "(unnamed)"),
        availability_to_string (available));
    if (priority != G_MININT32)
      printf ("%d", priority);
    else
      printf ("?");

    /* On full-card inventory, explain *where* each route applies. */
    if (filtered_logical_device < 0) {
      printf (" devices=");
      print_int_array (pod, "devices");
      printf (" profiles=");
      print_int_array (pod, "profiles");
    }
    printf ("\n");
    shown++;
  }

  if (shown == 0)
    printf ("  (no matching routes; this can be normal)\n");

  g_main_loop_quit (self->loop);
  return;

fail:
  self->exit_code = 3;
  g_main_loop_quit (self->loop);
}

/* set-route */

static gboolean
set_route_parse_positional (gint argc, gchar ** argv, GError **error)
{
  if (argc < 4) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "ID and INDEX required");
    return FALSE;
  }

  cmdline.set_route.index = atoi (argv[3]);
  return parse_id (true, true, argv[2], &cmdline.set_route.id, error);
}

static gboolean
set_route_prepare (WpCtl * self, GError ** error)
{
  request_default_streams (self, cmdline.set_route.id);
  wp_object_manager_add_interest (self->om, WP_TYPE_GLOBAL_PROXY, NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_GLOBAL_PROXY,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  return TRUE;
}

static void
set_route_run (WpCtl * self)
{
  g_autoptr (WpPlugin) def_streams_api = wp_plugin_find (self->core, "default-streams-api");
  g_autoptr (WpPipewireObject) proxy = NULL;
  g_autoptr (WpPipewireObject) device_proxy = NULL;
  g_autoptr (GError) error = NULL;
  guint32 node_id;

  if (!translate_id (def_streams_api, cmdline.set_route.id, &node_id, &error)) {
    fprintf(stderr, "Translate ID error: %s\n\n", error->message);
    goto out;
  }

  proxy = wp_object_manager_lookup (self->om, WP_TYPE_GLOBAL_PROXY,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "object.id", "=u", node_id, NULL);
  if (!proxy) {
    fprintf (stderr, "Object '%d' not found\n", node_id);
    goto out;
  }

  const char * route_device_str = wp_pipewire_object_get_property(proxy, "card.profile.device");
  if (!route_device_str) {
    fprintf (stderr, "Property 'card.profile.device' not found\n");
    goto out;
  }
  guint32 route_device = atoi (route_device_str);

  const char * device_id_str = wp_pipewire_object_get_property (proxy, "device.id");
  if (!device_id_str) {
    fprintf (stderr, "Property 'device.id' not found\n");
    goto out;
  }
  guint32 device_id = atoi (device_id_str);

  device_proxy = wp_object_manager_lookup (self->om, WP_TYPE_GLOBAL_PROXY,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "object.id", "=u", device_id, NULL);
  if (!device_proxy) {
    fprintf (stderr, "Object '%d' not found\n", device_id);
    goto out;
  }

  wp_pipewire_object_set_param (device_proxy, "Route", 0,
      wp_spa_pod_new_object (
        "Spa:Pod:Object:Param:Route", "Route",
        "index", "i", cmdline.set_route.index,
        "device", "i", route_device,
        NULL));
  wp_core_sync (self->core, NULL, (GAsyncReadyCallback) async_quit, self);
  return;

out:
  self->exit_code = 3;
  g_main_loop_quit (self->loop);
}

/* clear-default */

static gboolean
clear_default_parse_positional (gint argc, gchar ** argv, GError **error)
{
  if (argc >= 3) {
    long id = strtol (argv[2], NULL, 10);
    if (id < 0 || id >= (long)G_N_ELEMENTS (DEFAULT_NODE_MEDIA_CLASSES)) {
      g_set_error (error, wpctl_error_domain_quark(), 0,
          "The setting ID value must be between 0 and %ld inclusive",
              G_N_ELEMENTS (DEFAULT_NODE_MEDIA_CLASSES) - 1);
      return FALSE;
    }
    cmdline.clear_default.id = id;
  } else {
    cmdline.clear_default.id = SPA_ID_INVALID;
  }

  return TRUE;
}

static gboolean
clear_default_prepare (WpCtl * self, GError ** error)
{
  return TRUE;
}

static void
clear_default_run (WpCtl * self)
{
  g_autoptr (WpPlugin) def_streams_api = NULL;
  gboolean res = FALSE;

  def_streams_api = wp_plugin_find (self->core, "default-streams-api");
  if (!def_streams_api) {
    fprintf (stderr, "Default streams API not loaded\n");
    goto out;
  }

  /* clear all defaults if id was not given */
  if (cmdline.clear_default.id == SPA_ID_INVALID) {
    for (guint i = 0; i < G_N_ELEMENTS (DEFAULT_NODE_MEDIA_CLASSES); i++) {
      g_signal_emit_by_name (def_streams_api, "set-default-configured-stream",
          DEFAULT_NODE_MEDIA_CLASSES[i], NULL, NULL, &res);
      if (!res) {
        fprintf (stderr, "failed to clear default configured node (%s)\n",
            DEFAULT_NODE_MEDIA_CLASSES[i]);
        goto out;
      }
    }
  } else {
    g_signal_emit_by_name (def_streams_api, "set-default-configured-stream",
        DEFAULT_NODE_MEDIA_CLASSES[cmdline.clear_default.id], NULL, NULL,
        &res);
    if (!res) {
      fprintf (stderr, "failed to clear default configured node (%s)\n",
          DEFAULT_NODE_MEDIA_CLASSES[cmdline.clear_default.id]);
      goto out;
    }
  }

  wp_core_sync (self->core, NULL, (GAsyncReadyCallback) async_quit, self);
  return;

out:
  self->exit_code = 3;
  g_main_loop_quit (self->loop);
}

/* settings */

static gboolean
settings_parse_positional (gint argc, gchar ** argv, GError **error)
{
  cmdline.settings.key = NULL;
  cmdline.settings.val = NULL;
  if (argc >= 3) {
    cmdline.settings.key = argv[2];
    if (argc >= 4)
      cmdline.settings.val = argv[3];
  }

  if (cmdline.settings.delete && cmdline.settings.save) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
                   "Cannot use --delete and --save flags at the same time");
    return FALSE;
  }
  if (cmdline.settings.delete && cmdline.settings.reset) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
                   "Cannot use --delete and --reset flags at the same time");
    return FALSE;
  }
  if (cmdline.settings.save && cmdline.settings.reset) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
                   "Cannot use --save and --reset flags at the same time");
    return FALSE;
  }

  return TRUE;
}

static gboolean
settings_prepare (WpCtl * self, GError ** error)
{
  wp_object_manager_add_interest (self->om, WP_TYPE_METADATA, NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_METADATA,
      WP_OBJECT_FEATURES_ALL);
  return TRUE;
}

static const char *
settings_spec_type_to_string (WpSettingsSpecType type)
{
  switch (type) {
    case WP_SETTINGS_SPEC_TYPE_BOOL:
      return "Boolean";
    case WP_SETTINGS_SPEC_TYPE_INT:
      return "Integer";
    case WP_SETTINGS_SPEC_TYPE_FLOAT:
      return "Float";
    case WP_SETTINGS_SPEC_TYPE_STRING:
      return "String";
    case WP_SETTINGS_SPEC_TYPE_ARRAY:
      return "Array";
    case WP_SETTINGS_SPEC_TYPE_OBJECT:
      return "Object";
    case WP_SETTINGS_SPEC_TYPE_UNKNOWN:
    default:
      break;
  }
  return "Uknown";
}

static void
print_setting (WpSettings *s, const gchar *key)
{
  g_autoptr (WpSpaJson) value = NULL;
  g_autoptr (WpSpaJson) saved = NULL;
  g_autoptr (WpSettingsSpec) spec = NULL;
  const gchar *name;
  const gchar *desc;
  WpSettingsSpecType val_type;
  g_autoptr (WpSpaJson) def = NULL;
  g_autoptr (WpSpaJson) min = NULL;
  g_autoptr (WpSpaJson) max = NULL;

  value = wp_settings_get (s, key);
  saved = wp_settings_get_saved (s, key);
  spec = wp_settings_get_spec (s, key);
  name = wp_settings_spec_get_name (spec);
  desc = wp_settings_spec_get_description (spec);
  val_type = wp_settings_spec_get_value_type (spec);
  def = wp_settings_spec_get_default_value (spec);
  min = wp_settings_spec_get_min_value (spec);
  max = wp_settings_spec_get_max_value (spec);

  /* print key */
  printf ("- Id: %s\n", key);

  /* print spec */
  if (name)
    printf ("  Name: %s\n", dgettext (GETTEXT_PACKAGE, name));
  printf ("  Desc: %s\n", dgettext (GETTEXT_PACKAGE, desc));
  printf ("  Type: %s\n", settings_spec_type_to_string (val_type));
  printf ("  Default: %s", wp_spa_json_get_data (def));
  if (min && max)
    printf ("\t[Min: %s, Max: %s]", wp_spa_json_get_data (min),
        wp_spa_json_get_data (max));
  printf ("\n");
  printf ("  Value: %s", wp_spa_json_get_data (value));
  if (saved)
    printf ("\t[Saved: %s]", wp_spa_json_get_data (saved));
  printf ("\n\n");
}

static void
print_settings (WpSettings *s)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  printf ("Settings:\n\n");

  it = wp_settings_new_iterator (s);
  while (wp_iterator_next (it, &item)) {
    WpSettingsItem *si = g_value_get_boxed (&item);
    const gchar *key = wp_settings_item_get_key (si);
    print_setting (s, key);
    g_value_unset (&item);
  }
}

static void
settings_run (WpCtl * self)
{
  g_autoptr (WpSettings) s = NULL;
  const gchar *key = cmdline.settings.key, *val = cmdline.settings.val;
  gboolean delete_flag = cmdline.settings.delete;
  gboolean save_flag = cmdline.settings.save;
  gboolean reset_flag = cmdline.settings.reset;

  s = wp_settings_find (self->core, NULL);
  if (!s) {
    printf ("Could not find registered settings\n");
    goto out;
  }

  /* If no key or value are provided */
  if (!key && !val) {
    if (!delete_flag && !save_flag && !reset_flag) {
      print_settings (s);
    } else if (!delete_flag && save_flag && !reset_flag) {
      wp_settings_save_all (s);
      fprintf (stderr, "Saved all settings\n");
    } else if (delete_flag && !save_flag && !reset_flag) {
      wp_settings_delete_all (s);
      fprintf (stderr, "Deleted all saved settings\n");
    } else if (!delete_flag && !save_flag && reset_flag) {
      wp_settings_reset_all (s);
      fprintf (stderr, "Reset all settings\n");
    } else {
      g_assert_not_reached ();
    }
  }

  /* If key is only provided */
  else if (key && !val) {
    if (!delete_flag && !save_flag && !reset_flag) {
      g_autoptr (WpSpaJson) value = NULL;
      value = wp_settings_get (s, key);
      if (value) {
        g_autoptr (WpSpaJson) saved = wp_settings_get_saved (s, key);
        printf ("Value: %s", wp_spa_json_get_data (value));
        if (saved)
          printf (" (Saved: %s)", wp_spa_json_get_data (saved));
        printf ("\n");
      } else {
        printf ("Setting '%s' not found\n", key);
      }
    } else if (!delete_flag && save_flag && !reset_flag) {
      if (wp_settings_save (s, key))
        printf ("Saved setting '%s' successfully\n", key);
      else
        printf ("Setting '%s' not found\n", key);
    } else if (delete_flag && !save_flag && !reset_flag) {
      if (wp_settings_delete (s, key))
        printf ("Deleted setting '%s' successfully\n", key);
      else
        printf ("Setting '%s' not found\n", key);
    } else if (!delete_flag && !save_flag && reset_flag) {
      if (wp_settings_reset (s, key))
        printf ("Reset setting '%s' successfully\n", key);
      else
        printf ("Setting '%s' not found\n", key);
    } else {
      g_assert_not_reached ();
    }
  }

  /* If both key and value are provided */
  else if (key && val) {
    if (!delete_flag && !save_flag && !reset_flag) {
      g_autoptr (WpSpaJson) value = wp_spa_json_new_from_string (val);
      if (wp_settings_set (s, key, value))
        printf ("Updated setting '%s' to: %s\n", key, val);
      else
        printf ("Failed to set setting '%s' to: %s\n", key, val);
    } else if (!delete_flag && save_flag && !reset_flag) {
      g_autoptr (WpSpaJson) value = wp_spa_json_new_from_string (val);
      if (wp_settings_set (s, key, value) && wp_settings_save (s, key))
        printf ("Updated and saved setting '%s' to: %s\n", key, val);
      else
        printf ("Failed to update and save setting '%s' to: %s\n", key, val);
    } else if (delete_flag && !save_flag && !reset_flag) {
      if (wp_settings_delete (s, key))
        printf ("Deleted setting '%s' successfully\n", key);
      else
        printf ("Setting %s not found\n", key);
    } else if (!delete_flag && !save_flag && reset_flag) {
      if (wp_settings_reset (s, key))
        printf ("Reset setting '%s' successfully\n", key);
      else
        printf ("Setting '%s' not found\n", key);
    } else {
      g_assert_not_reached ();
    }
  } else {
    g_assert_not_reached ();
  }

  wp_core_sync (self->core, NULL, (GAsyncReadyCallback) async_quit, self);
  return;

out:
  self->exit_code = 3;
  g_main_loop_quit (self->loop);
}

/* set-log-level */

static gboolean
set_log_level_parse_positional (gint argc, gchar ** argv, GError **error)
{
  if (argc == 4) {
    if (!spa_atou64 (argv[2], &cmdline.set_log_level.id, 10)) {
      g_set_error (error, wpctl_error_domain_quark(), 0,
                   "failed to parse client id");
      return FALSE;
    }
    cmdline.set_log_level.level = argv[3];
  } else if (argc == 3) {
    cmdline.set_log_level.id = SPA_ID_INVALID;
    cmdline.set_log_level.level = argv[2];
  } else {
    g_set_error (error, wpctl_error_domain_quark(), 0,
                 "wrong number of arguments for set-log-level");
    return FALSE;
  }

  if (spa_streq(cmdline.set_log_level.level, "-"))
    cmdline.set_log_level.level = NULL;

  return TRUE;
}

static gboolean
set_log_level_prepare (WpCtl * self, GError ** error)
{
  wp_object_manager_add_interest (self->om, WP_TYPE_METADATA,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      "metadata.name", "=s", "settings",
      NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_CLIENT,
      WP_CONSTRAINT_TYPE_PW_PROPERTY,
      "wireplumber.daemon", "+",
      NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_METADATA,
      WP_OBJECT_FEATURES_ALL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_CLIENT,
      WP_OBJECT_FEATURES_ALL);
  return TRUE;
}

static void
set_log_level_run (WpCtl * self)
{
  g_autoptr (WpPlugin) def_streams_api = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpIterator) client_it = NULL;
  g_auto (GValue) client_val = G_VALUE_INIT;

  g_autoptr (WpMetadata) settings = wp_object_manager_lookup (self->om, WP_TYPE_METADATA, NULL);
  if (!settings) {
    fprintf (stderr, "No settings metadata found\n");
    goto out;
  }

  if (cmdline.set_log_level.id == SPA_ID_INVALID)
    client_it = wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_CLIENT, NULL);

  if (client_it) {
      for (; wp_iterator_next (client_it, &client_val); g_value_unset (&client_val)) {
        WpPipewireObject *client = g_value_get_object (&client_val);
        guint32 client_id = wp_proxy_get_bound_id (WP_PROXY (client));

        if (client_id == SPA_ID_INVALID)
          continue;

        wp_metadata_set (settings, client_id, "log.level", "", cmdline.set_log_level.level);
      }
  } else {
    wp_metadata_set (settings, cmdline.set_log_level.id, "log.level", "", cmdline.set_log_level.level);
  }

  wp_core_sync (self->core, NULL, (GAsyncReadyCallback) async_quit, self);
  return;

out:
  self->exit_code = 3;
  g_main_loop_quit (self->loop);
}

/* reset */

/* Collect all paths under `file` in post-order (children before their parent directory) */
static gboolean
collect_paths_recursive (GFile *file, GPtrArray *paths, GError **error)
{
  GFileType type = g_file_query_file_type (file,
      G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL);

  if (type == G_FILE_TYPE_DIRECTORY) {
    g_autoptr (GFileEnumerator) enumerator = g_file_enumerate_children (file,
        G_FILE_ATTRIBUTE_STANDARD_NAME,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
    if (!enumerator)
      return FALSE;

    while (TRUE) {
      GFileInfo *info = g_file_enumerator_next_file (enumerator, NULL, error);
      if (!info) {
        if (*error)
          return FALSE;
        break;
      }
      const gchar *name = g_file_info_get_name (info);
      g_autoptr (GFile) child = g_file_get_child (file, name);
      g_object_unref (info);
      if (!collect_paths_recursive (child, paths, error))
        return FALSE;
    }
    if (!g_file_enumerator_close (enumerator, NULL, error))
      return FALSE;
  }

  /* Add this entry after its children */
  g_ptr_array_add (paths, g_file_get_path (file));
  return TRUE;
}

static gboolean
systemctl_user (const gchar *action, const gchar *service)
{
  const gchar *argv[] = { "systemctl", "--user", action, service, NULL };
  gint exit_status;
  g_autoptr (GError) error = NULL;

  if (!g_spawn_sync (NULL, (gchar **) argv, NULL,
          G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
          NULL, NULL, NULL, NULL, &exit_status, &error)) {
    fprintf (stderr, "Warning: could not run systemctl: %s\n", error->message);
    return FALSE;
  }
  if (!g_spawn_check_exit_status (exit_status, &error)) {
    fprintf (stderr, "Warning: systemctl --user %s %s failed: %s\n",
        action, service, error->message);
    return FALSE;
  }
  return TRUE;
}

static gchar *
reset_get_state_dir (const gchar *subdir)
{
  const gchar *base = g_getenv ("XDG_STATE_HOME");
  if (base)
    return g_build_filename (base, subdir, NULL);
  return g_build_filename (g_get_home_dir (), ".local", "state", subdir, NULL);
}

static gboolean
reset_parse_positional (gint argc, gchar ** argv, GError **error)
{
  return TRUE;
}

static gboolean
reset_prepare (WpCtl * self, GError ** error)
{
  return TRUE;
}

static void
reset_run (WpCtl * self)
{
  gboolean remove_wp_config = cmdline.reset.wp_config || cmdline.reset.all;
  gboolean remove_pw_config = cmdline.reset.pw_config || cmdline.reset.all;
  gboolean dry_run = cmdline.reset.dry_run;
  gboolean yes = cmdline.reset.yes;
  gboolean do_restart = !cmdline.reset.no_restart && !dry_run;

  g_autofree gchar *wp_state_dir = reset_get_state_dir ("wireplumber");
  g_autofree gchar *wp_config_dir = remove_wp_config ?
      g_build_filename (g_get_user_config_dir (), "wireplumber", NULL) : NULL;
  g_autofree gchar *pw_config_dir = remove_pw_config ?
      g_build_filename (g_get_user_config_dir (), "pipewire", NULL) : NULL;

  /* Collect all paths to be removed */
  g_autoptr (GPtrArray) paths =
      g_ptr_array_new_with_free_func (g_free);

  const gchar *dirs[] = { wp_state_dir, wp_config_dir, pw_config_dir };
  for (guint i = 0; i < G_N_ELEMENTS (dirs); i++) {
    if (!dirs[i])
      continue;
    g_autoptr (GFile) file = g_file_new_for_path (dirs[i]);
    if (!g_file_query_exists (file, NULL))
      continue;
    g_autoptr (GError) error = NULL;
    if (!collect_paths_recursive (file, paths, &error)) {
      fprintf (stderr, "Error listing '%s': %s\n", dirs[i], error->message);
      self->exit_code = 3;
      return;
    }
  }

  if (paths->len == 0) {
    printf ("Nothing to remove.\n");
    return;
  }

  /* Print what will be removed */
  if (dry_run)
    printf ("Dry run - no files will be deleted and no services will be restarted\n\n");
  printf ("The following files will be removed:\n");
  for (guint i = 0; i < paths->len; i++)
    printf ("  %s\n", (gchar *) g_ptr_array_index (paths, i));

  if (dry_run)
    return;

  if (!yes) {
    /* Ask for confirmation */
    printf ("\nWARNING: this action is not reversible.\n");
    printf ("Proceed? [y/N] ");
    fflush (stdout);

    char buf[8] = {0};
    if (!fgets (buf, sizeof (buf), stdin) || (buf[0] != 'y' && buf[0] != 'Y')) {
      printf ("Aborted.\n");
      return;
    }
  }

  /* Stop wireplumber before removing its files */
  if (do_restart)
    systemctl_user ("stop", "wireplumber");

  /* Delete all collected paths in order (children before their parent) */
  for (guint i = 0; i < paths->len; i++) {
    const gchar *path = g_ptr_array_index (paths, i);
    g_autoptr (GFile) file = g_file_new_for_path (path);
    g_autoptr (GError) error = NULL;
    if (!g_file_delete (file, NULL, &error)) {
      fprintf (stderr, "Failed to remove '%s': %s\n", path, error->message);
      self->exit_code = 3;
    }
  }

  /* Restart pipewire if its config was removed */
  if (do_restart && remove_pw_config)
    systemctl_user ("restart", "pipewire");

  /* Start wireplumber again */
  if (do_restart)
    systemctl_user ("start", "wireplumber");
}

#define N_ENTRIES 7

static const struct subcommand {
  /* the name to match on the command line */
  const gchar *name;
  /* description of positional arguments, shown in the help message */
  const gchar *positional_args;
  /* short description, shown at the top of the help message */
  const gchar *summary;
  /* long description, shown at the bottom of the help message */
  const gchar *description;
  /* if TRUE, run the subcommand without connecting to PipeWire */
  gboolean no_connect;
  /* additional cmdline arguments for this subcommand */
  const GOptionEntry entries[N_ENTRIES];
  /* function to parse positional arguments */
  gboolean (*parse_positional) (gint, gchar **, GError **);
  /* function to prepare the object manager */
  gboolean (*prepare) (WpCtl *, GError **);
  /* function to run after the object manager is installed */
  void (*run) (WpCtl *);
} subcommands[] = {
  {
    .name = "status",
    .positional_args = "",
    .summary = "Displays the current state of objects in PipeWire",
    .description = NULL,
    .entries = {
      { "nick", 'k', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.status.display_nicknames,
        "Display device and node nicknames instead of descriptions", NULL },
      { "name", 'n', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.status.display_names,
        "Display device and node names instead of descriptions", NULL },
      { NULL }
    },
    .parse_positional = NULL,
    .prepare = status_prepare,
    .run = status_run,
  },
  {
    .name = "list",
    .positional_args = "[audio|video] [devices|sinks|sources]",
    .summary = "Displays PipeWire objects, optionally filtered by media and object type",
    .description = NULL,
    .entries = { { NULL } },
    .parse_positional = list_parse_positional,
    .prepare = list_prepare,
    .run = list_run,
  },
  {
    .name = "get-volume",
    .positional_args = "ID",
    .summary = "Displays volume information about the audio streams of the "
               "specified node in PipeWire",
    .description = NULL,
    .entries = {
      { "stream", 's', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
        &cmdline.get_volume.stream,
        "Only uses the audio stream of ID with this port group",
        "PORT_GROUP" },
      { NULL }
    },
    .parse_positional = get_volume_parse_positional,
    .prepare = get_volume_prepare,
    .run = get_volume_run,
  },
  {
    .name = "inspect",
    .positional_args = "ID",
    .summary = "Displays information about the specified object",
    .description = NULL,
    .entries = {
      { "referenced", 'r', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.inspect.show_referenced,
        "Show objects that are referenced in properties", NULL },
      { "associated", 'a', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.inspect.show_associated, "Show associated objects", NULL },
      { NULL }
    },
    .parse_positional = inspect_parse_positional,
    .prepare = inspect_prepare,
    .run = inspect_run,
  },
  {
    .name = "set-default",
    .positional_args = "ID",
    .summary = "Sets the streams of ID to be the default streams of their "
               "kind (capture/playback) in its session",
    .description = NULL,
    .entries = {
      { "stream", 's', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
        &cmdline.set_default.stream,
        "Only sets the stream of ID with this port group as default",
        "PORT_GROUP" },
      { NULL }
    },
    .parse_positional = set_default_parse_positional,
    .prepare = set_default_prepare,
    .run = set_default_run,
  },
  {
    .name = "set-volume",
    .positional_args = "ID VOL[%][-/+]",
    .summary =
        "Sets the volume of ID from specified argument. (floating point, 1.0 is 100%)\n"
        "  VOL%[-/+] - Step up/down volume by specified percent (Example: 0.5%+)\n"
        "  VOL[-/+] - Step up/down volume by specified value (Example: 0.5+)\n"
        "  VOL - Set volume as the specified value (Example: 0.5)",
    .description = NULL,
    .entries = {
      { "pid", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.set_volume.is_pid,
        "Selects all nodes associated to the given PID number", NULL },
      { "limit", 'l', G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE,
        &cmdline.set_volume.limit,
        "Limits the final volume percentage to below this value. (floating point, 1.0 is 100%)", NULL },
      { "stream", 's', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
        &cmdline.set_volume.stream,
        "Only uses the audio stream of ID with this port group",
        "PORT_GROUP" },
      { NULL }
    },
    .parse_positional = set_volume_parse_positional,
    .prepare = set_volume_prepare,
    .run = set_volume_run,
  },
  {
    .name = "set-mute",
    .positional_args = "ID 1|0|toggle",
    .summary = "Changes the mute state of ID",
    .description = NULL,
    .entries = {
      { "pid", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.set_mute.is_pid,
        "Selects all nodes associated to the given PID number", NULL },
      { "stream", 's', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
        &cmdline.set_mute.stream,
        "Only uses the audio stream of ID with this port group",
        "PORT_GROUP" },
      { NULL }
    },
    .parse_positional = set_mute_parse_positional,
    .prepare = set_mute_prepare,
    .run = set_mute_run,
  },
  {
    .name = "get-profile",
    .positional_args = "DEVICE_ID",
    .summary = "Shows active and available profiles for a device",
    .description = NULL,
    .entries = { { NULL } },
    .parse_positional = get_profile_parse_positional,
    .prepare = get_profile_prepare,
    .run = get_profile_run,
  },
  {
    .name = "set-profile",
    .positional_args = "ID INDEX",
    .summary = "Sets the profile of ID to INDEX (integer, 0 is 'off')",
    .description = NULL,
    .entries = { { NULL } },
    .parse_positional = set_profile_parse_positional,
    .prepare = set_profile_prepare,
    .run = set_profile_run,
  },
  {
    .name = "get-route",
    .positional_args = "DEVICE_OR_NODE_ID",
    .summary = "Shows active and available routes for a device or node",
    .description = NULL,
    .entries = { { NULL } },
    .parse_positional = get_route_parse_positional,
    .prepare = get_route_prepare,
    .run = get_route_run,
  },
  {
    .name = "set-route",
    .positional_args = "ID INDEX",
    .summary = "Sets the route of ID to INDEX (integer, 0 is 'off')",
    .description = NULL,
    .entries = { { NULL } },
    .parse_positional = set_route_parse_positional,
    .prepare = set_route_prepare,
    .run = set_route_run,
  },
  {
    .name = "clear-default",
    .positional_args = "[ID]",
    .summary = "Clears the default configured node (no ID means 'all')",
    .description = NULL,
    .entries = { { NULL } },
    .parse_positional = clear_default_parse_positional,
    .prepare = clear_default_prepare,
    .run = clear_default_run,
  },
  {
    .name = "settings",
    .positional_args = "[KEY] [VAL]",
    .summary = "Shows, changes or removes settings",
    .description = NULL,
    .entries = {
      { "delete", 'd', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.settings.delete,
        "Deletes the saved setting value (no KEY means 'all')", NULL },
      { "save", 's', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.settings.save,
        "Saves the setting value (no KEY means 'all', no VAL means current value)", NULL },
      { "reset", 'r', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.settings.reset,
        "Resets the saved setting to its default value", NULL },
      { NULL }
    },
    .parse_positional = settings_parse_positional,
    .prepare = settings_prepare,
    .run = settings_run,
  },
  {
    .name = "set-log-level",
    .positional_args = "[ID] LEVEL",
    .summary = "Sets the log level of a client (no ID means Wireplumber, 0 means Pipewire server)",
    .description = NULL,
    .entries = { { NULL } },
    .parse_positional = set_log_level_parse_positional,
    .prepare = set_log_level_prepare,
    .run = set_log_level_run,
  },
  {
    .name = "reset",
    .positional_args = "",
    .summary = "Resets WirePlumber (and optionally PipeWire) to defaults by removing state and config files",
    .description = NULL,
    .no_connect = TRUE,
    .entries = {
      { "wireplumber-config", 'c', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.reset.wp_config,
        "Also remove WirePlumber config files (~/.config/wireplumber)", NULL },
      { "pipewire-config", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.reset.pw_config,
        "Also remove PipeWire config files (~/.config/pipewire) and restart pipewire", NULL },
      { "all", 'a', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.reset.all,
        "Remove all state and config files", NULL },
      { "no-restart", 'n', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.reset.no_restart,
        "Do not restart services after removing files", NULL },
      { "dry-run", 'd', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.reset.dry_run,
        "Show what would be deleted without actually deleting anything", NULL },
      { "yes", 'y', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.reset.yes,
        "Proceed without prompting for confirmation", NULL },
      { NULL }
    },
    .parse_positional = reset_parse_positional,
    .prepare = reset_prepare,
    .run = reset_run,
  },
};

static void
on_settings_activated (WpSettings *s, GAsyncResult *res, WpCtl *ctl)
{
  GError *error = NULL;

  if (!wp_object_activate_finish (WP_OBJECT (s), res, &error)) {
    fprintf (stderr, "%s\n", error->message);
    ctl->exit_code = 1;
    g_main_loop_quit (ctl->loop);
    return;
  }

  wp_core_register_object (ctl->core, g_object_ref (s));
}

static void
on_plugin_loaded (WpCore * core, GAsyncResult * res, WpCtl *ctl)
{
  GError *error = NULL;

  if (!wp_core_load_component_finish (core, res, &error)) {
    fprintf (stderr, "%s\n", error->message);
    ctl->exit_code = 1;
    g_main_loop_quit (ctl->loop);
    return;
  }

  on_plugin_ready (ctl);
}

static void
on_plugin_ready (WpCtl * ctl)
{
  if (--ctl->pending_plugins == 0) {
    g_autoptr (WpPlugin) mixer_api = wp_plugin_find (ctl->core, "mixer-api");
    g_object_set (mixer_api, "scale", 1 /* cubic */, NULL);
    wp_core_install_object_manager (ctl->core, ctl->om);
  }
}

static void
on_core_activated (WpObject * core, GAsyncResult * res, WpCtl * ctl)
{
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (core, res, &error)) {
    fprintf (stderr, "%s\n", error->message);
    ctl->exit_code = 2;
    g_main_loop_quit (ctl->loop);
  }
}

gint
main (gint argc, gchar **argv)
{
  g_auto (WpCtl) ctl = {0};
  const struct subcommand *cmd = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *summary = NULL;
  g_autoptr (WpSettings) settings = NULL;

  setlocale (LC_ALL, "");
  setlocale (LC_NUMERIC, "C");
  wp_init (WP_INIT_ALL);

  ctl.context = g_option_context_new (
      "COMMAND [COMMAND_OPTIONS] - WirePlumber Control CLI");
  ctl.loop = g_main_loop_new (NULL, FALSE);
  ctl.core = wp_core_new (NULL, NULL, wp_properties_new (PW_KEY_REMOTE_NAME,
          ("[" PW_DEFAULT_REMOTE "-manager," PW_DEFAULT_REMOTE "]"), NULL));
  ctl.om = wp_object_manager_new ();

  /* find the subcommand */
  if (argc > 1) {
    for (guint i = 0; i < G_N_ELEMENTS (subcommands); i++) {
      if (!g_strcmp0 (argv[1], subcommands[i].name)) {
        cmd = &subcommands[i];
        break;
      }
    }
  }

  /* prepare the subcommand options */
  if (cmd) {
    GOptionGroup *group;

    /* options */
    group = g_option_group_new (cmd->name, NULL, NULL, &ctl, NULL);
    g_option_group_add_entries (group, cmd->entries);
    g_option_context_set_main_group (ctl.context, group);

    /* summary */
    summary = g_strdup_printf ("Command: %s %s\n  %s",
        cmd->name, cmd->positional_args, cmd->summary);
    g_option_context_set_summary (ctl.context, summary);

    /* description */
    if (cmd->description)
      g_option_context_set_description (ctl.context, cmd->description);
  }
  else {
    /* build the generic summary */
    GString *summary_str = g_string_new ("Commands:");
    for (guint i = 0; i < G_N_ELEMENTS (subcommands); i++) {
      g_string_append_printf (summary_str, "\n  %s %s", subcommands[i].name,
          subcommands[i].positional_args);
    }
    summary = g_string_free (summary_str, FALSE);
    g_option_context_set_summary (ctl.context, summary);
    g_option_context_set_description (ctl.context, "Pass -h after a command "
        "to see command-specific options\n");
  }

  /* parse options */
  if (!g_option_context_parse (ctl.context, &argc, &argv, &error) ||
      (cmd && cmd->parse_positional &&
          !cmd->parse_positional (argc, argv, &error))) {
    fprintf (stderr, "Error: %s\n\n", error->message);
    cmd = NULL;
  }

  /* no active subcommand, show usage and exit */
  if (!cmd) {
    g_autofree gchar *help =
        g_option_context_get_help (ctl.context, FALSE, NULL);
    printf ("%s", help);
    return 1;
  }

  /* prepare the subcommand */
  if (!cmd->prepare (&ctl, &error)) {
    fprintf (stderr, "%s\n", error->message);
    return 1;
  }

  /* if the subcommand does not need PipeWire, run it directly */
  if (cmd->no_connect) {
    cmd->run (&ctl);
    return ctl.exit_code;
  }

  /* load and register settings */
  settings = wp_settings_new (ctl.core, NULL);
  wp_object_activate (WP_OBJECT (settings),
      WP_OBJECT_FEATURES_ALL,
      NULL,
      (GAsyncReadyCallback)on_settings_activated,
      &ctl);

  /* load required API modules */
  ctl.pending_plugins++;
  wp_core_load_component (ctl.core, "libwireplumber-module-default-streams-api",
      "module", NULL, NULL, NULL, (GAsyncReadyCallback) on_plugin_loaded, &ctl);
  ctl.pending_plugins++;
  wp_core_load_component (ctl.core, "libwireplumber-module-mixer-api",
      "module", NULL, NULL, NULL, (GAsyncReadyCallback) on_plugin_loaded, &ctl);

  /* connect */
  wp_object_activate (WP_OBJECT (ctl.core), WP_CORE_FEATURE_CONNECTED, NULL,
      (GAsyncReadyCallback) on_core_activated, &ctl);

  /* run */
  g_signal_connect_swapped (ctl.core, "disconnected",
      (GCallback) g_main_loop_quit, ctl.loop);
  g_signal_connect_swapped (ctl.om, "installed",
      (GCallback) cmd->run, &ctl);

  g_main_loop_run (ctl.loop);

  return ctl.exit_code;
}
