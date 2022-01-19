/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_EVENT_DISPATCHER_H__
#define __WIREPLUMBER_EVENT_DISPATCHER_H__

#include "properties.h"
#include "event-hook.h"
#include "core.h"

G_BEGIN_DECLS

/*!
 * \brief The WpEvent GType
 * \ingroup wpeventdispatcher
 */
#define WP_TYPE_EVENT (wp_event_get_type ())
WP_API
GType wp_event_get_type (void) G_GNUC_CONST;

typedef struct _WpEvent WpEvent;

WP_API
WpEvent * wp_event_new (const gchar * type, gint priority,
    WpProperties * properties, GObject * source, GObject * subject);

WP_API
WpEvent * wp_event_ref (WpEvent * self);

WP_API
void wp_event_unref (WpEvent * self);

WP_API
WpProperties * wp_event_get_properties (WpEvent * self);

WP_API
GObject * wp_event_get_source (WpEvent * self);

WP_API
GObject * wp_event_get_subject (WpEvent * self);

WP_API
void wp_event_stop_processing (WpEvent * self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (WpEvent, wp_event_unref)


/*!
 * \brief The WpEventDispatcher GType
 * \ingroup wpeventdispatcher
 */
#define WP_TYPE_EVENT_DISPATCHER (wp_event_dispatcher_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpEventDispatcher, wp_event_dispatcher,
                      WP, EVENT_DISPATCHER, GObject)

WP_API
WpEventDispatcher * wp_event_dispatcher_get_instance (WpCore * core);

WP_API
void wp_event_dispatcher_push_event (WpEventDispatcher * self, WpEvent * event);

WP_API
void wp_event_dispatcher_register_hook (WpEventDispatcher * self,
    WpEventHook * hook);

WP_API
void wp_event_dispatcher_unregister_hook (WpEventDispatcher * self,
    WpEventHook * hook);

G_END_DECLS

#endif
