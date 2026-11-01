/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "core.h"
#include "private/connection.h"
#include "error.h"
#include "log.h"

#include <pipewire/pipewire.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-connection")

/*! \defgroup wpconnection WpConnection */
/*!
 * \struct WpConnection
 *
 * The connection wraps a `pw_core`, which links a WpContext to the
 * PipeWire server. It is owned by the context. The one of the WpMainContext is
 * available with wp_main_context_get_connection(), and the WpCore that uses that
 * context connects it through WP_CORE_FEATURE_CONNECTED.
 *
 * \gproperties
 *
 * \gproperty{core, WpCore *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The core that uses this connection}
 *
 * \gproperty{context, WpContext *,
 *   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The context that this connection connects to PipeWire}
 *
 * \gsignals
 *
 * \par connected
 * \parblock
 * \code
 * void
 * connected_callback (WpConnection * self,
 *                     gpointer user_data)
 * \endcode
 * Emitted when the connection to the PipeWire server has finished, once the
 * server has sent its core info.
 * \endparblock
 *
 * \par disconnected
 * \parblock
 * \code
 * void
 * disconnected_callback (WpConnection * self,
 *                        gpointer user_data)
 * \endcode
 * Emitted when the PipeWire server has closed the connection. This is not
 * emitted when the connection is closed by deactivating
 * WP_CORE_FEATURE_CONNECTED on the WpCore. The core does not clean up by itself;
 * users must deactivate WP_CORE_FEATURE_CONNECTED when this happens. This is
 * emitted from a PipeWire event, so the core must not be deactivated from within
 * the callback, but later, on an idle callback or after quitting the main loop.
 * An activation of the WpCore that is in progress fails, and new ones fail until
 * the core is deactivated.
 * \endparblock
 */
struct _WpConnection
{
  GObject parent;

  /* Props */
  GWeakRef core;
  GWeakRef context;

  /* Data */
  WpProperties *properties;
  struct pw_core *pw_core;
  struct pw_core_info *info;
  gint last_seq;
  gboolean connection_lost;
  struct spa_hook core_listener;
  struct spa_hook proxy_core_listener;
  GHashTable *async_tasks; // <int seq, GTask*>
};

enum {
  CONNECTION_PROP_0,
  CONNECTION_PROP_CORE,
  CONNECTION_PROP_CONTEXT,
  N_CONNECTION_PROPS,
};

enum {
  CONNECTION_SIGNAL_CONNECTED,
  CONNECTION_SIGNAL_DISCONNECTED,
  NUM_CONNECTION_SIGNALS
};

static guint32 connection_signals[NUM_CONNECTION_SIGNALS];
static GParamSpec *connection_properties[N_CONNECTION_PROPS] = { NULL, };

G_DEFINE_TYPE (WpConnection, wp_connection, G_TYPE_OBJECT)

static gboolean
wp_connection_is_connected (WpConnection * self)
{
  return self->pw_core && self->info && !self->connection_lost;
}

static void
core_info (void *data, const struct pw_core_info * info)
{
  WpConnection *self = WP_CONNECTION (data);
  gboolean new_connection = (self->info == NULL);

  self->info = pw_core_info_update (self->info, info);

  wp_info_object (self, "connected to server: %s, cookie: %u",
      self->info->name, self->info->cookie);

  if (new_connection)
    g_signal_emit (self, connection_signals[CONNECTION_SIGNAL_CONNECTED], 0);
}

static void
core_done (void *data, uint32_t id, int seq)
{
  WpConnection *self = WP_CONNECTION (data);
  g_autoptr (GTask) task = NULL;

  g_hash_table_steal_extended (self->async_tasks, GINT_TO_POINTER (seq), NULL,
      (gpointer *) &task);
  wp_debug_object (self, "done, seq 0x%x, task " WP_OBJECT_FORMAT,
      seq, WP_OBJECT_ARGS (task));

  if (task)
    g_task_return_int (task, seq);
}

