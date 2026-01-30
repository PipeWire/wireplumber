/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@ollabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PERMISSION_MANAGER_H__
#define __WIREPLUMBER_PERMISSION_MANAGER_H__

#include "object-interest.h"
#include "global-proxy.h"

G_BEGIN_DECLS

/*!
 * \brief Flags to be used as WpObjectFeatures for WpPermissionManager.
 * \ingroup wppermissionmanager
 */
typedef enum {  /*< flags >*/
  /*! Loads the permission manager */
  WP_PERMISSION_MANAGER_LOADED = (1 << 0),
} WpPermissionManagerFeatures;

/*!
 * \brief The WpPermissionManager GType
 * \ingroup wppermissionmanager
 */
#define WP_TYPE_PERMISSION_MANAGER (wp_permission_manager_get_type ())

WP_API
G_DECLARE_FINAL_TYPE (WpPermissionManager, wp_permission_manager, WP,
    PERMISSION_MANAGER, WpObject)

typedef struct _WpClient WpClient;

/*!
 * \brief callback to set permissions on the matched global object
 *
 * \ingroup wppermissionmanager
 * \param self the permission manager
 * \param client the client that will have its permissions updated
 * \param object the matched global
 * \param user_data the passed data
 */
typedef guint32 (*WpPermissionMatchCallback) (WpPermissionManager *self,
    WpClient *client, WpGlobalProxy *object, gpointer user_data);

WP_API
WpPermissionManager * wp_permission_manager_new (WpCore * core);

WP_API
void wp_permission_manager_set_default_permissions (
    WpPermissionManager *self, guint32 permissions);

WP_API
guint32 wp_permission_manager_add_interest_match (WpPermissionManager *self,
    WpPermissionMatchCallback callback, gpointer user_data,
    WpObjectInterest * interest);

WP_API
guint32 wp_permission_manager_add_interest_match_closure (
    WpPermissionManager *self, GClosure *closure, WpObjectInterest * interest);

WP_API
guint32 wp_permission_manager_add_interest_match_simple (
    WpPermissionManager *self, guint32 permissions,
    WpObjectInterest * interest);

WP_API
guint32 wp_permission_manager_add_rules_match (WpPermissionManager *self,
    WpSpaJson *rules);

WP_API
void wp_permission_manager_remove_match (WpPermissionManager *self,
    guint32 match_id);

WP_API
void wp_permission_manager_update_permissions (WpPermissionManager *self);

G_END_DECLS

#endif
