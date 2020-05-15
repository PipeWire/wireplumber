/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

struct _WpSessionSettings
{
  WpPlugin parent;
  WpObjectManager *sessions_om;
};

G_DECLARE_FINAL_TYPE (WpSessionSettings, wp_session_settings,
                      WP, SESSION_SETTINGS, WpPlugin)
G_DEFINE_TYPE (WpSessionSettings, wp_session_settings, WP_TYPE_PLUGIN)

static void
wp_session_settings_init (WpSessionSettings * self)
{
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
    g_autoptr (WpProperties) props = wp_proxy_get_properties (ep);
    const gchar *prio_str;
    gint prio;

    prio_str = wp_properties_get (props, "endpoint.priority");
    prio = atoi (prio_str);

    if (prio > highest_prio || id == 0) {
      highest_prio = prio;
      id = wp_proxy_get_bound_id (ep);
    }
  }
  return id;
}

static void
reevaluate_defaults (WpSession * session, WpDirection dir)
{
  guint32 id = 0;

  /* TODO
  if (settings exist)
    id = lookup endpoint in settings
  */

  if (id == 0)
    id = find_highest_prio (session, dir);

  wp_session_set_default_endpoint (session,
      (dir == WP_DIRECTION_INPUT) ? "Wp:defaultSink" : "Wp:defaultSource", id);
}

static void
on_endpoints_changed (WpSession * session, WpSessionSettings * self)
{
  reevaluate_defaults (session, WP_DIRECTION_INPUT);
  reevaluate_defaults (session, WP_DIRECTION_OUTPUT);
}

static void
on_session_added (WpObjectManager * om, WpSession * session,
    WpSessionSettings * self)
{
  on_endpoints_changed (session, self);
  g_signal_connect_object (session, "endpoints-changed",
      G_CALLBACK (on_endpoints_changed), self, 0);
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
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

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
