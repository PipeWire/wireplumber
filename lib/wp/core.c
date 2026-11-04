/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "core.h"
#include "wp.h"
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
 *  * it initializes the PipeWire library
 *  * it creates the WpMainContext, which wraps the `pw_context` and glues the
 *    PipeWire library's event loop system with GMainLoop
 *  * it allows connecting to the PipeWire server, creating a local `pw_core`
 *  * it maintains a list of registered objects, which other classes use
 *    to keep objects loaded permanently into memory
 *  * it watches the PipeWire registry and keeps track of remote and local
 *    objects that appear in the registry, making them accessible through
 *    the WpObjectManager API.
 *
 * The core is also responsible for loading components, which are defined in
 * the main configuration file. Components are loaded when
 * WP_CORE_FEATURE_COMPONENTS is activated.
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
 * \gproperty{g-main-context, GMainContext *,
 *   G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY,
 *   The GMainContext to use for events}
 *
 * \gproperty{properties, WpProperties *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The pipewire properties of the pw_core}
 *
 * \gproperty{conf, WpConf *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The main configuration file}
 *
 * \gsignals
 *
 * \par connected
 * \parblock
 * \code
 * void
 * connected_callback (WpCore * self,
 *                     gpointer user_data)
 * \endcode
 * Emitted when the core is successfully connected to the PipeWire server
 * \endparblock
 *
 * \par disconnected
 * \parblock
 * \code
 * void
 * disconnected_callback (WpCore * self,
 *                        gpointer user_data)
 * \endcode
 * Emitted when the core is disconnected from the PipeWire server
 * \endparblock
 */
struct _WpCore
{
  WpObject parent;

  /* main loop integration, until it is handed over to the main context */
  GMainContext *g_main_context;

  /* the socket to connect to, or -1 for the default one */
  gint main_socket_fd;

  /* extra properties */
  WpProperties *properties;

  /* pipewire main objects */
  WpMainContext *main_context;
  struct pw_core *pw_core;
  struct pw_core_info *info;

  /* pipewire main listeners */
  struct spa_hook core_listener;
  struct spa_hook proxy_core_listener;

  /* the main configuration file */
  WpConf *conf;

  WpRegistry registry;
  GHashTable *async_tasks; // <int seq, GTask*>
};

enum {
  PROP_0,
  PROP_G_MAIN_CONTEXT,
  PROP_PROPERTIES,
  PROP_CONF,
  N_PROPS,
};

enum {
  SIGNAL_CONNECTED,
  SIGNAL_DISCONNECTED,
  NUM_SIGNALS
};

static guint32 signals[NUM_SIGNALS];
static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE (WpCore, wp_core, WP_TYPE_OBJECT)

static void
core_info (void *data, const struct pw_core_info * info)
{
  WpCore *self = WP_CORE (data);
  gboolean new_connection = (self->info == NULL);

  self->info = pw_core_info_update (self->info, info);

  wp_info_object (self, "connected to server: %s, cookie: %u",
      self->info->name, self->info->cookie);

  if (new_connection) {
    g_signal_emit (self, signals[SIGNAL_CONNECTED], 0);
    wp_object_update_features (WP_OBJECT (self), WP_CORE_FEATURE_CONNECTED, 0);
  }
}

static void
core_done (void *data, uint32_t id, int seq)
{
  WpCore *self = WP_CORE (data);
  g_autoptr (GTask) task = NULL;

  g_hash_table_steal_extended (self->async_tasks, GINT_TO_POINTER (seq), NULL,
      (gpointer *) &task);
  wp_debug_object (self, "done, seq 0x%x, task " WP_OBJECT_FORMAT,
      seq, WP_OBJECT_ARGS (task));

  if (task)
    g_task_return_boolean (task, TRUE);
}

static void
wp_core_clear_registry (WpCore *self)
{
  /* already cleared for good in dispose() */
  if (!self->registry.objects)
    return;

  /* this unloads the components, which are registered objects */
  wp_registry_clear (&self->registry);
  wp_registry_init (&self->registry);
  wp_object_update_features (WP_OBJECT (self), 0, WP_CORE_FEATURE_COMPONENTS);
}

