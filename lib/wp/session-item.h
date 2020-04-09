/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_SESSION_ITEM_H__
#define __WIREPLUMBER_SESSION_ITEM_H__

#include "transition.h"
#include "session.h"

G_BEGIN_DECLS

/**
 * WP_TYPE_SESSION_ITEM:
 *
 * The #WpSessionItem #GType
 */
#define WP_TYPE_SESSION_ITEM (wp_session_item_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpSessionItem, wp_session_item,
                          WP, SESSION_ITEM, GObject)

/**
 * WpSiFlags:
 * @WP_SI_FLAG_ACTIVATING: set when an activation transition is in progress
 * @WP_SI_FLAG_ACTIVE: set when an activation transition completes successfully
 * @WP_SI_FLAG_IN_ERROR: set when there was an error in the activation process;
 *   to recover, the handler must call wp_session_item_reset() before anything
 *   else
 * @WP_SI_FLAG_CONFIGURED: must be set by subclasses when all the required
 *   (%WP_SI_CONFIG_OPTION_REQUIRED) configuration options have been set
 * @WP_SI_FLAG_EXPORTING: set when an export operation is in progress
 * @WP_SI_FLAG_EXPORTED: set when the item has exported all necessary objects
 *   to PipeWire
 */
typedef enum {
  /* immutable flags, set internally */
  WP_SI_FLAG_ACTIVATING = (1<<0),
  WP_SI_FLAG_ACTIVE = (1<<1),
  WP_SI_FLAG_IN_ERROR = (1<<4),

  /* flags that can be changed by subclasses */
  WP_SI_FLAG_CONFIGURED = (1<<8),
  WP_SI_FLAG_EXPORTING = (1<<9),
  WP_SI_FLAG_EXPORTED = (1<<10),

  /* implementation-specific flags */
  WP_SI_FLAG_CUSTOM_START = (1<<16),
} WpSiFlags;

/**
 * WpSiConfigOptionFlags:
 * @WP_SI_CONFIG_OPTION_WRITEABLE: the option can be set externally
 * @WP_SI_CONFIG_OPTION_REQUIRED: the option is required to activate the item
 */
typedef enum {
  WP_SI_CONFIG_OPTION_WRITEABLE = (1<<0),
  WP_SI_CONFIG_OPTION_REQUIRED = (1<<1),
} WpSiConfigOptionFlags;

/**
 * WpSessionItemClass:
 * @reset: See wp_session_item_reset()
 * @get_associated_proxy: See wp_session_item_get_associated_proxy()
 * @configure: See wp_session_item_configure()
 * @get_configuration: See wp_session_item_get_configuration()
 * @get_next_step: Implements #WpTransitionClass.get_next_step() for the
 *   transition of wp_session_item_activate()
 * @execute_step: Implements #WpTransitionClass.execute_step() for the
 *   transition of wp_session_item_activate()
 * @deactivate: See wp_session_item_deactivate()
 * @export: See wp_session_item_export()
 * @export_finish: See wp_session_item_export_finish()
 * @unexport: See wp_session_item_unexport()
 */
struct _WpSessionItemClass
{
  GObjectClass parent_class;

  void (*reset) (WpSessionItem * self);

  gpointer (*get_associated_proxy) (WpSessionItem * self, GType proxy_type);

  gboolean (*configure) (WpSessionItem * self, GVariant * args);
  GVariant * (*get_configuration) (WpSessionItem * self);

  guint (*get_next_step) (WpSessionItem * self, WpTransition * transition,
      guint step);
  void (*execute_step) (WpSessionItem * self, WpTransition * transition,
      guint step);
  void (*deactivate) (WpSessionItem * self);

  void (*export) (WpSessionItem * self,
      WpSession * session, GCancellable * cancellable,
      GAsyncReadyCallback callback, gpointer callback_data);
  gboolean (*export_finish) (WpSessionItem * self, GAsyncResult * res,
      GError ** error);
  void (*unexport) (WpSessionItem * self);
};

WP_API
void wp_session_item_reset (WpSessionItem * self);

/* flags */

WP_API
WpSiFlags wp_session_item_get_flags (WpSessionItem * self);

WP_API
void wp_session_item_set_flag (WpSessionItem * self, WpSiFlags flag);

WP_API
void wp_session_item_clear_flag (WpSessionItem * self, WpSiFlags flag);

/* associated proxies */

WP_API
gpointer wp_session_item_get_associated_proxy (WpSessionItem * self,
    GType proxy_type);

WP_API
guint32 wp_session_item_get_associated_proxy_id (WpSessionItem * self,
    GType proxy_type);

/* configuration */

WP_API
gboolean wp_session_item_configure (WpSessionItem * self, GVariant * args);

WP_API
GVariant * wp_session_item_get_configuration (WpSessionItem * self);

/* state management */

WP_API
void wp_session_item_activate (WpSessionItem * self,
    GAsyncReadyCallback callback, gpointer callback_data);

WP_API
gboolean wp_session_item_activate_finish (WpSessionItem * self,
    GAsyncResult * res, GError ** error);

WP_API
void wp_session_item_deactivate (WpSessionItem * self);

/* exporting */

WP_API
void wp_session_item_export (WpSessionItem * self, WpSession * session,
    GAsyncReadyCallback callback, gpointer callback_data);

WP_API
gboolean wp_session_item_export_finish (WpSessionItem * self,
    GAsyncResult * res, GError ** error);

WP_API
void wp_session_item_unexport (WpSessionItem * self);

G_END_DECLS

#endif
