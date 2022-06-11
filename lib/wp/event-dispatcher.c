/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-event-dispatcher"

#include "event-dispatcher.h"
#include "event-hook.h"
#include "private/registry.h"
#include "log.h"

#include <spa/support/plugin.h>
#include <spa/support/system.h>

struct _WpEvent
{
  grefcount ref;

  /* immutable fields */
  gint priority;
  WpProperties *properties;
  GObject *source;
  GObject *subject;
  GCancellable *cancellable;

  /* managed by the dispatcher */
  GList *hooks;
  WpEventHook *current_hook_in_async;
};

G_DEFINE_BOXED_TYPE (WpEvent, wp_event, wp_event_ref, wp_event_unref)

/*!
 * \brief Creates a new event
 *
 * \param type the type of the event
 * \param priority the priority of the event
 * \param properties (transfer full)(nullable): properties of the event
 * \param source (transfer none): the source of the event
 * \param subject (transfer none)(nullable): the object that the event is about
 * \return (transfer full): the newly constructed event
 */
WpEvent *
wp_event_new (const gchar * type, gint priority, WpProperties * properties,
    GObject * source, GObject * subject)
{
  WpEvent * self = g_slice_new0 (WpEvent);
  g_ref_count_init (&self->ref);

  self->priority = priority;
  self->properties = properties ?
      wp_properties_ensure_unique_owner (properties) :
      wp_properties_new_empty ();

  self->source = source ? g_object_ref (source) : NULL;
  self->subject = subject ? g_object_ref (subject) : NULL;
  self->cancellable = g_cancellable_new ();

  if (self->subject) {
    /* merge properties from subject */
    GParamSpec *pspec = g_object_class_find_property (
        G_OBJECT_GET_CLASS (self->subject), "properties");
    if (pspec && G_PARAM_SPEC_VALUE_TYPE (pspec) == WP_TYPE_PROPERTIES) {
      g_autoptr (WpProperties) subj_props = NULL;
      g_object_get (self->subject, "properties", &subj_props, NULL);
      if (subj_props) {
        wp_properties_update (self->properties, subj_props);
      }
    }

    /* watch for subject pw-proxy-destroyed and cancel event */
    if (g_type_is_a (G_OBJECT_TYPE (self->subject), WP_TYPE_PROXY)) {
      g_signal_connect_object (self->subject, "pw-proxy-destroyed",
          (GCallback) g_cancellable_cancel, self->cancellable,
          G_CONNECT_SWAPPED);
    }
  }

  wp_properties_set (self->properties, "event.type", type);

  return self;
}

static void
wp_event_free (WpEvent * self)
{
  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_object (&self->source);
  g_clear_object (&self->subject);
  g_clear_object (&self->cancellable);
}

WpEvent *
wp_event_ref (WpEvent * self)
{
  g_ref_count_inc (&self->ref);
  return self;
}

void
wp_event_unref (WpEvent * self)
{
  if (g_ref_count_dec (&self->ref))
    wp_event_free (self);
}

WpProperties *
wp_event_get_properties (WpEvent * self)
{
  g_return_val_if_fail(self != NULL, NULL);
  return wp_properties_ref (self->properties);
}

GObject *
wp_event_get_source (WpEvent * self)
{
  g_return_val_if_fail(self != NULL, NULL);
  return self->source ? g_object_ref (self->source) : NULL;
}

GObject *
wp_event_get_subject (WpEvent * self)
{
  g_return_val_if_fail(self != NULL, NULL);
  return self->subject ? g_object_ref (self->subject) : NULL;
}

void
wp_event_stop_processing (WpEvent * self)
{
  g_return_if_fail (self != NULL);
  g_cancellable_cancel (self->cancellable);
}



struct _WpEventDispatcher
{
  GObject parent;

  GWeakRef core;
  GPtrArray *hooks; /* registered hooks */
  GSource *source;  /* the event loop source */
  GList *events;    /* the events stack */
  WpEvent *rescan_event;
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
      !((WpEvent *) g_list_first (d->events)->data)->current_hook_in_async;
}

