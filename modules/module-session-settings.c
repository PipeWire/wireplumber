/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * This module selects "default" source & sink endpoints for each session
 * and when the user modifies them, it stores the user preference in text
 * files in $XDG_CONFIG_DIR, similarly to pulseaudio
 */

#include <wp/wp.h>
#include <errno.h>

G_DEFINE_QUARK (wp-session-settings-sink-file, session_settings_sink_file)
G_DEFINE_QUARK (wp-session-settings-source-file, session_settings_source_file)

struct _WpSessionSettings
{
  WpPlugin parent;
  WpObjectManager *sessions_om;
  gchar *config_dir;
};

G_DECLARE_FINAL_TYPE (WpSessionSettings, wp_session_settings,
                      WP, SESSION_SETTINGS, WpPlugin)
G_DEFINE_TYPE (WpSessionSettings, wp_session_settings, WP_TYPE_PLUGIN)

static void
wp_session_settings_init (WpSessionSettings * self)
{
  self->config_dir = g_build_filename (g_get_user_config_dir (),
      "wireplumber", NULL);
  if (g_mkdir_with_parents (self->config_dir, 0755) < 0) {
    wp_warning_object (self, "failed to create '%s': %s", self->config_dir,
        strerror (errno));
  }
}

static void
wp_session_settings_finalize (GObject * object)
{
  WpSessionSettings * self = WP_SESSION_SETTINGS (object);

  g_free (self->config_dir);

  G_OBJECT_CLASS (wp_session_settings_parent_class)->finalize (object);
}

static void
on_default_endpoint_changed (WpSession * session, const gchar * type,
    guint32 id, WpSessionSettings * self)
{
  GFile *file = NULL;
  g_autoptr (GFileOutputStream) out_stream = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpEndpoint) ep = NULL;

  wp_debug_object (self, "%s on " WP_OBJECT_FORMAT " changed (%u), storing",
      type, WP_OBJECT_ARGS (session), id);

  if (g_strcmp0 (type, "Wp:defaultSink") == 0)
    file = g_object_get_qdata (G_OBJECT (session),
        session_settings_sink_file_quark ());
  else if (g_strcmp0 (type, "Wp:defaultSource") == 0)
    file = g_object_get_qdata (G_OBJECT (session),
        session_settings_source_file_quark ());
  g_return_if_fail (file);

  ep = wp_session_lookup_endpoint (session,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", id, NULL);
  if (!ep) {
    wp_warning_object (self, "%s (%u) on " WP_OBJECT_FORMAT " not found",
        type, id, WP_OBJECT_ARGS (session));
    return;
  }

  out_stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL,
      &error);
  if (out_stream) {
    /* write the name plus its terminating null byte */
    const gchar *name = wp_endpoint_get_name (ep);
    if (!g_output_stream_write_all (G_OUTPUT_STREAM (out_stream),
            name, strlen (name) + 1, NULL, NULL, &error)) {
      wp_warning_object (self, "error writing %s: %s", g_file_peek_path (file),
          error->message);
    }
  }
}

static guint32
find_highest_prio (WpSession * session, WpDirection dir)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  gint highest_prio = 0;
  guint32 id = 0;

  it = wp_session_iterate_endpoints_filtered (session,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s",
      (dir == WP_DIRECTION_INPUT) ? "*/Sink" : "*/Source",
      NULL);

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpProxy *ep = g_value_get_object (&val);
    const gchar *prio_str = wp_proxy_get_property (ep, "endpoint.priority");
    gint prio = atoi (prio_str);

    if (prio > highest_prio || id == 0) {
      highest_prio = prio;
      id = wp_proxy_get_bound_id (ep);
    }
  }
  return id;
}

