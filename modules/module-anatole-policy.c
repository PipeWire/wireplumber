/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <anatole.h>
#include <pipewire/extensions/session-manager/keys.h>

G_DEFINE_QUARK (wp-anatole-policy-self-pointer, self_pointer);

typedef enum {
  ACTION_TYPE_CREATE_LINK = 0,
  ACTION_TYPE_DESTROY_LINK,
  ACTION_TYPE_LINK_REQUEST_STATE,
} ActionType;

static const gchar *direction_str[] = { "in", "out" };

struct _WpAnatolePolicy
{
  WpPlugin parent;
  WpObjectManager *om;
  AnatoleEngine *engine;
  GVariantBuilder *actions_builder;
};

G_DECLARE_FINAL_TYPE (WpAnatolePolicy, wp_anatole_policy,
                      WP, ANATOLE_POLICY, WpPlugin)
G_DEFINE_TYPE (WpAnatolePolicy, wp_anatole_policy, WP_TYPE_PLUGIN)

static void
wp_anatole_policy_init (WpAnatolePolicy * self)
{
}

static GVariant *
lua_action_callback (AnatoleEngine * engine, GVariant * args, gpointer data)
{
  WpAnatolePolicy * self =
      g_object_get_qdata (G_OBJECT (engine), self_pointer_quark());
  ActionType type = GPOINTER_TO_UINT (data);

  g_return_val_if_fail (self->actions_builder, NULL);
  g_variant_builder_add (self->actions_builder, "(uv)", type, args);
  return NULL;
}

static GVariant *
lua_debug (AnatoleEngine * engine, GVariant * args, gpointer data)
{
  const gchar * s = NULL;
  g_variant_get (args, "(&s)", &s);
  wp_debug_object (data, "%s", s);
  return NULL;
}

static GVariant *
lua_trace (AnatoleEngine * engine, GVariant * args, gpointer data)
{
  const gchar * s = NULL;
  g_variant_get (args, "(&s)", &s);
  wp_trace_object (data, "%s", s);
  return NULL;
}

static void
wp_anatole_policy_load_lua_functions (WpAnatolePolicy * self)
{
  g_autoptr (GError) error = NULL;

  g_object_set_qdata (G_OBJECT (self->engine), self_pointer_quark (), self);

  anatole_engine_add_function (self->engine,
      "create_link", lua_action_callback, "(xxxx)",
      GUINT_TO_POINTER (ACTION_TYPE_CREATE_LINK), NULL);
  anatole_engine_add_function (self->engine,
      "destroy_link", lua_action_callback, "(x)",
      GUINT_TO_POINTER (ACTION_TYPE_DESTROY_LINK), NULL);
  anatole_engine_add_function (self->engine,
      "link_request_state", lua_action_callback, "(xs)",
      GUINT_TO_POINTER (ACTION_TYPE_LINK_REQUEST_STATE), NULL);
  anatole_engine_add_function (self->engine,
      "debug", lua_debug, "(s)", self, NULL);
  anatole_engine_add_function (self->engine,
      "trace", lua_trace, "(s)", self, NULL);

  if (!anatole_engine_add_function_finish (self->engine, &error)) {
    wp_critical_object (self, "failed to load lua functions: %s",
        error->message);
  }
}

static GVariant *
serialize_properties (WpProperties * p)
{
  g_autoptr (WpProperties) props = p;
  GVariantBuilder b;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a{ss}"));

  it = wp_properties_iterate (props);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    g_variant_builder_add (&b, "{ss}",
        wp_properties_iterator_item_get_key (&val),
        wp_properties_iterator_item_get_value (&val));
  }
  return g_variant_builder_end (&b);
}

