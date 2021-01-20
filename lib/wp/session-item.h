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
 * @WP_SI_FLAG_ACTIVATE_ERROR: set when there was an error in the activation
 *   process; to clear, call wp_session_item_deactivate()
 * @WP_SI_FLAG_EXPORTING: set when an export operation is in progress
 * @WP_SI_FLAG_EXPORTED: set when the item has exported all necessary objects
 *   to PipeWire
 * @WP_SI_FLAG_EXPORT_ERROR: set when there was an error in the export
 *   process; to clear, call wp_session_item_unexport()
 * @WP_SI_FLAG_CONFIGURED: must be set by subclasses when all the required
 *   (%WP_SI_CONFIG_OPTION_REQUIRED) configuration options have been set
 */
typedef enum {
  /* immutable flags, set internally */
  WP_SI_FLAG_ACTIVATING = (1<<0),
  WP_SI_FLAG_ACTIVE = (1<<1),
  WP_SI_FLAG_ACTIVATE_ERROR = (1<<2),

  WP_SI_FLAG_EXPORTING = (1<<4),
  WP_SI_FLAG_EXPORTED = (1<<5),
  WP_SI_FLAG_EXPORT_ERROR = (1<<6),

  /* flags that can be changed by subclasses */
  WP_SI_FLAG_CONFIGURED = (1<<8),

  /* implementation-specific flags */
  WP_SI_FLAG_CUSTOM_START = (1<<16),
} WpSiFlags;

/**
 * WP_SI_FLAGS_MASK_OPERATION_IN_PROGRESS:
 *
 * A #WpSiFlags mask that can be used to test if an async operation
 * (activate or export) is currently in progress.
 */
#define WP_SI_FLAGS_MASK_OPERATION_IN_PROGRESS \
    (WP_SI_FLAG_ACTIVATING | WP_SI_FLAG_EXPORTING)

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
 * @activate_get_next_step: Implements #WpTransitionClass.get_next_step()
 *   for the transition of wp_session_item_activate()
 * @activate_execute_step: Implements #WpTransitionClass.execute_step()
 *   for the transition of wp_session_item_activate()
 * @activate_rollback: Reverses any effects of the activation process;
 *   see wp_session_item_activate()
 * @export_get_next_step: Implements #WpTransitionClass.get_next_step()
 *   for the transition of wp_session_item_export()
 * @export_execute_step: Implements #WpTransitionClass.execute_step()
 *   for the transition of wp_session_item_export()
 * @export_rollback: Reverses any effects of the export process;
 *   see wp_session_item_export()
 */
struct _WpSessionItemClass
{
  GObjectClass parent_class;

  void (*reset) (WpSessionItem * self);

  gpointer (*get_associated_proxy) (WpSessionItem * self, GType proxy_type);

  gboolean (*configure) (WpSessionItem * self, GVariant * args);
  GVariant * (*get_configuration) (WpSessionItem * self);

  guint (*activate_get_next_step) (WpSessionItem * self,
      WpTransition * transition, guint step);
  void (*activate_execute_step) (WpSessionItem * self,
      WpTransition * transition, guint step);
  void (*activate_rollback) (WpSessionItem * self);

  guint (*export_get_next_step) (WpSessionItem * self,
      WpTransition * transition, guint step);
  void (*export_execute_step) (WpSessionItem * self,
      WpTransition * transition, guint step);
  void (*export_rollback) (WpSessionItem * self);
};

WP_API
void wp_session_item_reset (WpSessionItem * self);

WP_API
WpSessionItem * wp_session_item_get_parent (WpSessionItem * self);

WP_PRIVATE_API
void wp_session_item_set_parent (WpSessionItem *self, WpSessionItem *parent);

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
void wp_session_item_activate_closure (WpSessionItem * self, GClosure *closure);

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
void wp_session_item_export_closure (WpSessionItem * self, WpSession * session,
    GClosure *closure);

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