static void
core_error (void *data, uint32_t id, int seq, int res, const char *message)
{
  WpConnection *self = WP_CONNECTION (data);

  /* protocol socket disconnected */
  if (id == 0 && res == -EPIPE) {
    self->connection_lost = TRUE;
    g_signal_emit (self, connection_signals[CONNECTION_SIGNAL_DISCONNECTED], 0);
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
  WpConnection *self = WP_CONNECTION (data);
  g_hash_table_foreach_remove (self->async_tasks, async_tasks_finish, NULL);
  g_clear_pointer (&self->info, pw_core_info_free);
  spa_hook_remove(&self->core_listener);
  spa_hook_remove(&self->proxy_core_listener);
  self->pw_core = NULL;
  self->connection_lost = FALSE;
  self->last_seq = 0;
}

static const struct pw_proxy_events proxy_core_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_core_destroy,
};

static void
wp_connection_init (WpConnection * self)
{
  g_weak_ref_init (&self->core, NULL);
  g_weak_ref_init (&self->context, NULL);
  self->async_tasks = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
}

static void
wp_connection_dispose (GObject * obj)
{
  wp_connection_disconnect (WP_CONNECTION (obj));

  G_OBJECT_CLASS (wp_connection_parent_class)->dispose (obj);
}

static void
wp_connection_finalize (GObject * obj)
{
  WpConnection *self = WP_CONNECTION (obj);

  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_pointer (&self->async_tasks, g_hash_table_unref);
  g_weak_ref_clear (&self->core);
  g_weak_ref_clear (&self->context);

  G_OBJECT_CLASS (wp_connection_parent_class)->finalize (obj);
}