static GVariant *
serialize_endpoint (WpEndpoint * ep)
{
  GVariantBuilder b;
  GVariantBuilder sb;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_init (&sb, G_VARIANT_TYPE_VARDICT);

  it = wp_endpoint_iterate_streams (ep);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpEndpointStream *s = g_value_get_object (&val);

    g_variant_builder_add (&sb, "{sv}", "id",
        g_variant_new_uint32 (wp_proxy_get_bound_id (WP_PROXY (s))));
    g_variant_builder_add (&sb, "{sv}", "name",
        g_variant_new_string (wp_endpoint_stream_get_name (s)));
    g_variant_builder_add (&sb, "{sv}", "properties",
        serialize_properties (wp_proxy_get_properties (WP_PROXY (s))));
  }

  g_variant_builder_add (&b, "{sv}", "id",
      g_variant_new_uint32 (wp_proxy_get_bound_id (WP_PROXY (ep))));
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_string (wp_endpoint_get_name (ep)));
  g_variant_builder_add (&b, "{sv}", "media_class",
      g_variant_new_string (wp_endpoint_get_media_class (ep)));
  g_variant_builder_add (&b, "{sv}", "direction",
      g_variant_new_string (direction_str[wp_endpoint_get_direction (ep)]));
  g_variant_builder_add (&b, "{sv}", "n_streams",
      g_variant_new_uint32 (wp_endpoint_get_n_streams (ep)));
  g_variant_builder_add (&b, "{sv}", "streams", g_variant_builder_end (&sb));
  g_variant_builder_add (&b, "{sv}", "properties",
      serialize_properties (wp_proxy_get_properties (WP_PROXY (ep))));

  return g_variant_builder_end (&b);
}

static GVariant *
serialize_link (WpEndpointLink * link)
{
  GVariantBuilder b;
  guint32 output_endpoint, output_stream, input_endpoint, input_stream;

  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);

  g_variant_builder_add (&b, "{sv}", "id",
      g_variant_new_uint32 (wp_proxy_get_bound_id (WP_PROXY (link))));

  wp_endpoint_link_get_linked_object_ids (link,
      &output_endpoint, &output_stream, &input_endpoint, &input_stream);
  g_variant_builder_add (&b, "{sv}", "output_endpoint",
      g_variant_new_uint32 (output_endpoint));
  g_variant_builder_add (&b, "{sv}", "output_stream",
      g_variant_new_uint32 (output_stream));
  g_variant_builder_add (&b, "{sv}", "input_endpoint",
      g_variant_new_uint32 (input_endpoint));
  g_variant_builder_add (&b, "{sv}", "input_stream",
      g_variant_new_uint32 (input_stream));

  {
    const gchar *error = NULL;
    g_autoptr (GEnumClass) klass =
        g_type_class_ref (WP_TYPE_ENDPOINT_LINK_STATE);
    GEnumValue *value =
        g_enum_get_value (klass, wp_endpoint_link_get_state (link, &error));

    g_variant_builder_add (&b, "{sv}", "state",
        g_variant_new_string (value->value_nick));
    if (error)
      g_variant_builder_add (&b, "{sv}", "state-error",
          g_variant_new_string (error));
  }

  g_variant_builder_add (&b, "{sv}", "properties",
      serialize_properties (wp_proxy_get_properties (WP_PROXY (link))));

  return g_variant_builder_end (&b);
}

static GVariant *
serialize_session (WpSession * session)
{
  GVariantBuilder b;
  GVariantBuilder defb;

  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_init (&defb, G_VARIANT_TYPE_VARDICT);

  g_variant_builder_add (&defb, "{sv}", "in", g_variant_new_uint32 (
          wp_session_get_default_endpoint (session, WP_DIRECTION_INPUT)));
  g_variant_builder_add (&defb, "{sv}", "out", g_variant_new_uint32 (
          wp_session_get_default_endpoint (session, WP_DIRECTION_OUTPUT)));

  g_variant_builder_add (&b, "{sv}", "id",
      g_variant_new_uint32 (wp_proxy_get_bound_id (WP_PROXY (session))));
  g_variant_builder_add (&b, "{sv}", "name",
      g_variant_new_string (wp_session_get_name (session)));
  g_variant_builder_add (&b, "{sv}", "default_target",
      g_variant_builder_end (&defb));
  g_variant_builder_add (&b, "{sv}", "properties",
      serialize_properties (wp_proxy_get_properties (WP_PROXY (session))));

  return g_variant_builder_end (&b);
}