static void
wp_core_disconnect_internal (WpCore *self)
{
  wp_core_clear_registry (self);

  /* pw_core_disconnect destroys the core proxy
    and we continue in proxy_core_destroy() */
  if (self->pw_core)
    pw_core_disconnect (self->pw_core);
}

static inline gboolean
wp_core_is_connected (WpCore * self)
{
  return WP_IS_CORE (self) && self->pw_core && self->info;
}

static gboolean
core_disconnect_async (WpCore * self)
{
  wp_core_disconnect_internal (self);
  return G_SOURCE_REMOVE;
}

static void
core_error (void *data, uint32_t id, int seq, int res, const char *message)
{
  WpCore *self = WP_CORE (data);

  /* protocol socket disconnected; schedule disconnecting our core */
  if (id == 0 && res == -EPIPE) {
    wp_context_idle_add_closure (WP_CONTEXT (self->main_context), NULL,
        g_cclosure_new_object (
            G_CALLBACK (core_disconnect_async), G_OBJECT (self)));
  }
}

static const struct pw_core_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .info = core_info,
  .done = core_done,
  .error = core_error,
};

static gboolean
async_tasks_finish (gpointer key, gpointer value, gpointer user_data)
{
  GTask *task = G_TASK (value);
  g_return_val_if_fail (task, FALSE);

  g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT, "core disconnected");
  return TRUE;
}

static void
proxy_core_destroy (void *data)
{
  WpCore *self = WP_CORE (data);
  g_hash_table_foreach_remove (self->async_tasks, async_tasks_finish, NULL);
  g_clear_pointer (&self->info, pw_core_info_free);
  spa_hook_remove(&self->core_listener);
  spa_hook_remove(&self->proxy_core_listener);
  self->pw_core = NULL;
  wp_debug_object (self, "emit disconnected");
  g_signal_emit (self, signals[SIGNAL_DISCONNECTED], 0);
  wp_object_update_features (WP_OBJECT (self), 0, WP_CORE_FEATURE_CONNECTED);
}

static const struct pw_proxy_events proxy_core_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_core_destroy,
};

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

static gboolean
wp_core_connect_internal (WpCore *self)
{
  struct pw_context *pw_context =
      wp_context_get_pw_context (WP_CONTEXT (self->main_context));
  struct pw_properties *p = NULL;

  /* Don't do anything if core is already connected */
  if (self->pw_core)
    return TRUE;

  /* Connect */
  p = self->properties ? wp_properties_to_pw_properties (self->properties) : NULL;

  if (self->main_socket_fd == -1) {
    self->pw_core = pw_context_connect (pw_context, p, 0);
  } else {
    self->pw_core = pw_context_connect_fd (pw_context,
        self->main_socket_fd, p, 0);
  }

  if (!self->pw_core)
    return FALSE;

  /* Add the core listeners */
  pw_core_add_listener (self->pw_core, &self->core_listener, &core_events, self);
  pw_proxy_add_listener((struct pw_proxy*)self->pw_core,
      &self->proxy_core_listener, &proxy_core_events, self);

  /* Add the registry listener */
  wp_registry_attach (&self->registry, self->pw_core);

  return TRUE;
}

static void
wp_core_init (WpCore * self)
{
  self->main_socket_fd = -1;
  wp_registry_init (&self->registry);
  self->async_tasks = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);

  wp_core_register_object (self,
      g_object_new (WP_TYPE_INTERNAL_COMP_LOADER, NULL));
}

static void
wp_core_constructed (GObject * object)
{
  WpCore *self = WP_CORE (object);
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

  G_OBJECT_CLASS (wp_core_parent_class)->constructed (object);
}

static void
wp_core_dispose (GObject * obj)
{
  WpCore *self = WP_CORE (obj);

  wp_registry_clear (&self->registry);
  wp_object_update_features (WP_OBJECT (self), 0, WP_CORE_FEATURE_COMPONENTS);

  G_OBJECT_CLASS (wp_core_parent_class)->dispose (obj);
}

