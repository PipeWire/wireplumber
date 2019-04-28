/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <wp/plugin.h>
#include <wp/session.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#define MIN_QUANTUM_SIZE  64
#define MAX_QUANTUM_SIZE  1024

G_DECLARE_FINAL_TYPE (DefaultSession, session, DEFAULT, SESSION, WpSession)
G_DECLARE_FINAL_TYPE (DefaultSessionPlugin, plugin, DEFAULT, SESSION_PLUGIN, WpPlugin)

/* DefaultSession */

struct _DefaultSession
{
  WpSession parent;

  WpProxy *device_node;
  struct pw_node_proxy *dsp_proxy;

  struct spa_audio_info_raw format;
  guint32 media_type;
  guint32 session_id;
};

G_DEFINE_TYPE (DefaultSession, session, WP_TYPE_SESSION)

static void
session_init (DefaultSession * self)
{
}

static void
session_finalize (GObject * obj)
{
  DefaultSession *self = DEFAULT_SESSION (obj);

  G_OBJECT_CLASS (session_parent_class)->finalize (obj);
}

static void
session_class_init (DefaultSessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  object_class->finalize = session_finalize;
}

static DefaultSession *
session_new (WpProxy * device_node, guint32 type, WpSessionDirection dir,
    const gchar * media_class)
{
  DefaultSession *sess = g_object_new (session_get_type (),
      "direction", dir,
      "media-class", media_class,
      NULL);

  sess->device_node = device_node;
  sess->media_type = type;

  return sess;
}

/* DefaultSessionPlugin */

struct _DefaultSessionPlugin
{
  WpPlugin parent;
};

G_DEFINE_TYPE (DefaultSessionPlugin, plugin, WP_TYPE_PLUGIN)

static void
device_node_destroyed (WpProxy * device_node, DefaultSession * session)
{
  g_autoptr (WpObject) core = NULL;
  WpSessionRegistry *sr = NULL;

  g_info ("Proxy %u destroyed - unregistering session %u",
      wp_proxy_get_id (device_node), session->session_id);

  core = wp_proxy_get_core (device_node);
  sr = wp_object_get_interface (core, WP_TYPE_SESSION_REGISTRY);
  g_return_if_fail (sr != NULL);

  wp_session_registry_unregister_session (sr, session->session_id);
}

static gboolean
handle_node (WpPlugin * self, WpProxy * proxy)
{
  g_autoptr (WpObject) core = NULL;
  g_autoptr (DefaultSession) session = NULL;
  g_autoptr (GError) error = NULL;
  WpSessionRegistry *sr = NULL;
  const gchar *media_class, *ptr;
  WpSessionDirection direction;
  guint32 media_type;
  guint32 sess_id;

  ptr = media_class = wp_pipewire_properties_get (
          WP_PIPEWIRE_PROPERTIES (proxy), "media.class");

  if (g_str_has_prefix (ptr, "Audio/")) {
    ptr += strlen ("Audio/");
    media_type = SPA_MEDIA_TYPE_audio;
  } else if (g_str_has_prefix (ptr, "Video/")) {
    ptr += strlen ("Video/");
    media_type = SPA_MEDIA_TYPE_video;
  } else {
    goto out;
  }

  if (strcmp (ptr, "Sink") == 0)
    direction = WP_SESSION_DIRECTION_OUTPUT;
  else if (strcmp (ptr, "Source") == 0)
    direction = WP_SESSION_DIRECTION_INPUT;
  else
    goto out;

  g_info ("Creating session for node %u (%s), media.class = '%s'",
      wp_proxy_get_id (proxy), wp_proxy_get_spa_type_string (proxy),
      media_class);

  session = session_new (proxy, media_type, direction, media_class);

  core = wp_plugin_get_core (self);
  sr = wp_object_get_interface (core, WP_TYPE_SESSION_REGISTRY);
  g_return_val_if_fail (sr != NULL, FALSE);

  if ((sess_id = wp_session_registry_register_session (sr,
          WP_SESSION (session), &error)) == -1) {
    g_warning ("Error registering session: %s", error->message);
    return FALSE;
  }

  session->session_id = sess_id;
  g_object_set_data_full (G_OBJECT (proxy), "module-default-session.session",
      g_object_ref (session), g_object_unref);
  g_signal_connect_object (proxy, "destroyed",
      (GCallback) device_node_destroyed, session, 0);

  return TRUE;

out:
  g_message ("Unrecognized media.class '%s' - not handling proxy %u (%s)",
      media_class, wp_proxy_get_id (proxy),
      wp_proxy_get_spa_type_string (proxy));
  return FALSE;
}