static void
on_event_hook_done (WpEventHook * hook, GAsyncResult * res, WpEvent * event)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (WpEventDispatcher) dispatcher =
      wp_event_hook_get_dispatcher (hook);

  g_assert (event->current_hook_in_async == hook);

  if (!wp_event_hook_finish (hook, res, &error) && error &&
      error->domain != G_IO_ERROR && error->code != G_IO_ERROR_CANCELLED)
    wp_message_object (hook, "failed: %s", error->message);

  g_clear_object (&event->current_hook_in_async);
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
    WpEvent *event = (WpEvent *) (levent->data);

    /* event hook is still in progress, we will continue later */
    if (event->current_hook_in_async)
      return G_SOURCE_CONTINUE;

    wp_trace_object (d, "dispatching event %s(%p) of type(%s) priority(%d)",
        wp_properties_get(event->properties, "event.type"),
        event,
        wp_properties_get(event->properties, "event.subject.type"),
        event->priority);

    /* remove the remaining hooks if the event was cancelled */
    if (g_cancellable_is_cancelled (event->cancellable) && event->hooks)
      g_list_free_full (g_steal_pointer (&event->hooks), g_object_unref);

    /* get the highest priority hook */
    GList *lhook = g_list_first (event->hooks);
    if (lhook) {
      event->current_hook_in_async = WP_EVENT_HOOK (lhook->data);
      event->hooks = g_list_delete_link (event->hooks, g_steal_pointer (&lhook));

      /* execute the hook, possibly async */
      wp_event_hook_run (event->current_hook_in_async, event,
          event->cancellable, (GAsyncReadyCallback) on_event_hook_done, event);
    } else
      wp_trace_object (d, "no hooks for this event");

    /* clear the event after all hooks are done */
    if (!event->hooks && !event->current_hook_in_async) {
      d->events = g_list_delete_link (d->events, g_steal_pointer (&levent));
      if (event == d->rescan_event)
        d->rescan_event = NULL;
      g_clear_pointer (&event, wp_event_unref);
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
clear_event (WpEvent * event)
{
  g_cancellable_cancel (event->cancellable);
  g_list_free_full (g_steal_pointer (&event->hooks), g_object_unref);
  wp_event_unref (event);
}

static void
wp_event_dispatcher_finalize (GObject * object)
{
  WpEventDispatcher *self = WP_EVENT_DISPATCHER (object);

  g_list_free_full (g_steal_pointer (&self->events),
      (GDestroyNotify) clear_event);

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
 * This method will also create the instance and register it with the core
 * if it had not been created before.
 *
 * \param core the core
 * \return (transfer full): the event dispatcher instance
 */
WpEventDispatcher *
wp_event_dispatcher_get_instance (WpCore * core)
{
  WpRegistry *registry = wp_core_get_registry (core);
  WpEventDispatcher *dispatcher = wp_registry_find_object (registry,
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
    wp_registry_register_object (registry, g_object_ref (dispatcher));
  }

  return dispatcher;
}

static gint
event_cmp_func (const WpEvent *a, const WpEvent *b)
{
  return b->priority - a->priority;
}

static gint
hook_cmp_func (const WpEventHook *a, const WpEventHook *b)
{
  return wp_event_hook_get_priority ((WpEventHook *)b) -
         wp_event_hook_get_priority ((WpEventHook *)a);
}

/*!
 * \brief Pushes a new event for dispatching
 *
 * \param self the dispatcher
 * \param event (transfer full): the new event
 */
void
wp_event_dispatcher_push_event (WpEventDispatcher * self, WpEvent * event)
{
  g_return_if_fail (WP_IS_EVENT_DISPATCHER (self));
  g_return_if_fail (event != NULL);

  wp_trace_object (self, "pushing event %s(%p) of type(%s) priority(%d)",
      wp_properties_get(event->properties, "event.type"),
      event,
      wp_properties_get(event->properties, "event.subject.type"),
      event->priority);

  /* schedule rescan */
  if (!self->rescan_event) {
    self->rescan_event = wp_event_new ("rescan", G_MININT16, NULL, NULL, NULL);
    self->events = g_list_insert_sorted(self->events, self->rescan_event,
        (GCompareFunc)event_cmp_func);
  }

  /* push the event on the stack */
  self->events = g_list_insert_sorted (self->events, event,
      (GCompareFunc) event_cmp_func);

  /* attach hooks that run for this event */
  for (guint i = 0; i < self->hooks->len; i++) {
    WpEventHook *hook = g_ptr_array_index (self->hooks, i);
    if (wp_event_hook_runs_for_event (hook, event)) {
      /* ON_EVENT hooks run at the dispatching of the event */
      if (wp_event_hook_get_exec_type (hook) == WP_EVENT_HOOK_EXEC_TYPE_ON_EVENT) {
        event->hooks = g_list_insert_sorted (event->hooks, g_object_ref (hook),
            (GCompareFunc) hook_cmp_func);
      }
      /* AFTER_EVENTS hooks run after all other events have been dispatched */
      else if (!g_list_find (self->rescan_event->hooks, hook)) {
        self->rescan_event->hooks = g_list_insert_sorted (
            self->rescan_event->hooks, g_object_ref (hook),
            (GCompareFunc) hook_cmp_func);
      }
    }
  }

  /* wakeup the GSource */
  spa_system_eventfd_write (self->system, self->eventfd, 1);
}

/*!
 * \brief Registers an event hook
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