static void
reevaluate_defaults (WpSessionSettings * self,
    WpSession * session, WpDirection dir)
{
  guint32 id = 0;
  GFile *file;
  g_autoptr (GFileInputStream) in_stream = NULL;
  g_autoptr (GError) error = NULL;
  gchar buffer[128];

  /* try to read the default endpoint's name from the settings file */

  file = g_object_get_qdata (G_OBJECT (session),
      (dir == WP_DIRECTION_INPUT) ?
      session_settings_sink_file_quark () :
      session_settings_source_file_quark ());

  in_stream = g_file_read (file, NULL, &error);
  if (in_stream) {
    gsize bytes_read = 0;
    g_autoptr (WpEndpoint) ep = NULL;

    if (!g_input_stream_read_all (G_INPUT_STREAM (in_stream), buffer,
            sizeof (buffer), &bytes_read, NULL, &error)) {
      wp_warning_object (self, "error reading %s: %s", g_file_peek_path (file),
          error->message);
    } else {
      /* the file should have a null byte, but let's not trust it */
      buffer[MIN (bytes_read, sizeof (buffer) - 1)] = '\0';
      ep = wp_session_lookup_endpoint (session,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, "endpoint.name", "=s", buffer,
          WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s",
              (dir == WP_DIRECTION_INPUT) ? "*/Sink" : "*/Source",
          NULL);
      if (ep)
        id = wp_proxy_get_bound_id (WP_PROXY (ep));
    }
  }
  else if (error) {
    /* this is an expected condition if no settings are stored; just debug */
    wp_debug_object (self, "file read error (%s): %s", g_file_peek_path (file),
        error->message);
  }

  /* if not found by settings, find the highest priority one */
  if (id == 0)
    id = find_highest_prio (session, dir);

  wp_debug_object (self, "selecting default %s for " WP_OBJECT_FORMAT ": %u",
      (dir == WP_DIRECTION_INPUT) ? "sink" : "source",
      WP_OBJECT_ARGS (session), id);

  /* block the signal to avoid storing this on the file;
     only selections done by the user should be stored */
  g_signal_handlers_block_by_func (session, on_default_endpoint_changed, self);
  wp_session_set_default_endpoint (session,
      (dir == WP_DIRECTION_INPUT) ? "Wp:defaultSink" : "Wp:defaultSource", id);
  g_signal_handlers_unblock_by_func (session, on_default_endpoint_changed, self);
}

static void
on_endpoints_changed (WpSession * session, WpSessionSettings * self)
{
  wp_trace_object (session, "endpoints changed, re-evaluating defaults");
  reevaluate_defaults (self, session, WP_DIRECTION_INPUT);
  reevaluate_defaults (self, session, WP_DIRECTION_OUTPUT);
}

static void
on_session_added (WpObjectManager * om, WpSession * session,
    WpSessionSettings * self)
{
  GFile *sink_file;
  GFile *source_file;
  gchar *filename;

  filename = g_strdup_printf ("%s%c%s-default-sink", self->config_dir,
      G_DIR_SEPARATOR, wp_session_get_name (session));
  sink_file = g_file_new_for_path (filename);
  g_free (filename);

  filename = g_strdup_printf ("%s%c%s-default-source", self->config_dir,
      G_DIR_SEPARATOR, wp_session_get_name (session));
  source_file = g_file_new_for_path (filename);
  g_free (filename);

  g_object_set_qdata_full (G_OBJECT (session),
      session_settings_sink_file_quark (), sink_file, g_object_unref);
  g_object_set_qdata_full (G_OBJECT (session),
      session_settings_source_file_quark (), source_file, g_object_unref);

  g_signal_connect_object (session, "default-endpoint-changed",
      G_CALLBACK (on_default_endpoint_changed), self, 0);
  g_signal_connect_object (session, "endpoints-changed",
      G_CALLBACK (on_endpoints_changed), self, 0);

  on_endpoints_changed (session, self);
}

static void
wp_session_settings_activate (WpPlugin * plugin)
{
  WpSessionSettings * self = WP_SESSION_SETTINGS (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);

  g_return_if_fail (core);

  self->sessions_om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->sessions_om, WP_TYPE_SESSION, NULL);
  wp_object_manager_request_proxy_features (self->sessions_om, WP_TYPE_SESSION,
      WP_PROXY_FEATURES_STANDARD |
      WP_PROXY_FEATURE_CONTROLS |
      WP_SESSION_FEATURE_ENDPOINTS);
  g_signal_connect_object (self->sessions_om, "object-added",
      G_CALLBACK (on_session_added), self, 0);
  wp_core_install_object_manager (core, self->sessions_om);
}

static void
wp_session_settings_deactivate (WpPlugin * plugin)
{
  WpSessionSettings * self = WP_SESSION_SETTINGS (plugin);

  g_clear_object (&self->sessions_om);
}

static void
wp_session_settings_class_init (WpSessionSettingsClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_session_settings_finalize;

  plugin_class->activate = wp_session_settings_activate;
  plugin_class->deactivate = wp_session_settings_deactivate;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  wp_plugin_register (g_object_new (wp_session_settings_get_type (),
          "module", module,
          NULL));
}