static void
wp_core_finalize (GObject * obj)
{
  WpCore *self = WP_CORE (obj);

  /* the registry was already cleared in dispose() */
  if (self->pw_core)
    pw_core_disconnect (self->pw_core);

  g_clear_object (&self->main_context);

  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_pointer (&self->g_main_context, g_main_context_unref);
  g_clear_pointer (&self->async_tasks, g_hash_table_unref);
  g_clear_object (&self->conf);

  wp_debug_object (self, "WpCore destroyed");

  G_OBJECT_CLASS (wp_core_parent_class)->finalize (obj);
}

static void
wp_core_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpCore *self = WP_CORE (object);

  switch (property_id) {
  case PROP_PROPERTIES:
    g_value_take_boxed (value, wp_core_get_properties (self));
    break;
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
  case PROP_G_MAIN_CONTEXT:
    self->g_main_context = g_value_dup_boxed (value);
    break;
  case PROP_PROPERTIES:
    self->properties = g_value_dup_boxed (value);
    break;
  case PROP_CONF:
    self->conf = g_value_dup_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static WpObjectFeatures
wp_core_get_supported_features (WpObject * self)
{
  return WP_CORE_FEATURE_CONNECTED |
      WP_CORE_FEATURE_COMPONENTS;
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
      if (missing & WP_CORE_FEATURE_CONNECTED)
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
on_components_loaded (WpCore * self, GAsyncResult *res,
    WpTransition * transition)
{
  g_autoptr (GError) error = NULL;

  if (!wp_core_load_component_finish (self, res, &error)) {
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

  switch (step) {
    case STEP_CONNECT: {
      wp_info_object (self, "connecting to pipewire...");

      if (!wp_core_connect_internal (self)) {
        wp_transition_return_error (WP_TRANSITION (transition), g_error_new (
            WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_SERVICE_UNAVAILABLE,
            "Failed to connect to PipeWire"));
      }
      break;
    }

    case STEP_LOAD_COMPONENTS: {
      g_autoptr (WpProperties) props = wp_core_get_properties (self);
      const gchar *profile = wp_properties_get (props, "wireplumber.profile");

      wp_info_object (self,
          "parsing & loading components for profile [%s]...", profile);

      /* Load components that are defined in the configuration section */
      wp_core_load_component (self, profile, "profile", NULL, NULL, NULL,
          (GAsyncReadyCallback) on_components_loaded, transition);
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

  if (features & WP_CORE_FEATURE_CONNECTED) {
    wp_core_disconnect_internal (self);
  } else if (features & WP_CORE_FEATURE_COMPONENTS) {
    wp_core_clear_registry (self);

    /* stay connected; watch the pipewire registry again */
    if (self->pw_core && self->registry.objects)
      wp_registry_attach (&self->registry, self->pw_core);
  }
}

static void
wp_core_class_init (WpCoreClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;

  object_class->constructed = wp_core_constructed;
  object_class->dispose = wp_core_dispose;
  object_class->finalize = wp_core_finalize;
  object_class->get_property = wp_core_get_property;
  object_class->set_property = wp_core_set_property;

  wpobject_class->get_supported_features = wp_core_get_supported_features;
  wpobject_class->activate_get_next_step = wp_core_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_core_activate_execute_step;
  wpobject_class->deactivate = wp_core_deactivate;

  properties[PROP_G_MAIN_CONTEXT] = g_param_spec_boxed ("g-main-context", "g-main-context",
      "A GMainContext to attach to", G_TYPE_MAIN_CONTEXT,
      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_PROPERTIES] = g_param_spec_boxed ("properties", "properties", "Extra properties",
      WP_TYPE_PROPERTIES, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_CONF] = g_param_spec_object ("conf", "conf", "The main configuration file",
      WP_TYPE_CONF, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[SIGNAL_CONNECTED] = g_signal_new ("connected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);

  signals[SIGNAL_DISCONNECTED] = g_signal_new ("disconnected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
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
 * \brief Gets the main configuration file of the core
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer full) (nullable): the main configuration file
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
 * \brief Gets the internal PipeWire core of the core
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer none) (nullable): the internal pw_core object,
 *   or NULL if the core is not connected to PipeWire
 */
struct pw_core *
wp_core_get_pw_core (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->pw_core;
}

/*!
 * \brief Gets the bound id of the client object that is created as a result
 * of this core being connected to the PipeWire daemon
 *
 * \ingroup wpcore
 * \since 0.4.16
 * \param self the core
 * \returns the bound id of this client
 */
guint32
wp_core_get_own_bound_id (WpCore * self)
{
  struct pw_client *client;

  g_return_val_if_fail (wp_core_is_connected (self), SPA_ID_INVALID);

  client = pw_core_get_client (self->pw_core);
  return pw_proxy_get_bound_id ((struct pw_proxy *) client);
}

/*!
 * \brief Gets the cookie of the core's connected PipeWire instance
 *
 * \ingroup wpcore
 * \param self the core
 * \returns The cookie of the PipeWire instance that \a self is connected to.
 *     The cookie is a unique random number for identifying an instance of
 *     PipeWire
 */
guint32
wp_core_get_remote_cookie (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), 0);

  return self->info->cookie;
}

/*!
 * \brief Gets the name of the core's connected PipeWire instance
 * \ingroup wpcore
 * \param self the core
 * \returns The name of the PipeWire instance that \a self is connected to
 */
const gchar *
wp_core_get_remote_name (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);

  return self->info->name;
}

/*!
 * \brief Gets the user name of the core's connected PipeWire instance
 * \ingroup wpcore
 * \param self the core
 * \returns The name of the user that started the PipeWire instance that
 *     \a self is connected to
 */
const gchar *
wp_core_get_remote_user_name (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);

  return self->info->user_name;
}

/*!
 * \brief Gets the host name of the core's connected PipeWire instance
 * \ingroup wpcore
 * \param self the core
 * \returns The name of the host where the PipeWire instance that
 *     \a self is connected to is running on
 */
const gchar *
wp_core_get_remote_host_name (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);

  return self->info->host_name;
}

/*!
 * \brief Gets the version of the core's connected PipeWire instance
 * \ingroup wpcore
 * \param self the core
 * \returns The version of the PipeWire instance that \a self is connected to
 */
const gchar *
wp_core_get_remote_version (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);

  return self->info->version;
}

/*!
 * \brief Gets the properties of the core's connected PipeWire instance
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer full): the properties of the PipeWire instance that
 *     \a self is connected to
 */
WpProperties *
wp_core_get_remote_properties (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);

  return wp_properties_new_wrap_dict (self->info->props);
}