static gboolean
plug_dsp (WpProxy * node)
{
  DefaultSession *session;
  g_autoptr (WpObject) core = NULL;
  WpPipewireObjects *pw_objects = NULL;
  struct pw_core_proxy *core_proxy;
  WpPipewireProperties *pw_props = NULL;
  struct pw_properties *props;
  const char *name;
  enum pw_direction reverse_direction;
  uint8_t buf[1024];
  struct spa_pod_builder b = { 0, };
  struct spa_pod *param;

  session = g_object_get_data (G_OBJECT (node), "module-default-session.session");

  g_return_val_if_fail (session->media_type == SPA_MEDIA_TYPE_audio,
      G_SOURCE_REMOVE);

  g_info ("making audio dsp for session %u", session->session_id);

  core = wp_proxy_get_core (node);
  pw_objects = WP_PIPEWIRE_OBJECTS (core);
  core_proxy = pw_remote_get_core_proxy (wp_pipewire_objects_get_pw_remote (pw_objects));

  pw_props = WP_PIPEWIRE_PROPERTIES (node);
  props = pw_properties_new_dict (
      wp_pipewire_properties_get_as_spa_dict (pw_props));
  if ((name = pw_properties_get (props, "device.nick")) == NULL)
    name = "unnamed";
  pw_properties_set (props, "audio-dsp.name", name);
  pw_properties_setf (props, "audio-dsp.direction", "%d",
      wp_session_get_direction (WP_SESSION (session)));
  pw_properties_setf (props, "audio-dsp.maxbuffer", "%ld",
      MAX_QUANTUM_SIZE * sizeof (float));

  session->dsp_proxy = pw_core_proxy_create_object (core_proxy,
      "audio-dsp",
      PW_TYPE_INTERFACE_Node,
      PW_VERSION_NODE,
      &props->dict,
      0);
  pw_properties_free (props);

  reverse_direction =
      (wp_session_get_direction (WP_SESSION (session)) == WP_SESSION_DIRECTION_INPUT) ?
      PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT;

  spa_pod_builder_init (&b, buf, sizeof (buf));
  param = spa_format_audio_raw_build (&b, SPA_PARAM_Format, &session->format);
  param = spa_pod_builder_add_object (&b,
      SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
      SPA_PARAM_PROFILE_direction,  SPA_POD_Id (reverse_direction),
      SPA_PARAM_PROFILE_format,     SPA_POD_Pod (param));

  pw_node_proxy_set_param ((struct pw_node_proxy *) session->dsp_proxy,
      SPA_PARAM_Profile, 0, param);

  return G_SOURCE_REMOVE;
}

static void
audio_port_enum_params_done (GObject * port, GAsyncResult * res, gpointer data)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GPtrArray) params = NULL;
  WpProxy *node;
  DefaultSession *session;
  struct spa_audio_info_raw info = { 0, };
  guint32 media_type, media_subtype;
  guint i;

  g_debug ("done enumerating port %u params",
      wp_proxy_get_id (WP_PROXY (port)));

  params = wp_proxy_enum_params_finish (WP_PROXY (port), res, &error);
  if (!params) {
    g_warning ("%s", error->message);
    return;
  }

  node = WP_PROXY (data);
  session = g_object_get_data (G_OBJECT (node), "module-default-session.session");

  for (i = 0; i < params->len; i++) {
    struct spa_pod *param = g_ptr_array_index (params, i);

    if (spa_format_parse(param, &media_type, &media_subtype) < 0)
      return;

    if (media_type != SPA_MEDIA_TYPE_audio ||
        media_subtype != SPA_MEDIA_SUBTYPE_raw)
      return;

    spa_pod_fixate (param);

    if (spa_format_audio_raw_parse (param, &info) < 0)
      return;

    if (info.channels > session->format.channels)
      session->format = info;
  }

  g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, (GSourceFunc) plug_dsp,
      g_object_ref (node), g_object_unref);
}

static gboolean
handle_audio_port (WpPlugin * self, WpProxy * port, WpProxy * node)
{
  wp_proxy_enum_params (port, SPA_PARAM_EnumFormat,
      audio_port_enum_params_done, node);
  return TRUE;
}

static gboolean
handle_pw_proxy (WpPlugin * self, WpProxy * proxy)
{
  g_autoptr (WpObject) core = NULL;
  g_autoptr (WpProxy) parent = NULL;
  WpProxyRegistry *reg = NULL;
  DefaultSession *session;

  if (wp_proxy_get_spa_type (proxy) != PW_TYPE_INTERFACE_Port &&
      wp_proxy_get_spa_type (proxy) != PW_TYPE_INTERFACE_Node)
    return FALSE;

  core = wp_plugin_get_core (self);
  reg = wp_object_get_interface (core, WP_TYPE_PROXY_REGISTRY);
  parent = wp_proxy_registry_get_proxy (reg, wp_proxy_get_parent_id (proxy));

  if (wp_proxy_get_spa_type (parent) == PW_TYPE_INTERFACE_Device &&
      wp_proxy_get_spa_type (proxy) == PW_TYPE_INTERFACE_Node)
  {
    g_debug ("handling node %u (parent device %u)", wp_proxy_get_id (proxy),
        wp_proxy_get_id (parent));
    return handle_node (self, proxy);
  }
  else if (wp_proxy_get_spa_type (parent) == PW_TYPE_INTERFACE_Node &&
      wp_proxy_get_spa_type (proxy) == PW_TYPE_INTERFACE_Port &&
      (session = g_object_get_data (G_OBJECT (parent), "module-default-session.session")) &&
      session->media_type == SPA_MEDIA_TYPE_audio)
  {
    g_debug ("handling audio port %u (parent node %u)", wp_proxy_get_id (proxy),
        wp_proxy_get_id (parent));
    return handle_audio_port (self, proxy, parent);
  }

  return FALSE;
}

static void
plugin_init (DefaultSessionPlugin * self)
{
}

static void
plugin_class_init (DefaultSessionPluginClass * klass)
{
  WpPluginClass *plugin_class = WP_PLUGIN_CLASS (klass);
  plugin_class->handle_pw_proxy = handle_pw_proxy;
}

static const WpPluginMetadata plugin_metadata = {
  .rank = WP_PLUGIN_RANK_UPSTREAM,
  .name = "default-session",
  .description = "Provides the default WpSession implementation",
  .author = "George Kiagiadakis <george.kiagiadakis@collabora.com>",
  .license = "LGPL-2.1-or-later",
  .version = "0.1",
  .origin = "https://gitlab.freedesktop.org/gkiagia/wireplumber"
};

void
WP_MODULE_INIT_SYMBOL (WpPluginRegistry * registry)
{
  wp_plugin_registry_register_static (registry, plugin_get_type (),
      &plugin_metadata, sizeof (plugin_metadata));
}
