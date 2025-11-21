/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "event.h"
#include "event-dispatcher.h"
#include "event-hook.h"
#include "log.h"
#include "proxy.h"

#include <spa/utils/defs.h>
#include <spa/utils/list.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-event")

struct _WpEvent
{
  grefcount ref;
  GData *datalist;
  GPtrArray *hooks;

  /* immutable fields */
  gint priority;
  WpProperties *properties;
  GObject *source;
  GObject *subject;
  GCancellable *cancellable;
  gchar *name;
};

G_DEFINE_BOXED_TYPE (WpEvent, wp_event, wp_event_ref, wp_event_unref)

static gchar *
form_event_name (WpEvent *e)
{
  WpProperties *props = e->properties;
  const gchar *type = wp_properties_get (props, "event.type");
  const gchar *subject_type = wp_properties_get (props, "event.subject.type");
  const gchar *metadata_name = wp_properties_get (props, "metadata.name");
  const gchar *param = wp_properties_get (props, "event.subject.param-id");

  return g_strdup_printf ("<%p>%s%s%s%s%s%s%s", e, (type ? type : ""),
    ((type && subject_type) ? "@" : ""),
    (subject_type ? subject_type : ""),
    ((subject_type && metadata_name) ? "@" : ""),
    (metadata_name ? metadata_name : ""),
    ((param && subject_type) ? "@" : ""),
    (param ? param : "")
    );
}

/*!
 * \brief Creates a new event
 * \ingroup wpevent
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
  WpEvent * self = g_new0 (WpEvent, 1);
  g_ref_count_init (&self->ref);
  g_datalist_init (&self->datalist);
  self->hooks = g_ptr_array_new_with_free_func (g_object_unref);

  self->priority = priority;
  self->properties = properties ?
      wp_properties_ensure_unique_owner (properties) :
      wp_properties_new_empty ();

  self->source = source ? g_object_ref (source) : NULL;
  self->subject = subject ? g_object_ref (subject) : NULL;
  self->cancellable = g_cancellable_new ();

  if (self->subject) {
    /* merge properties from subject */
    /* PW properties */
    GParamSpec *pspec = g_object_class_find_property (
        G_OBJECT_GET_CLASS (self->subject), "properties");
    if (pspec && G_PARAM_SPEC_VALUE_TYPE (pspec) == WP_TYPE_PROPERTIES) {
      g_autoptr (WpProperties) subj_props = NULL;
      g_object_get (self->subject, "properties", &subj_props, NULL);
      if (subj_props) {
        wp_properties_update (self->properties, subj_props);
      }
    }

    /* global properties */
    pspec = g_object_class_find_property ( G_OBJECT_GET_CLASS (self->subject),
        "global-properties");
    if (pspec && G_PARAM_SPEC_VALUE_TYPE (pspec) == WP_TYPE_PROPERTIES) {
      g_autoptr (WpProperties) subj_props = NULL;
      g_object_get (self->subject, "global-properties", &subj_props, NULL);
      if (subj_props) {
        wp_properties_update (self->properties, subj_props);
      }
    }
  }

  wp_properties_set (self->properties, "event.type", type);
  self->name = form_event_name (self);

  wp_trace ("event(%s) created", self->name);
  return self;
}

/*!
 * \brief Gets the name of the event
 * \ingroup wpevent
 * \param self the event
 * \return the event name
 */
const gchar *
wp_event_get_name(WpEvent *self)
{
  g_return_val_if_fail(self != NULL, NULL);
  return self->name;
}

