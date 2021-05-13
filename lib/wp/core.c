/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/*!
 * @file core.c
 */
#define G_LOG_DOMAIN "wp-core"

#include "core.h"
#include "wp.h"
#include "private/registry.h"

#include <pipewire/pipewire.h>

#include <spa/utils/result.h>
#include <spa/debug/types.h>

/*
 * Integration between the PipeWire main loop and GMainLoop
 */

#define WP_LOOP_SOURCE(x) ((WpLoopSource *) x)

typedef struct _WpLoopSource WpLoopSource;
struct _WpLoopSource
{
  GSource parent;
  struct pw_loop *loop;
};

static gboolean
wp_loop_source_dispatch (GSource * s, GSourceFunc callback, gpointer user_data)
{
  int result;

  wp_trace_boxed (G_TYPE_SOURCE, s, "entering pw main loop");

  pw_loop_enter (WP_LOOP_SOURCE(s)->loop);
  result = pw_loop_iterate (WP_LOOP_SOURCE(s)->loop, 0);
  pw_loop_leave (WP_LOOP_SOURCE(s)->loop);

  wp_trace_boxed (G_TYPE_SOURCE, s, "leaving pw main loop");

  if (G_UNLIKELY (result < 0))
    wp_warning_boxed (G_TYPE_SOURCE, s,
        "pw_loop_iterate failed: %s", spa_strerror (result));

  return G_SOURCE_CONTINUE;
}

static void
wp_loop_source_finalize (GSource * s)
{
  pw_loop_destroy (WP_LOOP_SOURCE(s)->loop);
}

static GSourceFuncs source_funcs = {
  NULL,
  NULL,
  wp_loop_source_dispatch,
  wp_loop_source_finalize
};

static GSource *
wp_loop_source_new (void)
{
  GSource *s = g_source_new (&source_funcs, sizeof (WpLoopSource));
  WP_LOOP_SOURCE(s)->loop = pw_loop_new (NULL);

  g_source_add_unix_fd (s,
      pw_loop_get_fd (WP_LOOP_SOURCE(s)->loop),
      G_IO_IN | G_IO_ERR | G_IO_HUP);

  return (GSource *) s;
}

/*!
 * @struct WpCore
 *
 * @section core_section Core
 *
 * @brief The core is the central object around which everything operates. It is
 * essential to create a [WpCore](@ref core_section) before using any other WirePlumber API.
 *
 * The core object has the following responsibilities:
 *  * it initializes the PipeWire library
 *  * it creates a `pw_context` and allows connecting to the PipeWire server,
 *    creating a local `pw_core`
 *  * it glues the PipeWire library's event loop system with GMainLoop
 *  * it maintains a list of registered objects, which other classes use
 *    to keep objects loaded permanently into memory
 *  * it watches the PipeWire registry and keeps track of remote and local
 *    objects that appear in the registry, making them accessible through
 *    the [WpObjectManager](@ref object_manager_section) API.
 *
 */
struct _WpCore
{
  GObject parent;

  /* main loop integration */
  GMainContext *g_main_context;

  /* extra properties */
  WpProperties *properties;

  /* pipewire main objects */
  struct pw_context *pw_context;
  struct pw_core *pw_core;
  struct pw_core_info *info;

  /* pipewire main listeners */
  struct spa_hook core_listener;
  struct spa_hook proxy_core_listener;

  WpRegistry registry;
  GHashTable *async_tasks; // <int seq, GTask*>
};

/*!
 * @memberof WpCore
 *
 * @props @b g-main-context
 *
 * @code
 * "g-main-context" GMainContext *
 * @endcode
 *
 * Flags : Read / Write / Construct Only
 *
 * @props @b properties
 *
 * @code
 * "properties" WpProperties *
 * @endcode
 *
 * Flags : Read / Write / Construct Only
 *
 * @props @b pw-context
 *
 * @code
 * "pw-context" gpointer *
 * @endcode
 *
 * Flags : Read
 *
 * @props @b pw-core
 *
 * @code
 * "pw-core" gpointer *
 * @endcode
 *
 * Flags : Read
 */
enum {
  PROP_0,
  PROP_G_MAIN_CONTEXT,
  PROP_PROPERTIES,
  PROP_PW_CONTEXT,
  PROP_PW_CORE,
};

