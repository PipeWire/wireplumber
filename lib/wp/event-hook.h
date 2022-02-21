/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_EVENT_HOOK_H__
#define __WIREPLUMBER_EVENT_HOOK_H__

#include "properties.h"
#include "object-interest.h"

G_BEGIN_DECLS

typedef struct _WpEvent WpEvent;
typedef struct _WpEventDispatcher WpEventDispatcher;

typedef enum {
  WP_EVENT_HOOK_EXEC_TYPE_ON_EVENT,
  WP_EVENT_HOOK_EXEC_TYPE_AFTER_EVENTS,
} WpEventHookExecType;

/*!
 * \brief The WpEventHook GType
 * \ingroup wpeventhook
 */
#define WP_TYPE_EVENT_HOOK (wp_event_hook_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpEventHook, wp_event_hook, WP, EVENT_HOOK, GObject)

struct _WpEventHookClass
{
  GObjectClass parent_class;

  gboolean (*runs_for_event) (WpEventHook * self, WpEvent * event);

  void (*run) (WpEventHook * self, WpEvent * event, GCancellable * cancellable,
      GAsyncReadyCallback callback, gpointer callback_data);

  gboolean (*finish) (WpEventHook * self, GAsyncResult * res, GError ** error);

  /*< private >*/
  WP_PADDING(5)
};

WP_API
gint wp_event_hook_get_priority (WpEventHook * self);

WP_API
WpEventHookExecType wp_event_hook_get_exec_type (WpEventHook * self);

WP_API
WpEventDispatcher * wp_event_hook_get_dispatcher (WpEventHook * self);

WP_PRIVATE_API
void wp_event_hook_set_dispatcher (WpEventHook * self,
    WpEventDispatcher * dispatcher);

WP_API
gboolean wp_event_hook_runs_for_event (WpEventHook * self, WpEvent * event);

WP_API
void wp_event_hook_run (WpEventHook * self,
    WpEvent * event, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer callback_data);

WP_API
gboolean wp_event_hook_finish (WpEventHook * self, GAsyncResult * res,
    GError ** error);


/*!
 * \brief The WpInterestEventHook GType
 * \ingroup wpeventhook
 */
#define WP_TYPE_INTEREST_EVENT_HOOK (wp_interest_event_hook_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpInterestEventHook, wp_interest_event_hook,
                          WP, INTEREST_EVENT_HOOK, WpEventHook)

struct _WpInterestEventHookClass
{
  WpEventHookClass parent_class;

  /*< private >*/
  WP_PADDING(4)
};

WP_API
void wp_interest_event_hook_add_interest (WpInterestEventHook * self,
    ...) G_GNUC_NULL_TERMINATED;

WP_API
void wp_interest_event_hook_add_interest_full (WpInterestEventHook * self,
    WpObjectInterest * interest);


/*!
 * \brief The WpSimpleEventHook GType
 * \ingroup wpeventhook
 */
#define WP_TYPE_SIMPLE_EVENT_HOOK (wp_simple_event_hook_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpSimpleEventHook, wp_simple_event_hook,
                      WP, SIMPLE_EVENT_HOOK, WpInterestEventHook)

WP_API
WpEventHook * wp_simple_event_hook_new (gint priority,
    WpEventHookExecType type, GClosure * closure);


/*!
 * \brief The WpAsyncEventHook GType
 * \ingroup wpeventhook
 */
#define WP_TYPE_ASYNC_EVENT_HOOK (wp_async_event_hook_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpAsyncEventHook, wp_async_event_hook,
                      WP, ASYNC_EVENT_HOOK, WpInterestEventHook)

WP_API
WpEventHook * wp_async_event_hook_new (gint priority,
    WpEventHookExecType type, GClosure * get_next_step,
    GClosure * execute_step);

G_END_DECLS

#endif