static void
wp_event_free (WpEvent * self)
{
  g_clear_pointer (&self->hooks, g_ptr_array_unref);
  g_datalist_clear (&self->datalist);
  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_object (&self->source);
  g_clear_object (&self->subject);
  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_free (self->name);
  g_free (self);
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

/*!
 * \brief Gets the priority of the event
 * \ingroup wpevent
 * \param self the event
 * \return the event priority
 */
gint
wp_event_get_priority (WpEvent * self)
{
  g_return_val_if_fail (self != NULL, 0);
  return self->priority;
}

/*!
 * \brief Gets the properties of the Event
 * \ingroup wpevent
 * \param self the handle
 * \return (transfer full): the properties of the event
 */
WpProperties *
wp_event_get_properties (WpEvent * self)
{
  g_return_val_if_fail(self != NULL, NULL);
  return wp_properties_ref (self->properties);
}

/*!
 * \brief Gets the Source Object of the Event
 * \ingroup wpevent
 * \param self the handle
 * \return (transfer full): the source of the event
 */
GObject *
wp_event_get_source (WpEvent * self)
{
  g_return_val_if_fail(self != NULL, NULL);
  return self->source ? g_object_ref (self->source) : NULL;
}

/*!
 * \brief Gets the Subject Object of the Event
 * \ingroup wpevent
 * \param self the handle
 * \return (transfer full): the subject of the event
 */
GObject *
wp_event_get_subject (WpEvent * self)
{
  g_return_val_if_fail(self != NULL, NULL);
  return self->subject ? g_object_ref (self->subject) : NULL;
}

/*!
 * \brief Returns the internal GCancellable that is used to track whether this
 *    event has been stopped by wp_event_stop_processing()
 * \ingroup wpevent
 * \param self the event
 * \return (transfer none): the cancellable
 */
GCancellable *
wp_event_get_cancellable (WpEvent * self)
{
  return self->cancellable;
}

/*!
 * \brief Stops processing of this event; any further hooks will not be executed
 *   from this moment onwards and the event will be discarded from the stack
 * \ingroup wpevent
 * \param self the event
 */
void
wp_event_stop_processing (WpEvent * self)
{
  g_return_if_fail (self != NULL);
  wp_debug ("stopping event(%s)", self->name);
  g_cancellable_cancel (self->cancellable);
}

static void
destroy_event_data (gpointer data)
{
  g_value_unset ((GValue *) data);
  g_free (data);
}

/*!
 * \brief Stores \a data on the event, associated with the specified \a key
 *
 * This can be used to exchange arbitrary data between hooks that run for
 * this event.
 *
 * \ingroup wpevent
 * \param self the event
 * \param key the key to associate \a data with
 * \param data (transfer none)(nullable): the data element, or \c NULL to
 *   remove any previous data associated with this \a key
 */
void
wp_event_set_data (WpEvent * self, const gchar * key, const GValue * data)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (key != NULL);
  GValue *data_copy = NULL;

  if (data && G_IS_VALUE (data)) {
    data_copy = g_new0 (GValue, 1);
    g_value_init (data_copy, G_VALUE_TYPE (data));
    g_value_copy (data, data_copy);
  }

  g_datalist_set_data_full (&self->datalist, key, data_copy,
      data_copy ? destroy_event_data : NULL);
}

/*!
 * \brief Gets the data that was previously associated with \a key by
 *    wp_event_set_data()
 * \ingroup wpevent
 * \param self the event
 * \param key the key
 * \return (transfer none)(nullable): the data associated with \a key or \c NULL
 */
const GValue *
wp_event_get_data (WpEvent * self, const gchar * key)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (key != NULL, NULL);

  return g_datalist_get_data (&self->datalist, key);
}

/*!
 * \brief Collects all the hooks registered in the \a dispatcher that run for
 *    this \a event
 * \ingroup wpevent
 * \param event the event
 * \param dispatcher the event dispatcher
 * \return TRUE if at least one hook has been collected,
 *    FALSE if no hooks run for this event or an error occurred
 */
gboolean
wp_event_collect_hooks (WpEvent * event, WpEventDispatcher * dispatcher)
{
  g_autoptr (WpIterator) all_hooks = NULL;
  g_auto (GValue) value = G_VALUE_INIT;
  const gchar *event_type = NULL;

  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (WP_IS_EVENT_DISPATCHER (dispatcher), FALSE);

  /* Clear all current hooks */
  g_ptr_array_set_size (event->hooks, 0);

  /* Get the event type */
  event_type = wp_properties_get (event->properties, "event.type");
  wp_debug_object (dispatcher, "Collecting hooks for event %s with type %s",
      event->name, event_type);

  /* Collect hooks that run for this event */
  all_hooks = wp_event_dispatcher_new_hooks_for_event_type_iterator (dispatcher,
      event_type);
  while (wp_iterator_next (all_hooks, &value)) {
    WpEventHook *hook = g_value_get_object (&value);
    if (wp_event_hook_runs_for_event (hook, event)) {
      g_ptr_array_add (event->hooks, g_object_ref (hook));
      wp_debug_boxed (WP_TYPE_EVENT, event, "added "WP_OBJECT_FORMAT"(%s)",
          WP_OBJECT_ARGS (hook), wp_event_hook_get_name (hook));
    }
    g_value_unset (&value);
  }

  return event->hooks->len > 0;
}

/*!
 * \brief Returns an iterator that iterates over all the hooks that were
 *    collected by wp_event_collect_hooks()
 * \ingroup wpevent
 * \param event the event
 * \return (transfer full): the new iterator
 */
WpIterator *
wp_event_new_hooks_iterator (WpEvent * event)
{
  GPtrArray *hooks;
  hooks = g_ptr_array_copy (event->hooks, (GCopyFunc) g_object_ref, NULL);
  return wp_iterator_new_ptr_array (hooks, WP_TYPE_EVENT_HOOK);

}
