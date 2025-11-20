/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "event-dispatcher.h"
#include "log.h"

#include <spa/support/plugin.h>
#include <spa/support/system.h>
#include <pipewire/pipewire.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-event-dispatcher")

typedef struct _EventData EventData;
struct _EventData
{
  WpEvent *event;
  WpIterator *hooks_iter;
  WpEventHook *current_hook_in_async;
  gint64 seq;
};

static inline EventData *
event_data_new (WpEvent * event)
{
  static gint64 seqn = 0;
  EventData *event_data = g_new0 (EventData, 1);
  event_data->event = wp_event_ref (event);
  event_data->hooks_iter = wp_event_new_hooks_iterator (event);
  event_data->seq = seqn++;
  return event_data;
}

static void
event_data_free (EventData * self)
{
  g_clear_pointer (&self->event, wp_event_unref);
  g_clear_pointer (&self->hooks_iter, wp_iterator_unref);
  g_clear_object (&self->current_hook_in_async);
  g_free (self);
}

struct _WpEventDispatcher
{
  GObject parent;

  GWeakRef core;
  GHashTable *defined_hooks;  /* registered hooks for defined events */
  GPtrArray *undefined_hooks;  /* registered hooks for undefined events */
  GSource *source;  /* the event loop source */
  GList *events;    /* the events stack */
  struct spa_system *system;
  int eventfd;
};

G_DEFINE_TYPE (WpEventDispatcher, wp_event_dispatcher, G_TYPE_OBJECT)

#define WP_EVENT_SOURCE_DISPATCHER(x) \
    WP_EVENT_DISPATCHER (((WpEventSource *) x)->dispatcher)

typedef struct _WpEventSource WpEventSource;
struct _WpEventSource
{
  GSource parent;
  WpEventDispatcher *dispatcher;
};

static gboolean
wp_event_source_check (GSource * s)
{
  WpEventDispatcher *d = WP_EVENT_SOURCE_DISPATCHER (s);
  return d && d->events &&
      !((EventData *) g_list_first (d->events)->data)->current_hook_in_async;
}

static void
on_event_hook_done (WpEventHook * hook, GAsyncResult * res, EventData * data)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_hook_get_dispatcher (hook);

  g_assert (data->current_hook_in_async == hook);

  if (!wp_event_hook_finish (hook, res, &error) && error &&
      error->domain != G_IO_ERROR && error->code != G_IO_ERROR_CANCELLED)
    wp_notice_object (hook, "failed: %s", error->message);

  g_clear_object (&data->current_hook_in_async);
  spa_system_eventfd_write (dispatcher->system, dispatcher->eventfd, 1);
}

static gboolean
wp_event_source_dispatch (GSource * s, GSourceFunc callback, gpointer user_data)
{
  WpEventDispatcher *d = WP_EVENT_SOURCE_DISPATCHER (s);
  uint64_t count;

  /* clear the eventfd */
  spa_system_eventfd_read (d->system, d->eventfd, &count);

  /* get the highest priority event */
  GList *levent = g_list_first (d->events);
  while (levent) {
    EventData *event_data = (EventData *) (levent->data);
    WpEvent *event = event_data->event;
    GCancellable *cancellable = wp_event_get_cancellable (event);
    g_auto (GValue) value = G_VALUE_INIT;
    gboolean has_next = FALSE;

    /* event hook is still in progress, we will continue later */
    if (event_data->current_hook_in_async)
      return G_SOURCE_CONTINUE;

    /* check if the event was cancelled */
    if (g_cancellable_is_cancelled (cancellable)) {
      wp_debug_object (d, "event(%p) cancelled remove it", event);
      has_next = FALSE;
    } else {
      /* get the highest priority hook */
      has_next = wp_iterator_next (event_data->hooks_iter, &value);
    }

    if (has_next) {
      WpEventHook *hook = g_value_get_object (&value);
      const gchar *name = wp_event_hook_get_name (hook);

      event_data->current_hook_in_async = g_object_ref (hook);

      wp_trace_object(d, "dispatching event (%s) running hook <%p>(%s)",
          wp_event_get_name(event), hook, name);

      /* execute the hook, possibly async */
      wp_event_hook_run (hook, event, cancellable,
          (GAsyncReadyCallback) on_event_hook_done, event_data);
    } else {
      /* clear the event after all hooks are done */
      d->events = g_list_delete_link (d->events, g_steal_pointer (&levent));
      g_clear_pointer (&event_data, event_data_free);
    }

    /* get the next event */
    levent = g_list_first (d->events);
  }

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs source_funcs = {
  NULL,
  wp_event_source_check,
  wp_event_source_dispatch,
  NULL
};

static void
wp_event_dispatcher_init (WpEventDispatcher * self)
{
  g_weak_ref_init (&self->core, NULL);
  self->defined_hooks = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify)g_ptr_array_unref);
  self->undefined_hooks = g_ptr_array_new_with_free_func (g_object_unref);

  self->source = g_source_new (&source_funcs, sizeof (WpEventSource));
  ((WpEventSource *) self->source)->dispatcher = self;

  /* this is higher than normal "idle" operations but lower than the default
     priority, which is used for events from the PipeWire socket and timers */
  g_source_set_priority (self->source, G_PRIORITY_HIGH_IDLE);
}

