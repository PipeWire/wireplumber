/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_REMOTE_H__
#define __WIREPLUMBER_REMOTE_H__

#include "core.h"

G_BEGIN_DECLS

/**
 * WpRemoteState:
 * @WP_REMOTE_STATE_ERROR: remote is in error
 * @WP_REMOTE_STATE_UNCONNECTED: not connected
 * @WP_REMOTE_STATE_CONNECTING: connecting to remote service
 * @WP_REMOTE_STATE_CONNECTED: remote is connected and ready
 *
 * The different states the remote can be
 */
typedef enum {
  WP_REMOTE_STATE_ERROR = -1,
  WP_REMOTE_STATE_UNCONNECTED = 0,
  WP_REMOTE_STATE_CONNECTING = 1,
  WP_REMOTE_STATE_CONNECTED = 2,
} WpRemoteState;

#define WP_TYPE_REMOTE (wp_remote_get_type ())
G_DECLARE_DERIVABLE_TYPE (WpRemote, wp_remote, WP, REMOTE, GObject)

struct _WpRemoteClass
{
  GObjectClass parent_class;

  gboolean (*connect) (WpRemote *self);
};

WpCore *wp_remote_get_core (WpRemote *self);
gboolean wp_remote_connect (WpRemote *self);

G_END_DECLS

#endif
