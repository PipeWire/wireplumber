/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "wp.h"
#include "private/connection.h"
#include "private/context.h"
#include "private/registry.h"
#include "private/internal-comp-loader.h"

#include <pipewire/pipewire.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-core")

#define MAIN_SOCKET_FD_PROPERTY "wireplumber.main-socket-fd"

/*! \defgroup wpcore WpCore */
/*!
 * \struct WpCore
 *
 * The core is the central object around which everything operates. It is
 * essential to create a WpCore before using any other WirePlumber API.
 *
 * The core object has the following responsibilities:
 *  * it owns the connection to the PipeWire server, and through it the
 *    WpMainContext that wraps the `pw_context` and glues the PipeWire library's
 *    event loop system with GMainLoop
 *  * it owns the WpRegistry, which keeps track of registered objects and
 *    of the objects that appear on the PipeWire registry
 *
 * The core is also responsible for loading components, which are defined in
 * the main configuration file. WP_CORE_FEATURE_COMPONENTS loads the components
 * of the profile in the "wireplumber.profile" property, if that property is
 * set.
 *
 * \b Configuration
 *
 * The main configuration file needs to be created and opened before the core
 * is created, using the WpConf API. It is then passed to the core as an
 * argument in the constructor.
 *
 * If a configuration file is not provided, the core lets the `pw_context` of
 * the main context load its own configuration, based on the rules that apply
 * to all pipewire clients (e.g. it respects the `PIPEWIRE_CONFIG_NAME`
 * environment variable and loads "client.conf" as a last resort).
 *
 * If a configuration file is provided, the `pw_context` does not load any
 * configuration, and the "context.properties", "context.spa-libs" and
 * "context.modules" sections of the provided WpConf are used instead.
 *
 * \gproperties
 *
 * \gproperty{conf, WpConf *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The main configuration file}
 *
 * \gproperty{g-main-context, GMainContext *,
 *   G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY,
 *   The GMainContext to use for events}
 *
 * \gproperty{properties, WpProperties *, G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY,
 *   Additional properties of the main context}
 */
struct _WpCore
{
  WpObject parent;

  /* Props */
  WpConf *conf;

  /* Props until they are handed over to the pw_context */
  GMainContext *g_main_context;
  WpProperties *properties;

  /* Context info */
  WpMainContext *main_context;

  /* Connection info */
  gint main_socket_fd;
  gboolean connection_lost;

  /* Registry info */
  WpRegistry *registry;
};

