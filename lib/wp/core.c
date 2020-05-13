/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpCore
 *
 * The core is the central object around which everything operates. It is
 * essential to create a #WpCore before using any other WirePlumber API.
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
 *    the #WpObjectManager API.
 */

#define G_LOG_DOMAIN "wp-core"

#include "core.h"
#include "wp.h"
#include "private.h"

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

/**
 * WpCore
 */

struct _WpCore
{
  GObject parent;

  /* main loop integration */
  GMainContext *context;

  /* extra properties */
  WpProperties *properties;

  /* pipewire main objects */
  struct pw_context *pw_context;
  struct pw_core *pw_core;

  /* pipewire main listeners */
  struct spa_hook core_listener;
  struct spa_hook proxy_core_listener;

  WpRegistry registry;
  GHashTable *async_tasks; // <int seq, GTask*>
};

enum {
  PROP_0,
  PROP_CONTEXT,
  PROP_PROPERTIES,
  PROP_PW_CONTEXT,
  PROP_PW_CORE,
};

enum {
  SIGNAL_CONNECTED,
  SIGNAL_DISCONNECTED,
  NUM_SIGNALS
};

static guint32 signals[NUM_SIGNALS];

/**
 * WP_TYPE_CORE:
 *
 * The #WpCore #GType
 */
G_DEFINE_TYPE (WpCore, wp_core, G_TYPE_OBJECT)

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
  .done = core_done,
  .error = core_error,
};

static void
proxy_core_destroy (void *data)
{
  WpCore *self = WP_CORE (data);
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
  struct pw_properties *p = NULL;

  /* loop */
  source = wp_loop_source_new ();
  g_source_attach (source, self->context);

  /* context */
  p = self->properties ? wp_properties_to_pw_properties (self->properties) : NULL;
  self->pw_context = pw_context_new (WP_LOOP_SOURCE(source)->loop, p, 0);

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

  wp_core_disconnect (self);
  g_clear_pointer (&self->pw_context, pw_context_destroy);

  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_pointer (&self->context, g_main_context_unref);
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
  case PROP_CONTEXT:
    g_value_set_boxed (value, self->context);
    break;
  case PROP_PROPERTIES:
    g_value_set_boxed (value, self->properties);
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
  case PROP_CONTEXT:
    self->context = g_value_dup_boxed (value);
    break;
  case PROP_PROPERTIES:
    self->properties = g_value_dup_boxed (value);
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

  g_object_class_install_property (object_class, PROP_CONTEXT,
      g_param_spec_boxed ("context", "context", "A GMainContext to attach to",
          G_TYPE_MAIN_CONTEXT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties", "Extra properties",
          WP_TYPE_PROPERTIES,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_CONTEXT,
      g_param_spec_pointer ("pw-context", "pw-context", "The pipewire context",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_CORE,
      g_param_spec_pointer ("pw-core", "pw-core", "The pipewire core",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * WpCore::connected:
   * @self: the core
   *
   * Emitted when the core is successfully connected to the PipeWire server
   */
  signals[SIGNAL_CONNECTED] = g_signal_new ("connected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);

  /**
   * WpCore::disconnected:
   * @self: the core
   *
   * Emitted when the core is disconnected from the PipeWire server
   */
  signals[SIGNAL_DISCONNECTED] = g_signal_new ("disconnected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
}

/**
 * wp_core_new:
 * @context: (transfer none) (nullable): the #GMainContext to use for events
 * @properties: (transfer full) (nullable): additional properties, which are
 *   passed to `pw_context_new` and `pw_context_connect`
 *
 * Returns: (transfer full): a new #WpCore
 */
WpCore *
wp_core_new (GMainContext *context, WpProperties * properties)
{
  g_autoptr (WpProperties) props = properties;
  return g_object_new (WP_TYPE_CORE,
      "context", context,
      "properties", properties,
      NULL);
}

/**
 * wp_core_get_context:
 * @self: the core
 *
 * Returns: (transfer none) (nullable): the #GMainContext that is in use by
 *   this core for events
 */
GMainContext *
wp_core_get_context (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->context;
}

/**
 * wp_core_get_pw_context:
 * @self: the core
 *
 * Returns: (transfer none): the internal `pw_context` object
 */
struct pw_context *
wp_core_get_pw_context (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->pw_context;
}

/**
 * wp_core_get_pw_core:
 * @self: the core
 *
 * Returns: (transfer none) (nullable): the internal `pw_core` object,
 *   or %NULL if the core is not connected to PipeWire
 */
struct pw_core *
wp_core_get_pw_core (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->pw_core;
}

/**
 * wp_core_connect:
 * @self: the core
 *
 * Connects this core to the PipeWire server. When connection succeeds,
 * the #WpCore::connected signal is emitted
 *
 * Returns: %TRUE if the core is effectively connected or %FALSE if
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

  /* Emit the connected signal */
  g_signal_emit (self, signals[SIGNAL_CONNECTED], 0);

  return TRUE;
}

/**
 * wp_core_disconnect:
 * @self: the core
 *
 * Disconnects this core from the PipeWire server. This also effectively
 * destroys all #WpProxy objects that were created through the registry,
 * destroys the `pw_core` and finally emits the #WpCore::disconnected signal.
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

/**
 * wp_core_is_connected:
 * @self: the core
 *
 * Returns: %TRUE if the core is connected to PipeWire, %FALSE otherwise
 */
gboolean
wp_core_is_connected (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), FALSE);
  return self->pw_core != NULL;
}

/**
 * wp_core_idle_add:
 * @self: the core
 * @source: (out) (optional): the source
 * @function: (scope notified): the function to call
 * @data: (closure): data to pass to @function
 * @destroy: (nullable): a function to destroy @data
 *
 * Adds an idle callback to be called in the same #GMainContext as the
 * one used by this core. This is essentially the same as g_idle_add_full(),
 * but it adds the created #GSource on the #GMainContext used by this core
 * instead of the default context.
 */
void
wp_core_idle_add (WpCore * self, GSource **source, GSourceFunc function,
    gpointer data, GDestroyNotify destroy)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CORE (self));

  s = g_idle_source_new ();
  g_source_set_callback (s, function, data, destroy);
  g_source_attach (s, self->context);

  if (source)
    *source = g_source_ref (s);
}

/**
 * wp_core_idle_add_closure: (rename-to wp_core_idle_add)
 * @self: the core
 * @source: (out) (optional): the source
 * @closure: the closure to invoke
 *
 * Adds an idle callback to be called in the same #GMainContext as the
 * one used by this core.
 *
 * This is the same as wp_core_idle_add(), but it allows you to specify
 * a #GClosure instead of a C callback.
 */
void
wp_core_idle_add_closure (WpCore * self, GSource **source, GClosure * closure)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (closure != NULL);

  s = g_idle_source_new ();
  g_source_set_closure (s, closure);
  g_source_attach (s, self->context);

  if (source)
    *source = g_source_ref (s);
}

