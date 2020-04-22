/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_SESSION_BIN_H__
#define __WIREPLUMBER_SESSION_BIN_H__

#include "core.h"
#include "session-item.h"
#include "iterator.h"

G_BEGIN_DECLS

/**
 * WP_TYPE_SESSION_BIN:
 *
 * The #WpSessionBin #GType
 */
#define WP_TYPE_SESSION_BIN (wp_session_bin_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpSessionBin, wp_session_bin, WP, SESSION_BIN,
    WpSessionItem)

/**
 * WpSessionBinClass:
 */
struct _WpSessionBinClass
{
  WpSessionItemClass parent_class;
};

WP_API
WpSessionBin *wp_session_bin_new (void);

WP_API
gboolean wp_session_bin_add (WpSessionBin *self, WpSessionItem *item);

WP_API
gboolean wp_session_bin_remove (WpSessionBin *self, WpSessionItem *item);

WP_API
WpIterator *wp_session_bin_iterate (WpSessionBin *self);

G_END_DECLS

#endif