static void
wp_connection_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpConnection *self = WP_CONNECTION (object);

  switch (property_id) {
  case CONNECTION_PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&self->core));
    break;
  case CONNECTION_PROP_CONTEXT:
    g_value_take_object (value, g_weak_ref_get (&self->context));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_connection_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpConnection *self = WP_CONNECTION (object);

  switch (property_id) {
  case CONNECTION_PROP_CORE:
    g_weak_ref_set (&self->core, g_value_get_object (value));
    break;
  case CONNECTION_PROP_CONTEXT:
    g_weak_ref_set (&self->context, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_connection_class_init (WpConnectionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->dispose = wp_connection_dispose;
  object_class->finalize = wp_connection_finalize;
  object_class->get_property = wp_connection_get_property;
  object_class->set_property = wp_connection_set_property;

  connection_properties[CONNECTION_PROP_CORE] = g_param_spec_object ("core", "core",
      "The core that uses this connection", WP_TYPE_CORE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  connection_properties[CONNECTION_PROP_CONTEXT] = g_param_spec_object ("context", "context",
      "The context that this connection connects to PipeWire", WP_TYPE_CONTEXT,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_CONNECTION_PROPS, connection_properties);

  connection_signals[CONNECTION_SIGNAL_CONNECTED] = g_signal_new ("connected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);

  connection_signals[CONNECTION_SIGNAL_DISCONNECTED] = g_signal_new ("disconnected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
}

WpConnection *
wp_connection_new (WpCore * core, WpContext * context)
{
  return g_object_new (WP_TYPE_CONNECTION,
      "core", core,
      "context", context,
      NULL);
}

gboolean
wp_connection_connect (WpConnection * self, gint socket_fd)
{
  g_autoptr (WpContext) context = NULL;
  struct pw_context *pw_context;
  struct pw_properties *p = NULL;

  g_return_val_if_fail (WP_IS_CONNECTION (self), FALSE);

  /* the old connection stays around until the connection is disconnected */
  if (self->connection_lost)
    return FALSE;

  /* Don't do anything if core is already connected */
  if (self->pw_core)
    return TRUE;

  context = g_weak_ref_get (&self->context);
  g_return_val_if_fail (context, FALSE);
  pw_context = wp_context_get_pw_context (context);
  g_return_val_if_fail (pw_context, FALSE);

  wp_info_object (self, "connecting to pipewire...");

  /* Connect */
  p = self->properties ? wp_properties_to_pw_properties (self->properties) : NULL;
  if (socket_fd == -1) {
    self->pw_core = pw_context_connect (pw_context, p, 0);
  } else {
    self->pw_core = pw_context_connect_fd (pw_context, socket_fd, p, 0);
  }

  if (!self->pw_core)
    return FALSE;

  /* Add the core listeners */
  pw_core_add_listener (self->pw_core, &self->core_listener, &core_events, self);
  pw_proxy_add_listener((struct pw_proxy*)self->pw_core,
      &self->proxy_core_listener, &proxy_core_events, self);

  return TRUE;
}

void
wp_connection_disconnect (WpConnection * self)
{
  g_return_if_fail (WP_IS_CONNECTION (self));

  if (self->pw_core)
    pw_core_disconnect (self->pw_core);
}

/*!
 * \brief Gets the core that uses the connection
 *
 * \ingroup wpconnection
 * \param self the connection
 * \returns (transfer full) (nullable): the core that uses \a self, or NULL if
 *   that core is gone
 */
WpCore *
wp_connection_get_core (WpConnection * self)
{
  g_return_val_if_fail (WP_IS_CONNECTION (self), NULL);
  return g_weak_ref_get (&self->core);
}

/*!
 * \brief Gets the internal PipeWire core of the connection
 *
 * \ingroup wpconnection
 * \param self the connection
 * \returns (transfer none) (nullable): the internal pw_core object,
 *   or NULL if the connection is not connected to PipeWire
 */
struct pw_core *
wp_connection_get_pw_core (WpConnection * self)
{
  g_return_val_if_fail (WP_IS_CONNECTION (self), NULL);
  return self->pw_core;
}

/*!
 * \brief Gets the bound id of the client object that is created as a result
 * of this connection being connected to the PipeWire daemon
 *
 * \ingroup wpconnection
 * \param self the connection
 * \returns the bound id of this client
 */
guint32
wp_connection_get_own_bound_id (WpConnection * self)
{
  struct pw_client *client;

  g_return_val_if_fail (wp_connection_is_connected (self), SPA_ID_INVALID);

  client = pw_core_get_client (self->pw_core);
  return pw_proxy_get_bound_id ((struct pw_proxy *) client);
}

/*!
 * \brief Gets the cookie of the connection's PipeWire instance
 *
 * \ingroup wpconnection
 * \param self the connection
 * \returns The cookie of the PipeWire instance that \a self is connected to.
 *     The cookie is a unique random number for identifying an instance of
 *     PipeWire
 */
guint32
wp_connection_get_remote_cookie (WpConnection * self)
{
  g_return_val_if_fail (wp_connection_is_connected (self), 0);

  return self->info->cookie;
}

/*!
 * \brief Gets the name of the connection's PipeWire instance
 * \ingroup wpconnection
 * \param self the connection
 * \returns The name of the PipeWire instance that \a self is connected to
 */
const gchar *
wp_connection_get_remote_name (WpConnection * self)
{
  g_return_val_if_fail (wp_connection_is_connected (self), NULL);

  return self->info->name;
}

/*!
 * \brief Gets the user name of the connection's PipeWire instance
 * \ingroup wpconnection
 * \param self the connection
 * \returns The name of the user that started the PipeWire instance that
 *     \a self is connected to
 */
const gchar *
wp_connection_get_remote_user_name (WpConnection * self)
{
  g_return_val_if_fail (wp_connection_is_connected (self), NULL);

  return self->info->user_name;
}

/*!
 * \brief Gets the host name of the connection's PipeWire instance
 * \ingroup wpconnection
 * \param self the connection
 * \returns The name of the host where the PipeWire instance that
 *     \a self is connected to is running on
 */
const gchar *
wp_connection_get_remote_host_name (WpConnection * self)
{
  g_return_val_if_fail (wp_connection_is_connected (self), NULL);

  return self->info->host_name;
}

/*!
 * \brief Gets the version of the connection's PipeWire instance
 * \ingroup wpconnection
 * \param self the connection
 * \returns The version of the PipeWire instance that \a self is connected to
 */
const gchar *
wp_connection_get_remote_version (WpConnection * self)
{
  g_return_val_if_fail (wp_connection_is_connected (self), NULL);

  return self->info->version;
}

/*!
 * \brief Gets the properties of the connection's PipeWire instance
 * \ingroup wpconnection
 * \param self the connection
 * \returns (transfer full): the properties of the PipeWire instance that
 *     \a self is connected to
 */
WpProperties *
wp_connection_get_remote_properties (WpConnection * self)
{
  g_return_val_if_fail (wp_connection_is_connected (self), NULL);

  return wp_properties_new_wrap_dict (self->info->props);
}

/*!
 * \brief Gets the properties of the connection
 * \ingroup wpconnection
 * \param self the connection
 * \returns (transfer full): the properties of \a self
 */
WpProperties *
wp_connection_get_properties (WpConnection * self)
{
  g_autoptr (WpContext) context = NULL;

  g_return_val_if_fail (WP_IS_CONNECTION (self), NULL);

  /* pw_core has all the properties of the pw_context,
     plus our updates, passed in pw_context_connect() */
  if (self->pw_core)
    return wp_properties_new_wrap (pw_core_get_properties (self->pw_core));

  context = g_weak_ref_get (&self->context);
  g_return_val_if_fail (context, NULL);

  /* if there is no connection yet, return the properties of the context */
  if (!self->properties)
    return wp_context_get_properties (context);

  /* ... plus any further updates that we got from
     wp_connection_update_properties() */
  else {
    /* we need to copy here in order to augment with the updates */
    g_autoptr (WpProperties) props =
        wp_context_get_properties (context);
    WpProperties *ret = wp_properties_copy (props);
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
 * \ingroup wpconnection
 * \param self the connection
 * \param updates (transfer full): updates to apply to the properties of
 *    \a self; this does not need to include properties that have not changed
 */
void
wp_connection_update_properties (WpConnection * self, WpProperties * updates)
{
  g_autoptr (WpProperties) upd = updates;

  g_return_if_fail (WP_IS_CONNECTION (self));
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
 * Use wp_connection_sync_finish() from within the \a callback to determine
 * whether the operation completed successfully or if an error occurred.
 *
 * \ingroup wpconnection
 * \param self the connection
 * \param cancellable (nullable): a GCancellable to cancel the operation
 * \param callback (scope async)(closure user_data): a function to call when the operation is done
 * \param user_data data to pass to \a callback
 * \returns the sequence number of the sync operation, or a negative errno
 *   if an error occurred before returning from this function
 */
gint
wp_connection_sync (WpConnection * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  return wp_connection_sync_closure (self, cancellable,
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
 * Use wp_connection_sync_finish() from within the \a closure to determine
 * whether the operation completed successfully or if an error occurred.
 *
 * \ingroup wpconnection
 * \param self the connection
 * \param cancellable (nullable): a GCancellable to cancel the operation
 * \param closure (transfer floating): a closure to invoke when the operation
 *    is done
 * \returns the sequence number of the sync operation, or a negative errno
 *   if an error occurred before returning from this function
 */
gint
wp_connection_sync_closure (WpConnection * self, GCancellable * cancellable,
    GClosure * closure)
{
  g_autoptr (GTask) task = NULL;
  int seq;

  g_return_val_if_fail (WP_IS_CONNECTION (self), -EINVAL);
  g_return_val_if_fail (closure, -EINVAL);

  closure = g_closure_ref (closure);
  g_closure_sink (closure);
  if (G_CLOSURE_NEEDS_MARSHAL (closure))
    g_closure_set_marshal (closure, g_cclosure_marshal_VOID__OBJECT);

  task = g_task_new (self, cancellable, invoke_closure, closure);

  if (G_UNLIKELY (!self->pw_core)) {
    g_warn_if_reached ();
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT, "No pipewire core");
    return -ENOTCONN;
  }

  seq = pw_core_sync (self->pw_core, 0, self->last_seq);
  if (G_UNLIKELY (seq < 0)) {
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED, "pw_core_sync failed: %s",
        g_strerror (-seq));
    return seq;
  }

  self->last_seq = seq;

  wp_debug_object (self, "sync, seq 0x%x, task " WP_OBJECT_FORMAT,
      seq, WP_OBJECT_ARGS (task));

  g_hash_table_insert (self->async_tasks, GINT_TO_POINTER (seq),
      g_steal_pointer (&task));
  return seq;
}

/*!
 * \brief This function is meant to be called from within the callback of
 * wp_connection_sync() in order to determine the success or failure of the
 * operation.
 *
 * \ingroup wpconnection
 * \param self the connection
 * \param res a GAsyncResult
 * \param error (out) (optional): the error that occurred, if any
 * \returns the sequence number of the sync operation that finished, as it was
 *   returned by wp_connection_sync(), or a negative value if an error occurred
 */
gint
wp_connection_sync_finish (WpConnection * self, GAsyncResult * res,
    GError ** error)
{
  g_return_val_if_fail (WP_IS_CONNECTION (self), -EINVAL);
  g_return_val_if_fail (g_task_is_valid (res, self), -EINVAL);

  return g_task_propagate_int (G_TASK (res), error);
}
