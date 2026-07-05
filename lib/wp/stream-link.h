/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WP_STREAM_LINK_H__
#define __WP_STREAM_LINK_H__

#include "object.h"
#include "properties.h"
#include "stream.h"
#include "link.h"

G_BEGIN_DECLS

/*!
 * \brief Flags to be used as WpObjectFeatures on WpStreamLink.
 * \ingroup wpstreamlink
 */
typedef enum { /*< flags >*/
  /*! Loads the stream link */
  WP_STREAM_LINK_FEATURE_ACTIVE = (1 << 0),
} WpStreamLinkFeatures;

/*!
 * \brief The WpStreamLink GType
 * \ingroup wpstreamlink
 */
#define WP_TYPE_STREAM_LINK (wp_stream_link_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpStreamLink, wp_stream_link, WP, STREAM_LINK, WpObject)

struct _WpStreamLinkClass
{
  WpObjectClass parent_class;

  /* vmethods */
  void (*setup_ports) (WpStreamLink *self, WpTransition * transition);
  void (*teardown_ports) (WpStreamLink *self);

  /* signals */
  void (*link_added) (WpStreamLink *self, WpLink *link);
  void (*link_removed) (WpStreamLink *self, WpLink *link);

  /*< private >*/
  WP_PADDING(4)
};

WP_API
WpStream * wp_stream_link_get_output_stream (WpStreamLink *self,
    gboolean * is_monitor);

WP_API
WpStream * wp_stream_link_get_input_stream (WpStreamLink *self);

WP_API
guint wp_stream_link_get_links_count (WpStreamLink *self);

WP_API
WpIterator * wp_stream_link_new_links_iterator (WpStreamLink *self);


/*!
 * \brief The WpAudioStreamLink GType
 * \ingroup wpstreamlink
 */
#define WP_TYPE_AUDIO_STREAM_LINK (wp_audio_stream_link_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpAudioStreamLink, wp_audio_stream_link, WP,
    AUDIO_STREAM_LINK, WpStreamLink)


/*!
 * \brief The WpVideoStreamLink GType
 * \ingroup wpstreamlink
 */
#define WP_TYPE_VIDEO_STREAM_LINK (wp_video_stream_link_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpVideoStreamLink, wp_video_stream_link, WP,
    VIDEO_STREAM_LINK, WpStreamLink)

G_END_DECLS

#endif