static void
wp_event_dispatcher_finalize (GObject * object)
{
  WpEventDispatcher *self = WP_EVENT_DISPATCHER (object);

  g_list_free_full (g_steal_pointer (&self->events),
      (GDestroyNotify) event_data_free);

  ((WpEventSource *) self->source)->dispatcher = NULL;
  g_source_destroy (self->source);
  g_clear_pointer (&self->source, g_source_unref);

  close (self->eventfd);

  g_clear_pointer (&self->defined_hooks, g_hash_table_unref);
  g_clear_pointer (&self->undefined_hooks, g_ptr_array_unref);
  g_weak_ref_clear (&self->core);

  G_OBJECT_CLASS (wp_event_dispatcher_parent_class)->finalize (object);
}

static void
wp_event_dispatcher_class_init (WpEventDispatcherClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_event_dispatcher_finalize;
}

/*!
 * \brief Returns the event dispatcher instance that is associated with the
 * given core.
 *
 * This method will also create the instance and register it with the core,
 * if it had not been created before.
 *
 * \ingroup wpeventdispatcher
 * \param core the core
 * \return (transfer full): the event dispatcher instance
 */
WpEventDispatcher *
wp_event_dispatcher_get_instance (WpCore * core)
{
  WpEventDispatcher *dispatcher = wp_core_find_object (core,
      (GEqualFunc) WP_IS_EVENT_DISPATCHER, NULL);

  if (G_UNLIKELY (!dispatcher)) {
    dispatcher = g_object_new (WP_TYPE_EVENT_DISPATCHER, NULL);
    g_weak_ref_set (&dispatcher->core, core);

    struct pw_context *context = wp_core_get_pw_context (core);
    uint32_t n_support;
    const struct spa_support *support =
        pw_context_get_support (context, &n_support);
    dispatcher->system =
        spa_support_find (support, n_support, SPA_TYPE_INTERFACE_System);

    dispatcher->eventfd = spa_system_eventfd_create (dispatcher->system, 0);
    g_source_add_unix_fd (dispatcher->source, dispatcher->eventfd, G_IO_IN);

    g_source_attach (dispatcher->source, wp_core_get_g_main_context (core));
    wp_core_register_object (core, g_object_ref (dispatcher));

    wp_info_object (dispatcher, "event-dispatcher inited");
  }

  return dispatcher;
}

static gint
event_cmp_func (const EventData *a, const EventData *b)
{
  gint c = wp_event_get_priority (b->event) - wp_event_get_priority (a->event);
  return (c != 0) ? c : (gint)(a->seq - b->seq);
}

/*!
 * \brief Pushes a new event onto the event stack for dispatching only if there
 * are any hooks are available for it.
 * \ingroup wpeventdispatcher
 *
 * \param self the dispatcher
 * \param event (transfer full): the new event
 */
void
wp_event_dispatcher_push_event (WpEventDispatcher * self, WpEvent * event)
{
  g_return_if_fail (WP_IS_EVENT_DISPATCHER (self));
  g_return_if_fail (event != NULL);

  if (wp_event_collect_hooks (event, self)) {
    EventData *event_data = event_data_new (event);

    self->events = g_list_insert_sorted (self->events, event_data,
        (GCompareFunc) event_cmp_func);
    wp_debug_object (self, "pushed event (%s)", wp_event_get_name (event));

    /* wakeup the GSource */
    spa_system_eventfd_write (self->system, self->eventfd, 1);
  }

  wp_event_unref (event);
}

/*!
 * \brief Registers an event hook
 * \ingroup wpeventdispatcher
 *
 * \param self the event dispatcher
 * \param hook (transfer none): the hook to register
 */