static GVariant *
serialize_endpoints (WpSession * session)
{
  GVariantBuilder b;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a{uv}"));
  it = wp_session_iterate_endpoints (session);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpEndpoint *ep = g_value_get_object (&val);
    g_variant_builder_add (&b, "{uv}",
        wp_proxy_get_bound_id (WP_PROXY (ep)), serialize_endpoint (ep));
  }
  return g_variant_builder_end (&b);
}

static GVariant *
serialize_links (WpSession * session)
{
  GVariantBuilder b;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a{uv}"));
  it = wp_session_iterate_links (session);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpEndpointLink *l = g_value_get_object (&val);
    g_variant_builder_add (&b, "{uv}",
        wp_proxy_get_bound_id (WP_PROXY (l)), serialize_link (l));
  }
  return g_variant_builder_end (&b);
}

static void
process_actions (WpAnatolePolicy * self, WpSession * session, GVariant * actions)
{
  GVariantIter iter;
  guint type;
  GVariant *args;

  g_variant_iter_init (&iter, actions);
  while (g_variant_iter_loop (&iter, "(uv)", &type, &args)) {
    switch (type) {
    case ACTION_TYPE_CREATE_LINK: {
      /* gint64 because they come from lua and anatole converts them to gint64 */
      gint64 ep_id = -1, stream_id = -1, trgt_ep_id = -1, trgt_stream_id = -1;

      g_variant_get (args, "(xxxx)",
          &ep_id, &stream_id, &trgt_ep_id, &trgt_stream_id);

      g_autoptr (WpEndpoint) endpoint = wp_session_lookup_endpoint (session,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", (guint) ep_id,
          NULL);
      if (!endpoint) {
        wp_message_object (self, "invalid endpoint: %u", (guint) ep_id);
        continue;
      }

      /* Create the link properties */
      g_autoptr (WpProperties) props = wp_properties_new_empty ();
      switch (wp_endpoint_get_direction (endpoint)) {
      case WP_DIRECTION_INPUT:
        /* Capture */
        wp_properties_setf (props,
            PW_KEY_ENDPOINT_LINK_OUTPUT_ENDPOINT, "%i", (gint) trgt_ep_id);
        wp_properties_setf (props,
            PW_KEY_ENDPOINT_LINK_OUTPUT_STREAM, "%i", (gint) trgt_stream_id);
        wp_properties_setf (props,
            PW_KEY_ENDPOINT_LINK_INPUT_ENDPOINT, "%i", (gint) ep_id);
        wp_properties_setf (props,
            PW_KEY_ENDPOINT_LINK_INPUT_STREAM, "%i", (gint) stream_id);
        break;
      case WP_DIRECTION_OUTPUT:
        /* Playback */
        wp_properties_setf (props,
            PW_KEY_ENDPOINT_LINK_OUTPUT_ENDPOINT, "%i", (gint) ep_id);
        wp_properties_setf (props,
            PW_KEY_ENDPOINT_LINK_OUTPUT_STREAM, "%i", (gint) stream_id);
        wp_properties_setf (props,
            PW_KEY_ENDPOINT_LINK_INPUT_ENDPOINT, "%i", (gint) trgt_ep_id);
        wp_properties_setf (props,
            PW_KEY_ENDPOINT_LINK_INPUT_STREAM, "%i", (gint) trgt_stream_id);
        break;
      default:
        g_warn_if_reached ();
        continue;
      }

      /* Create the link */
      wp_endpoint_create_link (endpoint, props);
      break;
    }
    case ACTION_TYPE_DESTROY_LINK: {
      gint64 link_id = -1;

      g_variant_get (args, "(x)", &link_id);

      g_autoptr (WpEndpointLink) link = wp_session_lookup_link (session,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", (guint) link_id,
          NULL);
      if (!link) {
        wp_message_object (self, "invalid endpoint-link: %u", (guint) link_id);
        continue;
      }

      wp_proxy_request_destroy (WP_PROXY (link));
      break;
    }
    case ACTION_TYPE_LINK_REQUEST_STATE: {
      gint64 link_id = -1;
      const gchar *state = NULL;

      g_variant_get (args, "(x&s)", &link_id, &state);

      g_autoptr (WpEndpointLink) link = wp_session_lookup_link (session,
          WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", (guint) link_id,
          NULL);
      if (!link) {
        wp_message_object (self, "invalid endpoint-link: %u", (guint) link_id);
        continue;
      }

      g_autoptr (GEnumClass) klass =
          g_type_class_ref (WP_TYPE_ENDPOINT_LINK_STATE);
      GEnumValue *value = g_enum_get_value_by_nick (klass, state);
      if (!value) {
        wp_message_object (self, "invalid endpoint-link state: %s", state);
        continue;
      }

      wp_endpoint_link_request_state (link, value->value);
      break;
    }
    default:
      g_warn_if_reached ();
      break;
    }
  }
}

