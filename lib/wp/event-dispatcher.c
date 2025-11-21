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

typedef struct _HookData HookData;
struct _HookData
{
  struct spa_list link;
  WpEventHook *hook;
  GPtrArray *dependencies;
};

static inline HookData *
hook_data_new (WpEventHook * hook)
{
  HookData *hook_data = g_new0 (HookData, 1);
  spa_list_init (&hook_data->link);
  hook_data->hook = g_object_ref (hook);
  hook_data->dependencies = g_ptr_array_new ();
  return hook_data;
}

static void
hook_data_free (HookData *self)
{
  g_clear_object (&self->hook);
  g_clear_pointer (&self->dependencies, g_ptr_array_unref);
  g_free (self);
}

static inline void
record_dependency (struct spa_list *list, const gchar *target,
    const gchar *dependency)
{
  HookData *hook_data;
  spa_list_for_each (hook_data, list, link) {
    if (g_pattern_match_simple (target, wp_event_hook_get_name (hook_data->hook))) {
      g_ptr_array_insert (hook_data->dependencies, -1, (gchar *) dependency);
      break;
    }
  }
}

static inline gboolean
hook_exists_in (const gchar *hook_name, struct spa_list *list)
{
  HookData *hook_data;
  if (!spa_list_is_empty (list)) {
    spa_list_for_each (hook_data, list, link) {
      if (g_pattern_match_simple (hook_name, wp_event_hook_get_name (hook_data->hook))) {
        return TRUE;
      }
    }
  }
  return FALSE;
}

static gboolean
sort_hooks (GPtrArray *hooks)
{
  struct spa_list collected, result, remaining;
  HookData *sorted_hook_data = NULL;

  spa_list_init (&collected);
  spa_list_init (&result);
  spa_list_init (&remaining);

  for (guint i = 0; i < hooks->len; i++) {
    WpEventHook *hook = g_ptr_array_index (hooks, i);
    HookData *hook_data = hook_data_new (hook);

    /* record "after" dependencies directly */
    const gchar * const * strv =
        wp_event_hook_get_runs_after_hooks (hook_data->hook);
    while (strv && *strv) {
      g_ptr_array_insert (hook_data->dependencies, -1, (gchar *) *strv);
      strv++;
    }

    spa_list_append (&collected, &hook_data->link);
  }

  if (!spa_list_is_empty (&collected)) {
    HookData *hook_data;

    /* convert "before" dependencies into "after" dependencies */
    spa_list_for_each (hook_data, &collected, link) {
      const gchar * const * strv =
          wp_event_hook_get_runs_before_hooks (hook_data->hook);
      while (strv && *strv) {
        /* record hook_data->hook as a dependency of the *strv hook */
        record_dependency (&collected, *strv,
            wp_event_hook_get_name (hook_data->hook));
        strv++;
      }
    }

    /* sort */
    while (!spa_list_is_empty (&collected)) {
      gboolean made_progress = FALSE;

      /* examine each hook to see if its dependencies are satisfied in the
         result list; if yes, then append it to the result too */
      spa_list_consume (hook_data, &collected, link) {
        guint deps_satisfied = 0;

        spa_list_remove (&hook_data->link);

        for (guint i = 0; i < hook_data->dependencies->len; i++) {
          const gchar *dep = g_ptr_array_index (hook_data->dependencies, i);
          /* if the dependency is already in the sorted result list or if
             it doesn't exist at all, we consider it satisfied */
          if (hook_exists_in (dep, &result) ||
              !(hook_exists_in (dep, &collected) ||
                hook_exists_in (dep, &remaining))) {
            deps_satisfied++;
          }
        }

        if (deps_satisfied == hook_data->dependencies->len) {
          spa_list_append (&result, &hook_data->link);
          made_progress = TRUE;
        } else {
          spa_list_append (&remaining, &hook_data->link);
        }
      }

      if (made_progress) {
        /* run again with the remaining hooks */
        spa_list_insert_list (&collected, &remaining);
        spa_list_init (&remaining);
      }
      else if (!spa_list_is_empty (&remaining)) {
        /* if we did not make any progress towards growing the result list,
           it means the dependencies cannot be satisfied because of circles */
        spa_list_consume (hook_data, &result, link) {
          spa_list_remove (&hook_data->link);
          hook_data_free (hook_data);
        }
        spa_list_consume (hook_data, &remaining, link) {
          spa_list_remove (&hook_data->link);
          hook_data_free (hook_data);
        }
        return FALSE;
      }
    }
  }

  /* clear hooks and add the sorted ones */
  g_ptr_array_set_size (hooks, 0);
  spa_list_consume (sorted_hook_data, &result, link) {
    spa_list_remove (&sorted_hook_data->link);
    g_ptr_array_add (hooks, g_object_ref (sorted_hook_data->hook));
    hook_data_free (sorted_hook_data);
  }

  return TRUE;
}

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
        if (!sort_hooks (hooks))
          goto sort_error;
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
        if (!sort_hooks (new_hooks))
          goto sort_error;
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
      if (!sort_hooks (defined_hooks))
        goto sort_error;
    }

    /* Add it to the undefined hooks */
    g_ptr_array_add (self->undefined_hooks, g_object_ref (hook));
    if (!sort_hooks (self->undefined_hooks))
      goto sort_error;
  }

  wp_info_object (self, "Registered hook %s successfully", hook_name);
  return;

sort_error:
  /* Unregister hook */
  wp_event_dispatcher_unregister_hook (self, hook);
  wp_warning_object (self,
      "Could not register hook %s because of circular dependencies", hook_name);
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