/**
 * wp_core_timeout_add:
 * @self: the core
 * @source: (out) (optional): the source
 * @timeout_ms: the timeout in milliseconds
 * @function: (scope notified): the function to call
 * @data: (closure): data to pass to @function
 * @destroy: (nullable): a function to destroy @data
 *
 * Adds a timeout callback to be called at regular intervals in the same
 * #GMainContext as the one used by this core. The function is called repeatedly
 * until it returns FALSE, at which point the timeout is automatically destroyed
 * and the function will not be called again. The first call to the function
 * will be at the end of the first interval. This is essentially the same as
 * g_timeout_add_full(), but it adds the created #GSource on the #GMainContext
 * used by this core instead of the default context.
 */
void
wp_core_timeout_add (WpCore * self, GSource **source, guint timeout_ms,
    GSourceFunc function, gpointer data, GDestroyNotify destroy)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CORE (self));

  s = g_timeout_source_new (timeout_ms);
  g_source_set_callback (s, function, data, destroy);
  g_source_attach (s, self->context);

  if (source)
    *source = g_source_ref (s);
}

/**
 * wp_core_timeout_add_closure: (rename-to wp_core_timeout_add)
 * @self: the core
 * @source: (out) (optional): the source
 * @timeout_ms: the timeout in milliseconds
 * @closure: the closure to invoke
 *
 * Adds a timeout callback to be called at regular intervals in the same
 * #GMainContext as the one used by this core.
 *
 * This is the same as wp_core_timeout_add(), but it allows you to specify
 * a #GClosure instead of a C callback.
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
  g_source_attach (s, self->context);

  if (source)
    *source = g_source_ref (s);
}

/**
 * wp_core_sync:
 * @self: the core
 * @cancellable: (nullable): a #GCancellable to cancel the operation
 * @callback: (scope async): a function to call when the operation is done
 * @user_data: (closure): data to pass to @callback
 *
 * Asks the PipeWire server to call the @callback via an event.
 *
 * Since methods are handled in-order and events are delivered
 * in-order, this can be used as a barrier to ensure all previous
 * methods and the resulting events have been handled.
 *
 * In both success and error cases, @callback is always called. Use
 * wp_core_sync_finish() from within the @callback to determine whether
 * the operation completed successfully or if an error occurred.
 *
 * Returns: %TRUE if the sync operation was started, %FALSE if an error
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

/**
 * wp_core_sync_finish:
 * @self: the core
 * @res: a #GAsyncResult
 * @error: (out) (optional): the error that occurred, if any
 *
 * This function is meant to be called from within the callback of
 * wp_core_sync() in order to determine the success or failure of the operation.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE otherwise
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