/*!
 * \brief Gets the properties of the core
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer full): the properties of \a self
 */
WpProperties *
wp_core_get_properties (WpCore * self)
{
  struct pw_context *pw_context;

  g_return_val_if_fail (WP_IS_CORE (self), NULL);

  pw_context = wp_context_get_pw_context (WP_CONTEXT (self->main_context));

  /* pw_core has all the properties of the pw_context,
     plus our updates, passed in pw_context_connect() */
  if (self->pw_core)
    return wp_properties_new_wrap (pw_core_get_properties (self->pw_core));

  /* if there is no connection yet, return the properties of the context */
  else if (!self->properties)
    return wp_properties_new_wrap (pw_context_get_properties (pw_context));

  /* ... plus any further updates that we got from wp_core_update_properties() */
  else {
    /* we need to copy here in order to augment with the updates */
    WpProperties *ret =
        wp_properties_new_copy (pw_context_get_properties (pw_context));
    wp_properties_update (ret, self->properties);
    return ret;
  }
}

/*!
 * \brief Updates the properties of \a self on the connection, making them
 * appear on the client object that represents this connection.
 *
 * If \a self is not connected yet, these properties are stored and passed to
 * pw_context_connect() when connecting.
 *
 * \ingroup wpcore
 * \param self the core
 * \param updates (transfer full): updates to apply to the properties of
 *    \a self; this does not need to include properties that have not changed
 */