/*!
 * @memberof WpCore
 *
 * @signal @b connected
 *
 * Params:
 * @arg @c self: the core
 *
 * Emitted when the core is successfully connected to the PipeWire server
 * @code
 * connected_callback (WpCore * self,
 *                     gpointer user_data)
 * @endcode
 *
 *
 * @signal @b disconnected
 *
 * Params:
 * @arg @c self: the core
 *
 * Emitted when the core is disconnected from the PipeWire server
 * @code
 * disconnected_callback (WpCore * self,
 *                        gpointer user_data)
 * @endcode
 *
 */
enum {
  SIGNAL_CONNECTED,
  SIGNAL_DISCONNECTED,
  NUM_SIGNALS
};

static guint32 signals[NUM_SIGNALS];


G_DEFINE_TYPE (WpCore, wp_core, G_TYPE_OBJECT)

static void
core_info (void *data, const struct pw_core_info * info)
{
  WpCore *self = WP_CORE (data);
  gboolean new_connection = (self->info == NULL);

  self->info = pw_core_info_update (self->info, info);

  wp_info_object (self, "connected to server: %s, cookie: %u",
      self->info->name, self->info->cookie);

  if (new_connection)
    g_signal_emit (self, signals[SIGNAL_CONNECTED], 0);
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

static gboolean
core_disconnect_async (WpCore * self)
{
  wp_core_disconnect (self);
  return G_SOURCE_REMOVE;
}

static void
core_error (void *data, uint32_t id, int seq, int res, const char *message)
{
  WpCore *self = WP_CORE (data);

  /* protocol socket disconnected; schedule disconnecting our core */
  if (id == 0 && res == -EPIPE) {
    wp_core_idle_add_closure (self, NULL, g_cclosure_new_object (
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
  self->pw_core = NULL;
  wp_debug_object (self, "emit disconnected");
  g_signal_emit (self, signals[SIGNAL_DISCONNECTED], 0);
}

static const struct pw_proxy_events proxy_core_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_core_destroy,
};

static void
wp_core_init (WpCore * self)
{
  wp_registry_init (&self->registry);
  self->async_tasks = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
}

static void
wp_core_constructed (GObject *object)
{
  WpCore *self = WP_CORE (object);
  g_autoptr (GSource) source = NULL;

  /* loop */
  source = wp_loop_source_new ();
  g_source_attach (source, self->g_main_context);

  /* context */
  if (!self->pw_context) {
    struct pw_properties *p = NULL;
    const gchar *str = NULL;

    /* properties are fully stored in the pw_context, no need to keep a copy */
    p = self->properties ?
        wp_properties_unref_and_take_pw_properties (self->properties) : NULL;
    self->properties = NULL;

    self->pw_context = pw_context_new (WP_LOOP_SOURCE(source)->loop, p,
        sizeof (grefcount));
    g_return_if_fail (self->pw_context);

    /* use the same config option as pipewire to set the log level */
    p = (struct pw_properties *) pw_context_get_properties (self->pw_context);
    if (!g_getenv("WIREPLUMBER_DEBUG") &&
        (str = pw_properties_get(p, "log.level")) != NULL)
      wp_log_set_level (str);

    /* Init refcount */
    grefcount *rc = pw_context_get_user_data (self->pw_context);
    g_return_if_fail (rc);
    g_ref_count_init (rc);
  } else {
    /* Increase refcount */
    grefcount *rc = pw_context_get_user_data (self->pw_context);
    g_return_if_fail (rc);
    g_ref_count_inc (rc);
  }

  G_OBJECT_CLASS (wp_core_parent_class)->constructed (object);
}

static void
wp_core_dispose (GObject * obj)
{
  WpCore *self = WP_CORE (obj);

  wp_registry_clear (&self->registry);

  G_OBJECT_CLASS (wp_core_parent_class)->dispose (obj);
}

static void
wp_core_finalize (GObject * obj)
{
  WpCore *self = WP_CORE (obj);
  grefcount *rc = pw_context_get_user_data (self->pw_context);
  g_return_if_fail (rc);

  wp_core_disconnect (self);

  /* Clear pw-context if refcount reaches 0 */
  if (g_ref_count_dec (rc))
    g_clear_pointer (&self->pw_context, pw_context_destroy);

  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_pointer (&self->g_main_context, g_main_context_unref);
  g_clear_pointer (&self->async_tasks, g_hash_table_unref);

  wp_debug_object (self, "WpCore destroyed");

  G_OBJECT_CLASS (wp_core_parent_class)->finalize (obj);
}

static void
wp_core_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpCore *self = WP_CORE (object);

  switch (property_id) {
  case PROP_G_MAIN_CONTEXT:
    g_value_set_boxed (value, self->g_main_context);
    break;
  case PROP_PROPERTIES:
    g_value_take_boxed (value, wp_core_get_properties (self));
    break;
  case PROP_PW_CONTEXT:
    g_value_set_pointer (value, self->pw_context);
    break;
  case PROP_PW_CORE:
    g_value_set_pointer (value, self->pw_core);
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
  case PROP_PW_CONTEXT:
    self->pw_context = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_core_class_init (WpCoreClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_core_constructed;
  object_class->dispose = wp_core_dispose;
  object_class->finalize = wp_core_finalize;
  object_class->get_property = wp_core_get_property;
  object_class->set_property = wp_core_set_property;

  g_object_class_install_property (object_class, PROP_G_MAIN_CONTEXT,
      g_param_spec_boxed ("g-main-context", "g-main-context",
          "A GMainContext to attach to", G_TYPE_MAIN_CONTEXT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties", "Extra properties",
          WP_TYPE_PROPERTIES,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_CONTEXT,
      g_param_spec_pointer ("pw-context", "pw-context", "The pipewire context",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_CORE,
      g_param_spec_pointer ("pw-core", "pw-core", "The pipewire core",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /*
   * WpCore::connected:
   *
   * @self: the core
   *
   * Emitted when the core is successfully connected to the PipeWire server
   */
  signals[SIGNAL_CONNECTED] = g_signal_new ("connected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);

  /*
   * WpCore::disconnected:
   *
   * @self: the core
   *
   * Emitted when the core is disconnected from the PipeWire server
   */
  signals[SIGNAL_DISCONNECTED] = g_signal_new ("disconnected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
}

/*!
 * @memberof WpCore
 *
 * @param context: (transfer none) (nullable): the
 * <a href="https://developer.gnome.org/glib/unstable/glib-The-Main-Event-Loop.html#GMainContext">
 * GMainContext</a> to use for events
 * @param properties: (transfer full) (nullable): additional properties, which are
 *   passed to `pw_context_new` and `pw_context_connect`
 *
 * @returns (transfer full): a new [WpCore](@ref core_section)
 */
WpCore *
wp_core_new (GMainContext *context, WpProperties * properties)
{
  g_autoptr (WpProperties) props = properties;
  return g_object_new (WP_TYPE_CORE,
      "g-main-context", context,
      "properties", properties,
      "pw-context", NULL,
      NULL);
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @brief Clones a core with the same context as @em self
 *
 * @returns (transfer full): the clone [WpCore](@ref core_section)
 */
WpCore *
wp_core_clone (WpCore * self)
{
  return g_object_new (WP_TYPE_CORE,
      "g-main-context", self->g_main_context,
      "properties", self->properties,
      "pw-context", self->pw_context,
      NULL);
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @returns (transfer none) (nullable): the
 * <a href="https://developer.gnome.org/glib/unstable/glib-The-Main-Event-Loop.html#GMainContext">
 * GMainContext</a> that is in use by this core for events
 */
GMainContext *
wp_core_get_g_main_context (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->g_main_context;
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @returns (transfer none): the internal `pw_context` object
 */
struct pw_context *
wp_core_get_pw_context (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->pw_context;
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @returns (transfer none) (nullable): the internal `pw_core` object,
 *   or %NULL if the core is not connected to PipeWire
 */
struct pw_core *
wp_core_get_pw_core (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->pw_core;
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @brief Connects this core to the PipeWire server. When connection succeeds,
 * the [WpCore](@ref core_section) [connected](@ref signal_connected_section) signal is emitted
 *
 * @returns %TRUE if the core is effectively connected or %FALSE if
 *   connection failed
 */
gboolean
wp_core_connect (WpCore *self)
{
  struct pw_properties *p = NULL;

  g_return_val_if_fail (WP_IS_CORE (self), FALSE);

  /* Don't do anything if core is already connected */
  if (self->pw_core)
    return TRUE;

  /* Connect */
  p = self->properties ? wp_properties_to_pw_properties (self->properties) : NULL;
  self->pw_core = pw_context_connect (self->pw_context, p, 0);
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

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @brief Disconnects this core from the PipeWire server. This also effectively
 * destroys all [WpCore](@ref core_section) objects that were created through the registry,
 * destroys the `pw_core` and finally emits the [WpCore](@ref core_section)
 * [disconnected](@ref signal_disconnected_section) signal.
 */
void
wp_core_disconnect (WpCore *self)
{
  wp_registry_detach (&self->registry);

  /* pw_core_disconnect destroys the core proxy
    and we continue in proxy_core_destroy() */
  if (self->pw_core)
    pw_core_disconnect (self->pw_core);
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @returns %TRUE if the core is connected to PipeWire, %FALSE otherwise
 */
gboolean
wp_core_is_connected (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), FALSE);
  return self->pw_core != NULL;
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @returns The cookie of the PipeWire instance that @em self is connected to.
 *     The cookie is a unique random number for identifying an instance of
 *     PipeWire
 */
guint32
wp_core_get_remote_cookie (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), 0);
  g_return_val_if_fail (self->info, 0);

  return self->info->cookie;
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @returns The name of the PipeWire instance that @em self is connected to
 */
const gchar *
wp_core_get_remote_name (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);
  g_return_val_if_fail (self->info, NULL);

  return self->info->name;
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @returns The name of the user that started the PipeWire instance that
 *     @em self is connected to
 */
const gchar *
wp_core_get_remote_user_name (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);
  g_return_val_if_fail (self->info, NULL);

  return self->info->user_name;
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @returns The name of the host where the PipeWire instance that
 *     @em self is connected to is running on
 */
const gchar *
wp_core_get_remote_host_name (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);
  g_return_val_if_fail (self->info, NULL);

  return self->info->host_name;
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @returns The version of the PipeWire instance that @em self is connected to
 */
const gchar *
wp_core_get_remote_version (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);
  g_return_val_if_fail (self->info, NULL);

  return self->info->version;
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @returns (transfer full): the properties of the PipeWire instance that
 *     @em self is connected to
 */
WpProperties *
wp_core_get_remote_properties (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);
  g_return_val_if_fail (self->info, NULL);

  return wp_properties_new_wrap_dict (self->info->props);
}

/*!
 * @memberof WpCore
 * @param self: the core
 *
 * @returns (transfer full): the properties of @em self
 */
WpProperties *
wp_core_get_properties (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);

  /* pw_core has all the properties of the pw_context,
     plus our updates, passed in pw_context_connect() */
  if (self->pw_core)
    return wp_properties_new_wrap (pw_core_get_properties (self->pw_core));

  /* if there is no connection yet, return the properties of the context */
  else if (!self->properties)
    return wp_properties_new_wrap (pw_context_get_properties (self->pw_context));

  /* ... plus any further updates that we got from wp_core_update_properties() */
  else {
    /* we need to copy here in order to augment with the updates */
    WpProperties *ret =
        wp_properties_new_copy (pw_context_get_properties (self->pw_context));
    wp_properties_update (ret, self->properties);
    return ret;
  }
}

/*!
 * @memberof WpCore
 * @param self: the core
 * @param updates: (transfer full): updates to apply to the properties of @em self;
 *    this does not need to include properties that have not changed
 *
 * @brief Updates the properties of @em self on the connection, making them appear on
 * the client object that represents this connection.
 *
 * If @em self is not connected yet, these properties are stored and passed to
 * `pw_context_connect` when connecting.
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
 * @memberof WpCore
 * @param self: the core
 * @param source: (out) (optional): the source
 * @param function: (scope notified): the function to call
 * @param data: (closure): data to pass to @em function
 * @param destroy: (nullable): a function to destroy @em data
 *
 * @brief Adds an idle callback to be called in the same
 * <a href="https://developer.gnome.org/glib/unstable/glib-The-Main-Event-Loop.html#GMainContext">
 * GMainContext</a> as the one used by this core. This is essentially the same as g_idle_add_full(),
 * but it adds the created
 * <a href="https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html#GSource">
 * GSource</a> on the 
 * <a href="https://developer.gnome.org/glib/unstable/glib-The-Main-Event-Loop.html#GMainContext">
 * GMainContext</a> used by this core instead of the default context.
 */
void
wp_core_idle_add (WpCore * self, GSource **source, GSourceFunc function,
    gpointer data, GDestroyNotify destroy)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CORE (self));

  s = g_idle_source_new ();
  g_source_set_callback (s, function, data, destroy);
  g_source_attach (s, self->g_main_context);

  if (source)
    *source = g_source_ref (s);
}

/*!
 * @memberof WpCore
 * @param self: the core
 * @param source: (out) (optional): the source
 * @param closure: the closure to invoke
 *
 * @brief Adds an idle callback to be called in the same
 * <a href="https://developer.gnome.org/glib/unstable/glib-The-Main-Event-Loop.html#GMainContext">
 * GMainContext </a> as the one used by this core.
 *
 * This is the same as wp_core_idle_add(), but it allows you to specify a
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Closures.html#GClosure-struct">
 * GClosure</a> instead of a C callback.
 */
void
wp_core_idle_add_closure (WpCore * self, GSource **source, GClosure * closure)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (closure != NULL);

  s = g_idle_source_new ();
  g_source_set_closure (s, closure);
  g_source_attach (s, self->g_main_context);

  if (source)
    *source = g_source_ref (s);
}

/*!
 * @memberof WpCore
 * @param self: the core
 * @param source: (out) (optional): the source
 * @param timeout_ms: the timeout in milliseconds
 * @param function: (scope notified): the function to call
 * @param data: (closure): data to pass to @em function
 * @param destroy: (nullable): a function to destroy @em data
 *
 * @brief Adds a timeout callback to be called at regular intervals in the same
 * <a href="https://developer.gnome.org/glib/unstable/glib-The-Main-Event-Loop.html#GMainContext">
 * GMainContext</a> as the one used by this core. The function is called repeatedly
 * until it returns FALSE, at which point the timeout is automatically destroyed
 * and the function will not be called again. The first call to the function
 * will be at the end of the first interval. This is essentially the same as
 * g_timeout_add_full(), but it adds the created
 * <a href="https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html#GSource">
 * GSource</a> on the 
 * <a href="https://developer.gnome.org/glib/unstable/glib-The-Main-Event-Loop.html#GMainContext">
 * GMainContext</a> used by this core instead of the default context.
 */
void
wp_core_timeout_add (WpCore * self, GSource **source, guint timeout_ms,
    GSourceFunc function, gpointer data, GDestroyNotify destroy)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CORE (self));

  s = g_timeout_source_new (timeout_ms);
  g_source_set_callback (s, function, data, destroy);
  g_source_attach (s, self->g_main_context);

  if (source)
    *source = g_source_ref (s);
}

/*!
 * @memberof WpCore
 * @param self: the core
 * @param source: (out) (optional): the source
 * @param timeout_ms: the timeout in milliseconds
 * @param closure: the closure to invoke
 *
 * @brief Adds a timeout callback to be called at regular intervals in the same
 * <a href="https://developer.gnome.org/glib/unstable/glib-The-Main-Event-Loop.html#GMainContext">
 * GMainContext</a> as the one used by this core.
 *
 * This is the same as wp_core_timeout_add(), but it allows you to specify a
 * <a href="https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html#GSource">
 * GClosure</a> instead of a C callback.
 */
void
wp_core_timeout_add_closure (WpCore * self, GSource **source, guint timeout_ms,
    GClosure * closure)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (closure != NULL);

  s = g_timeout_source_new (timeout_ms);
  g_source_set_closure (s, closure);
  g_source_attach (s, self->g_main_context);

  if (source)
    *source = g_source_ref (s);
}

/*!
 * @memberof WpCore
 * @param self: the core
 * @param cancellable: (nullable):
 * a <a href="https://developer.gnome.org/gio/stable/GCancellable.html#GCancellable-struct">
 * GCancellable</a> to cancel the operation
 * @param callback: (scope async): a function to call when the operation is done
 * @param user_data: (closure): data to pass to @em callback
 *
 * @brief Asks the PipeWire server to call the @em callback via an event.
 *
 * Since methods are handled in-order and events are delivered
 * in-order, this can be used as a barrier to ensure all previous
 * methods and the resulting events have been handled.
 *
 * In both success and error cases, @em callback is always called. Use
 * wp_core_sync_finish() from within the @em callback to determine whether
 * the operation completed successfully or if an error occurred.
 *
 * @returns %TRUE if the sync operation was started, %FALSE if an error
 *   occurred before returning from this function
 */
gboolean
wp_core_sync (WpCore * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  g_autoptr (GTask) task = NULL;
  int seq;

  g_return_val_if_fail (WP_IS_CORE (self), FALSE);

  task = g_task_new (self, cancellable, callback, user_data);

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
 * @memberof WpCore
 * @param self: the core
 * @param res: a
 * <a href="https://developer.gnome.org/gio/stable/GAsyncResult.html#GAsyncResult-struct">
 * GAsyncResult</a>
 * @param error: (out) (optional): the error that occurred, if any
 *
 * @brief This function is meant to be called from within the callback of
 * wp_core_sync() in order to determine the success or failure of the operation.
 *
 * @returns %TRUE if the operation succeeded, %FALSE otherwise
 */
gboolean
wp_core_sync_finish (WpCore * self, GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_CORE (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);

  return g_task_propagate_boolean (G_TASK (res), error);
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