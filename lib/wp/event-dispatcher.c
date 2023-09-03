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
  GPtrArray *hooks; /* registered hooks */
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
  self->hooks = g_ptr_array_new_with_free_func (g_object_unref);

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

  g_clear_pointer (&self->hooks, g_ptr_array_unref);
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
    wp_trace_object (self, "pushed event (%s)", wp_event_get_name (event));

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
  g_return_if_fail (WP_IS_EVENT_DISPATCHER (self));
  g_return_if_fail (WP_IS_EVENT_HOOK (hook));

  g_autoptr (WpEventDispatcher) already_registered_dispatcher =
      wp_event_hook_get_dispatcher (hook);
  g_return_if_fail (already_registered_dispatcher == NULL);

  wp_event_hook_set_dispatcher (hook, self);
  g_ptr_array_add (self->hooks, g_object_ref (hook));
}

/*!
 * \brief Unregisters an event hook
 * \ingroup wpeventdispacher
 *
 * \param self the event dispatcher
 * \param hook (transfer none): the hook to unregister
 */
void
wp_event_dispatcher_unregister_hook (WpEventDispatcher * self,
    WpEventHook * hook)
{
  g_return_if_fail (WP_IS_EVENT_DISPATCHER (self));
  g_return_if_fail (WP_IS_EVENT_HOOK (hook));

  g_autoptr (WpEventDispatcher) already_registered_dispatcher =
      wp_event_hook_get_dispatcher (hook);
  g_return_if_fail (already_registered_dispatcher == self);

  wp_event_hook_set_dispatcher (hook, NULL);
  g_ptr_array_remove_fast (self->hooks, hook);
}

/*!
 * \brief Returns an iterator to iterate over all the registered hooks
 * \ingroup wpeventdispatcher
 *
 * \param self the event dispatcher
 * \return (transfer full): a new iterator
 */
WpIterator *
wp_event_dispatcher_new_hooks_iterator (WpEventDispatcher * self)
{
  GPtrArray *items =
      g_ptr_array_copy (self->hooks, (GCopyFunc) g_object_ref, NULL);
  return wp_iterator_new_ptr_array (items, WP_TYPE_EVENT_HOOK);
}