void
wp_core_update_properties (WpCore * self, WpProperties * updates)
{
  g_autoptr (WpProperties) upd = updates;

  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (updates != NULL);

  /* store updates locally so that
    - they persist after disconnection
    - we can pass them to pw_context_connect */
  if (!self->properties)
    self->properties = wp_properties_new_empty ();
  wp_properties_update (self->properties, upd);

  if (self->pw_core)
    pw_core_update_properties (self->pw_core, wp_properties_peek_dict (upd));
}

/*!
 * \brief Asks the PipeWire server to call the \a callback via an event.
 *
 * Since methods are handled in-order and events are delivered
 * in-order, this can be used as a barrier to ensure all previous
 * methods and the resulting events have been handled.
 *
 * In both success and error cases, \a callback is always called.
 * Use wp_core_sync_finish() from within the \a callback to determine whether
 * the operation completed successfully or if an error occurred.
 *
 * \ingroup wpcore
 * \param self the core
 * \param cancellable (nullable): a GCancellable to cancel the operation
 * \param callback (scope async)(closure user_data): a function to call when the operation is done
 * \param user_data data to pass to \a callback
 * \returns TRUE if the sync operation was started, FALSE if an error
 *   occurred before returning from this function
 */
gboolean
wp_core_sync (WpCore * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  return wp_core_sync_closure (self, cancellable,
      g_cclosure_new (G_CALLBACK (callback), user_data, NULL));
}

static void
invoke_closure (GObject * obj, GAsyncResult * res, gpointer data)
{
  GClosure *closure = data;
  GValue values[2] = { G_VALUE_INIT, G_VALUE_INIT };
  g_value_init (&values[0], G_TYPE_OBJECT);
  g_value_init (&values[1], G_TYPE_OBJECT);
  g_value_set_object (&values[0], obj);
  g_value_set_object (&values[1], res);
  g_closure_invoke (closure, NULL, 2, values, NULL);
  g_value_unset (&values[0]);
  g_value_unset (&values[1]);
  g_closure_unref (closure);
}

/*!
 * \brief Asks the PipeWire server to invoke the \a closure via an event.
 *
 * Since methods are handled in-order and events are delivered
 * in-order, this can be used as a barrier to ensure all previous
 * methods and the resulting events have been handled.
 *
 * In both success and error cases, \a closure is always invoked.
 * Use wp_core_sync_finish() from within the \a closure to determine whether
 * the operation completed successfully or if an error occurred.
 *
 * \ingroup wpcore
 * \since 0.4.6
 * \param self the core
 * \param cancellable (nullable): a GCancellable to cancel the operation
 * \param closure (transfer floating): a closure to invoke when the operation
 *    is done
 * \returns TRUE if the sync operation was started, FALSE if an error
 *   occurred before returning from this function
 */
gboolean
wp_core_sync_closure (WpCore * self, GCancellable * cancellable,
    GClosure * closure)
{
  g_autoptr (GTask) task = NULL;
  int seq;

  g_return_val_if_fail (WP_IS_CORE (self), FALSE);
  g_return_val_if_fail (closure, FALSE);

  closure = g_closure_ref (closure);
  g_closure_sink (closure);
  if (G_CLOSURE_NEEDS_MARSHAL (closure))
    g_closure_set_marshal (closure, g_cclosure_marshal_VOID__OBJECT);

  task = g_task_new (self, cancellable, invoke_closure, closure);

  if (G_UNLIKELY (!self->pw_core)) {
    g_warn_if_reached ();
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT, "No pipewire core");
    return FALSE;
  }

  seq = pw_core_sync (self->pw_core, 0, 0);
  if (G_UNLIKELY (seq < 0)) {
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED, "pw_core_sync failed: %s",
        g_strerror (-seq));
    return FALSE;
  }

  wp_debug_object (self, "sync, seq 0x%x, task " WP_OBJECT_FORMAT,
      seq, WP_OBJECT_ARGS (task));

  g_hash_table_insert (self->async_tasks, GINT_TO_POINTER (seq),
      g_steal_pointer (&task));
  return TRUE;
}