enum {
  PROP_0,
  PROP_CONF,
  PROP_G_MAIN_CONTEXT,
  PROP_PROPERTIES,
  N_PROPS,
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE (WpCore, wp_core, WP_TYPE_OBJECT)

static void
wp_core_clear_registry (WpCore *self)
{
  /* this unloads the components, which are registered objects */
  wp_registry_clear (self->registry);
  wp_object_update_features (WP_OBJECT (self), 0, WP_CORE_FEATURE_COMPONENTS);
}

static void
on_connection_connected (WpConnection * connection, WpCore * self)
{
  wp_object_update_features (WP_OBJECT (self), WP_CORE_FEATURE_CONNECTED, 0);
}

static void
on_connection_disconnected (WpConnection * connection, WpCore * self)
{
  self->connection_lost = TRUE;
  wp_object_abort_activation (WP_OBJECT (self),
      "The connection to PipeWire was lost");
}

static void
wp_core_init (WpCore * self)
{
  self->main_socket_fd = -1;
  self->registry = wp_registry_new (self);

  wp_registry_register_object (self->registry,
      g_object_new (WP_TYPE_INTERNAL_COMP_LOADER, NULL));
}

static void
wp_core_parse_main_socket_fd (WpCore * self)
{
  g_autoptr (WpProperties) props = NULL;
  const gchar *str;
  gint64 fd;

  props = wp_context_get_properties (WP_CONTEXT (self->main_context));
  str = wp_properties_get (props, MAIN_SOCKET_FD_PROPERTY);
  if (!str)
    return;

  if (!g_ascii_string_to_signed (str, 10, -1, G_MAXINT, &fd, NULL)) {
    wp_warning_object (self, "ignoring invalid " MAIN_SOCKET_FD_PROPERTY ": %s",
        str);
    return;
  }
  self->main_socket_fd = fd;
}

static void
wp_core_constructed (GObject * object)
{
  WpCore *self = WP_CORE (object);
  WpConnection *connection;
  WpSpaJson *spa_libs = NULL;
  WpSpaJson *modules = NULL;

  /* use our own configuration file, if specified */
  if (self->conf) {
    wp_info_object (self, "using configuration file: %s",
        wp_conf_get_name (self->conf));

    /* ensure we have our very own properties set,
       since we are going to modify it */
    self->properties = self->properties ?
        wp_properties_ensure_unique_owner (self->properties) :
        wp_properties_new_empty ();

    /* load context.properties */
    wp_conf_section_update_props (self->conf, "context.properties",
        self->properties);

    /* disable loading of a configuration file in pw_context */
    wp_properties_set (self->properties, PW_KEY_CONFIG_NAME, "null");
    wp_properties_set (self->properties, "context.modules.allow-empty", "true");

    spa_libs = wp_conf_get_section (self->conf, "context.spa-libs");
    modules = wp_conf_get_section (self->conf, "context.modules");
    if (!modules)
      wp_warning_object (self, "no modules loaded from context.modules");
  }

  /* Create the main context. We also hand over the properties and the
   * g_main_context properties to the main context */
  self->main_context = wp_main_context_new (self, self->g_main_context,
      g_steal_pointer (&self->properties), spa_libs, modules);
  g_clear_pointer (&self->g_main_context, g_main_context_unref);

  /* Parse the socket Id from the props */
  wp_core_parse_main_socket_fd (self);

  /* Handle connected and disconnected signals from the connection */
  connection = wp_main_context_get_connection (self->main_context);
  g_signal_connect_object (connection, "connected",
      G_CALLBACK (on_connection_connected), self, 0);
  g_signal_connect_object (connection, "disconnected",
      G_CALLBACK (on_connection_disconnected), self, 0);

  G_OBJECT_CLASS (wp_core_parent_class)->constructed (object);
}

static void
wp_core_finalize (GObject * obj)
{
  WpCore *self = WP_CORE (obj);
  WpConnection *conn;

  /* Registry; clear and detach it explicitly, in case something else holds
     a ref, as the connection is closed below */
  wp_registry_clear (self->registry);
  wp_registry_detach (self->registry);
  g_clear_object (&self->registry);

  /* Make sure we disconnect as the main context may outlive the core */
  conn = wp_main_context_get_connection (self->main_context);
  wp_connection_disconnect (conn);
  g_clear_object (&self->main_context);

  /* Props */
  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_object (&self->conf);
  g_clear_pointer (&self->g_main_context, g_main_context_unref);

  wp_debug_object (self, "WpCore destroyed");

  G_OBJECT_CLASS (wp_core_parent_class)->finalize (obj);
}

static void
wp_core_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpCore *self = WP_CORE (object);