void
wp_event_dispatcher_register_hook (WpEventDispatcher * self,
    WpEventHook * hook)
{
  g_autoptr (GPtrArray) event_types = NULL;
  gboolean is_defined = FALSE;
  const gchar *hook_name;

  g_return_if_fail (WP_IS_EVENT_DISPATCHER (self));
  g_return_if_fail (WP_IS_EVENT_HOOK (hook));

  g_autoptr (WpEventDispatcher) already_registered_dispatcher =
      wp_event_hook_get_dispatcher (hook);
  g_return_if_fail (already_registered_dispatcher == NULL);

  wp_event_hook_set_dispatcher (hook, self);

  /* Register the event hook in the defined hooks table if it is defined */
  hook_name = wp_event_hook_get_name (hook);
  event_types = wp_event_hook_get_matching_event_types (hook);
  if (event_types) {
    for (guint i = 0; i < event_types->len; i++) {
      const gchar *event_type = g_ptr_array_index (event_types, i);
      GPtrArray *hooks;

      wp_debug_object (self, "Registering hook %s for defined event type %s",
          hook_name, event_type);

      /* Check if the event type was registered in the hash table */
      hooks = g_hash_table_lookup (self->defined_hooks, event_type);
      if (hooks) {
        g_ptr_array_add (hooks, g_object_ref (hook));
      } else {
        GPtrArray *new_hooks = g_ptr_array_new_with_free_func (g_object_unref);
        /* Add undefined hooks */
        for (guint i = 0; i < self->undefined_hooks->len; i++) {
          WpEventHook *uh = g_ptr_array_index (self->undefined_hooks, i);
          g_ptr_array_add (new_hooks, g_object_ref (uh));
        }
        /* Add current hook */
        g_ptr_array_add (new_hooks, g_object_ref (hook));
        g_hash_table_insert (self->defined_hooks, g_strdup (event_type),
            new_hooks);
      }

      is_defined = TRUE;
    }
  }

  /* Otherwise just register it as undefined hook */
  if (!is_defined) {
    GHashTableIter iter;
    gpointer value;

    wp_debug_object (self, "Registering hook %s for undefined event types",
          hook_name);

    /* Add it to the defined hooks table */
    g_hash_table_iter_init (&iter, self->defined_hooks);
    while (g_hash_table_iter_next (&iter, NULL, &value)) {
      GPtrArray *defined_hooks = value;
      g_ptr_array_add (defined_hooks, g_object_ref (hook));
    }

    /* Add it to the undefined hooks */
    g_ptr_array_add (self->undefined_hooks, g_object_ref (hook));
  }
}

/*!
 * \brief Unregisters an event hook
 * \ingroup wpeventdispatcher
 *
 * \param self the event dispatcher
 * \param hook (transfer none): the hook to unregister
 */
void
wp_event_dispatcher_unregister_hook (WpEventDispatcher * self,
    WpEventHook * hook)
{
  GHashTableIter iter;
  gpointer value;

  g_return_if_fail (WP_IS_EVENT_DISPATCHER (self));
  g_return_if_fail (WP_IS_EVENT_HOOK (hook));

  g_autoptr (WpEventDispatcher) already_registered_dispatcher =
      wp_event_hook_get_dispatcher (hook);
  g_return_if_fail (already_registered_dispatcher == self);

  wp_event_hook_set_dispatcher (hook, NULL);

  /* Remove hook from defined table and undefined list */
  g_hash_table_iter_init (&iter, self->defined_hooks);
  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    GPtrArray *defined_hooks = value;
    g_ptr_array_remove (defined_hooks, hook);
  }
  g_ptr_array_remove (self->undefined_hooks, hook);
}

static void
add_unique (GPtrArray *array, WpEventHook * hook)
{
  for (guint i = 0; i < array->len; i++)
    if (g_ptr_array_index (array, i) == hook)
      return;
  g_ptr_array_add (array, g_object_ref (hook));
}

/*!
 * \brief Returns an iterator to iterate over all the registered hooks
 * \deprecated Use \ref wp_event_dispatcher_new_hooks_for_event_type_iterator
 * instead.
 * \ingroup wpeventdispatcher
 *
 * \param self the event dispatcher
 * \return (transfer full): a new iterator
 */
WpIterator *
wp_event_dispatcher_new_hooks_iterator (WpEventDispatcher * self)
{
  GPtrArray *items = g_ptr_array_new_with_free_func (g_object_unref);
  GHashTableIter iter;
  gpointer value;

  /* Add all defined hooks */
  g_hash_table_iter_init (&iter, self->defined_hooks);
  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    GPtrArray *hooks = value;
    for (guint i = 0; i < hooks->len; i++) {
      WpEventHook *hook = g_ptr_array_index (hooks, i);
      add_unique (items, hook);
    }
  }

  /* Add all undefined hooks */
  for (guint i = 0; i < self->undefined_hooks->len; i++) {
    WpEventHook *hook = g_ptr_array_index (self->undefined_hooks, i);
    add_unique (items, hook);
  }

  return wp_iterator_new_ptr_array (items, WP_TYPE_EVENT_HOOK);
}

/*!
 * \brief Returns an iterator to iterate over the registered hooks for a
 * particular event type.
 * \ingroup wpeventdispatcher
 *
 * \param self the event dispatcher
 * \param event_type the event type
 * \return (transfer full): a new iterator
 * \since 0.5.13
 */
WpIterator *
wp_event_dispatcher_new_hooks_for_event_type_iterator (
    WpEventDispatcher * self, const gchar *event_type)
{
  GPtrArray *items;
  GPtrArray *hooks;

  hooks = g_hash_table_lookup (self->defined_hooks, event_type);
  if (hooks) {
    wp_debug_object (self, "Using %d defined hooks for event type %s",
        hooks->len, event_type);
  } else {
    hooks = self->undefined_hooks;
    wp_debug_object (self, "Using %d undefined hooks for event type %s",
        hooks->len, event_type);
  }

  items = g_ptr_array_copy (hooks, (GCopyFunc) g_object_ref, NULL);
  return wp_iterator_new_ptr_array (items, WP_TYPE_EVENT_HOOK);
}
