/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_STREAM_LINK_MANAGER_H__
#define __WIREPLUMBER_STREAM_LINK_MANAGER_H__

#include "object.h"
#include "stream-link.h"

G_BEGIN_DECLS

/*!
 * \brief Flags to be used as WpObjectFeatures on WpStreamLinkManager.
 * \ingroup wpstreamlinkmanager
 */
typedef enum { /*< flags >*/
  /*! Loads the stream link manager */
  WP_STREAM_LINK_MANAGER_FEATURE_LOADED = (1 << 0),
} WpStreamLinkManagerFeatures;

/*!
 * \brief The WpStreamLinkManager GType
 * \ingroup wpstreamlinkmanager
 */
#define WP_TYPE_STREAM_LINK_MANAGER (wp_stream_link_manager_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpStreamLinkManager, wp_stream_link_manager, WP,
    STREAM_LINK_MANAGER, WpObject)

WP_API
WpStreamLinkManager * wp_stream_link_manager_new (WpCore *core,
    const gchar *name);

WP_API
WpStreamLinkManager * wp_stream_link_manager_find (WpCore * core,
    const gchar *name);

WP_API
const gchar * wp_stream_link_manager_get_name (WpStreamLinkManager *self);

WP_API
WpStreamLink * wp_stream_link_manager_find_link (WpStreamLinkManager *self,
    WpStream *output, WpStream *input);

WP_API
WpStreamLink * wp_stream_link_manager_create_link (WpStreamLinkManager *self,
    WpStream *output, WpStream *input);

WP_API
void wp_stream_link_manager_remove_link (WpStreamLinkManager *self,
    WpStreamLink *link);

WP_API
void wp_stream_link_manager_unlink (WpStreamLinkManager *self,
    WpStream *stream);

WP_API
guint wp_stream_link_manager_get_peers_count (WpStreamLinkManager *self,
    WpStream *stream);

WP_API
WpIterator * wp_stream_link_manager_new_peers_iterator (
    WpStreamLinkManager *self, WpStream *stream);

WP_API
WpIterator * wp_stream_link_manager_new_links_iterator (
    WpStreamLinkManager *self, WpStream *stream);

G_END_DECLS

#endif