/*!
 * \brief This function is meant to be called from within the callback of
 * wp_core_sync() in order to determine the success or failure of the operation.
 *
 * \ingroup wpcore
 * \param self the core
 * \param res a GAsyncResult
 * \param error (out) (optional): the error that occurred, if any
 * \returns TRUE if the operation succeeded, FALSE otherwise
 */
gboolean
wp_core_sync_finish (WpCore * self, GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_CORE (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);

  return g_task_propagate_boolean (G_TASK (res), error);
}


/*!
 * \brief Finds a registered object
 *
 * \ingroup wpcore
 * \param self the core
 * \param func (scope call): a function that takes the object being searched
 *   as the first argument and \a data as the second. it should return TRUE if
 *   the object is found or FALSE otherwise
 * \param data the second argument to \a func
 * \returns (transfer full) (type GObject*) (nullable): the registered object
 *   or NULL if not found
 */
gpointer
wp_core_find_object (WpCore * self, GEqualFunc func, gconstpointer data)
{
  GObject *object;
  guint i;

  g_return_val_if_fail (WP_IS_CORE (self), NULL);

  /* prevent bad things when called from within wp_registry_clear() */
  if (G_UNLIKELY (!self->registry.objects))
    return NULL;

  for (i = 0; i < self->registry.objects->len; i++) {
    object = g_ptr_array_index (self->registry.objects, i);
    if (func (object, data))
      return g_object_ref (object);
  }

  return NULL;
}

/*!
 * \brief Registers \a obj with the core, making it appear on WpObjectManager
 * instances as well.
 *
 * The core will also maintain a ref to that object until it
 * is removed.
 *
 * \ingroup wpcore
 * \param self the core
 * \param obj (transfer full) (type GObject*): the object to register
 */
void
wp_core_register_object (WpCore * self, gpointer obj)
{
  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (G_IS_OBJECT (obj));

  /* prevent bad things when called from within wp_registry_clear() */
  if (G_UNLIKELY (!self->registry.objects)) {
    g_object_unref (obj);
    return;
  }

  /* ensure the registered object is associated with this core */
  if (WP_IS_OBJECT (obj)) {
    g_autoptr (WpCore) obj_core = wp_object_get_core (WP_OBJECT (obj));
    g_return_if_fail (obj_core == self);
  }

  g_ptr_array_add (self->registry.objects, obj);

  /* notify object managers */
  wp_registry_notify_add_object (&self->registry, obj);
}

/*!
 * \brief Detaches and unrefs the specified object from this core.
 *
 * \ingroup wpcore
 * \param self the core
 * \param obj (transfer none) (type GObject*): a pointer to the object to remove
 */
void
wp_core_remove_object (WpCore * self, gpointer obj)
{
  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (G_IS_OBJECT (obj));

  /* prevent bad things when called from within wp_registry_clear() */
  if (G_UNLIKELY (!self->registry.objects))
    return;

  /* notify object managers */
  wp_registry_notify_rm_object (&self->registry, obj);

  g_ptr_array_remove_fast (self->registry.objects, obj);
}

/*!
 * \brief Test if a global feature is provided
 *
 * \ingroup wpcore
 * \param self the core
 * \param feature the feature name
 * \returns TRUE if the feature is provided, FALSE otherwise
 */
gboolean
wp_core_test_feature (WpCore * self, const gchar * feature)
{
  return g_ptr_array_find_with_equal_func (self->registry.features, feature,
      g_str_equal, NULL);
}

WpRegistry *
wp_core_get_registry (WpCore * self)
{
  return &self->registry;
}

WpCore *
wp_registry_get_core (WpRegistry * self)
{
  return SPA_CONTAINER_OF (self, WpCore, registry);
}