  switch (property_id) {
  case PROP_CONF:
    g_value_set_object (value, self->conf);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_core_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpCore *self = WP_CORE (object);

  switch (property_id) {
  case PROP_CONF:
    self->conf = g_value_dup_object (value);
    break;
  case PROP_G_MAIN_CONTEXT:
    self->g_main_context = g_value_dup_boxed (value);
    break;
  case PROP_PROPERTIES:
    self->properties = g_value_dup_boxed (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static WpObjectFeatures
wp_core_get_supported_features (WpObject * self)
{
  return WP_CORE_FEATURE_CONNECTED | WP_CORE_FEATURE_COMPONENTS;
}

enum {
  STEP_CONNECT = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_LOAD_COMPONENTS,
};

static guint
wp_core_activate_get_next_step (WpObject * self,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      /* WP_CORE_FEATURE_COMPONENTS depends on WP_CORE_FEATURE_CONNECTED */
      if (!wp_object_test_active_features (self, WP_CORE_FEATURE_CONNECTED))
        return STEP_CONNECT;
      G_GNUC_FALLTHROUGH;

    case STEP_CONNECT:
      if (missing & WP_CORE_FEATURE_COMPONENTS)
        return STEP_LOAD_COMPONENTS;
      G_GNUC_FALLTHROUGH;

    case STEP_LOAD_COMPONENTS:
      return WP_TRANSITION_STEP_NONE;

    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static void
on_components_loaded (WpCore * self, GAsyncResult *res, WpTransition * t)
{
  g_autoptr (WpTransition) transition = t;
  g_autoptr (GError) error = NULL;
  gboolean loaded = wp_core_load_component_finish (self, res, &error);

  /* the transition fails if the connection is lost while loading */
  if (wp_transition_get_completed (transition))
    return;

  if (!loaded) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "failed to load components: %s", error->message));
    return;
  }

  if (self->conf) {
    wp_info_object (self, "done loading components, closing conf file...");
    wp_conf_close (self->conf);
  }

  wp_object_update_features (WP_OBJECT (self), WP_CORE_FEATURE_COMPONENTS, 0);
}

static void
wp_core_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpCore *self = WP_CORE (object);
  WpConnection *connection = wp_main_context_get_connection (self->main_context);

  switch (step) {
    case STEP_CONNECT:
      /* we continue in on_connection_connected() */
      if (!wp_connection_connect (connection, self->main_socket_fd)) {
        wp_transition_return_error (WP_TRANSITION (transition), g_error_new (
            WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_SERVICE_UNAVAILABLE,
            "Failed to connect to PipeWire"));
        break;
      }
      /* bind the registry together with the connection request */
      wp_registry_attach (self->registry,
          wp_connection_get_pw_core (connection));
      break;

    case STEP_LOAD_COMPONENTS: {
      g_autoptr (WpProperties) props = wp_connection_get_properties (connection);
      const gchar *profile = wp_properties_get (props, "wireplumber.profile");

      if (self->connection_lost || !wp_object_test_active_features (object,
              WP_CORE_FEATURE_CONNECTED)) {
        wp_transition_return_error (WP_TRANSITION (transition), g_error_new (
            WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_SERVICE_UNAVAILABLE,
            "Not connected to PipeWire"));
        break;
      }

      /* without a profile, there is nothing to load */
      if (!profile) {
        wp_object_update_features (object, WP_CORE_FEATURE_COMPONENTS, 0);
        break;
      }

      wp_info_object (self,
          "parsing & loading components for profile [%s]...", profile);

      /* Load components that are defined in the configuration section */
      wp_core_load_component (self, profile, "profile", NULL, NULL, NULL,
          (GAsyncReadyCallback) on_components_loaded,
          g_object_ref (transition));
      break;
    }

    case WP_TRANSITION_STEP_ERROR:
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
wp_core_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpCore *self = WP_CORE (object);
  WpConnection *connection = wp_main_context_get_connection (self->main_context);

  if (features & WP_CORE_FEATURE_CONNECTED) {
    wp_core_clear_registry (self);
    wp_registry_detach (self->registry);
    self->connection_lost = FALSE;
    wp_connection_disconnect (connection);
    wp_object_update_features (object, 0, WP_CORE_FEATURE_CONNECTED);
  } else if (features & WP_CORE_FEATURE_COMPONENTS) {
    wp_core_clear_registry (self);
  }
}

static void
wp_core_class_init (WpCoreClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;

  object_class->constructed = wp_core_constructed;
  object_class->finalize = wp_core_finalize;
  object_class->get_property = wp_core_get_property;
  object_class->set_property = wp_core_set_property;

  wpobject_class->get_supported_features = wp_core_get_supported_features;
  wpobject_class->activate_get_next_step = wp_core_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_core_activate_execute_step;
  wpobject_class->deactivate = wp_core_deactivate;

  properties[PROP_CONF] = g_param_spec_object ("conf", "conf",
      "The main configuration file", WP_TYPE_CONF,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_G_MAIN_CONTEXT] = g_param_spec_boxed ("g-main-context",
      "g-main-context", "The GMainContext to use for events",
      G_TYPE_MAIN_CONTEXT,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_PROPERTIES] = g_param_spec_boxed ("properties", "properties",
      "Additional properties of the main context", WP_TYPE_PROPERTIES,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

/*!
 * \brief Creates a new core object
 *
 * \ingroup wpcore
 * \param context (transfer none) (nullable): the GMainContext to use for events
 * \param conf (transfer full) (nullable): the main configuration file
 * \param properties (transfer full) (nullable): additional properties, which
 *   are also passed to pw_context_new() and pw_context_connect()
 * \returns (transfer full): a new WpCore
 */
WpCore *
wp_core_new (GMainContext * context, WpConf * conf, WpProperties * properties)
{
  g_autoptr (WpConf) c = conf;
  g_autoptr (WpProperties) props = properties;

  return g_object_new (WP_TYPE_CORE,
      "g-main-context", context,
      "conf", conf,
      "properties", properties,
      NULL);
}

/*!
 * \brief Gets the main configuration file that the core was constructed with
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer full) (nullable): the main configuration file of \a self
 */
WpConf *
wp_core_get_conf (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->conf ? g_object_ref (self->conf) : NULL;
}

/*!
 * \brief Gets the main context of the core
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer none): the main context of \a self
 */
WpMainContext *
wp_core_get_main_context (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->main_context;
}

/*!
 * \brief Gets the connection of the main context of the core
 *
 * This is the same as calling wp_main_context_get_connection() on the context
 * returned by wp_core_get_main_context().
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer none): the connection of the main context of \a self
 */
WpConnection *
wp_core_get_connection (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return wp_main_context_get_connection (self->main_context);
}

/*!
 * \brief Gets the registry of the core
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer none): the registry of \a self
 */
WpRegistry *
wp_core_get_registry (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->registry;
}