static void
rescan_session (WpSession * session, WpAnatolePolicy * self)
{
  gboolean ret;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) actions = NULL;
  GVariantBuilder actions_builder;

  wp_debug_object (self, "calling lua rescan_session()");

  g_variant_builder_init (&actions_builder, G_VARIANT_TYPE ("a(uv)"));
  self->actions_builder = &actions_builder;

  ret = anatole_engine_call_function (self->engine, "rescan_session",
      g_variant_new ("(@a{sv}@a{uv}@a{uv})",
          serialize_session (session),
          serialize_endpoints (session),
          serialize_links (session)),
      &error);

  self->actions_builder = NULL;

  if (!ret) {
    wp_warning_object (self, "failed to call 'rescan_session' in the "
        "Lua policy script: %s", error->message);
    g_variant_builder_clear (&actions_builder);
    return;
  }

  actions = g_variant_builder_end (&actions_builder);
  process_actions (self, session, actions);
}

static void
on_session_added (WpObjectManager * om, WpProxy * proxy, WpAnatolePolicy * self)
{
  g_signal_connect_object (proxy, "endpoints-changed",
      G_CALLBACK (rescan_session), self, 0);
  g_signal_connect_object (proxy, "links-changed",
      G_CALLBACK (rescan_session), self, 0);
}

static void
wp_anatole_policy_activate (WpPlugin * plugin)
{
  WpAnatolePolicy * self = WP_ANATOLE_POLICY (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
  g_return_if_fail (core);
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  g_return_if_fail (config);
  g_autofree gchar * script_path = NULL;
  g_autoptr (GError) error = NULL;

  /* find the policy script */
  script_path = wp_configuration_find_file (config, "policy.lua");
  if (!script_path) {
    wp_warning_object (self, "policy.lua script was not found");
    return;
  }

  /* initialize the lua engine */
  self->engine = anatole_engine_new ("wp");
  wp_anatole_policy_load_lua_functions (self);
  if (!anatole_engine_load_script_from_path (self->engine, script_path, &error)) {
    wp_warning_object (self, "script load error: %s", error->message);
    g_clear_object (&self->engine);
    return;
  }

  /* Install the sessions object manager */
  self->om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->om, WP_TYPE_SESSION, NULL);
  wp_object_manager_request_proxy_features (self->om, WP_TYPE_SESSION,
      WP_SESSION_FEATURES_STANDARD);
  g_signal_connect_object (self->om, "object-added",
      G_CALLBACK (on_session_added), self, 0);
  wp_core_install_object_manager (core, self->om);
}

static void
wp_anatole_policy_deactivate (WpPlugin * plugin)
{
  WpAnatolePolicy * self = WP_ANATOLE_POLICY (plugin);

  g_clear_object (&self->om);
  g_clear_object (&self->engine);
}

static void
wp_anatole_policy_class_init (WpAnatolePolicyClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->activate = wp_anatole_policy_activate;
  plugin_class->deactivate = wp_anatole_policy_deactivate;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  wp_plugin_register (g_object_new (wp_anatole_policy_get_type (),
          "module", module,
          NULL));
}
