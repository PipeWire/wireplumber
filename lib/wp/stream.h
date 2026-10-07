/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WP_STREAM_H__
#define __WP_STREAM_H__

#include "object.h"
#include "properties.h"
#include "node.h"
#include "port.h"
#include "spa-pod.h"

G_BEGIN_DECLS

/*!
 * \brief The different kinds a stream can be
 * \ingroup wpstream
 */
typedef enum {
  WP_STREAM_KIND_UNKNOWN,
  WP_STREAM_KIND_SUBJECT,
  WP_STREAM_KIND_TARGET,
} WpStreamKind;

/*!
 * \brief The different shapes a stream can have
 * \ingroup wpstream
 */
typedef enum {
  WP_STREAM_SHAPE_UNKNOWN,
  WP_STREAM_SHAPE_SINK,
  WP_STREAM_SHAPE_SOURCE,
  WP_STREAM_SHAPE_INPUT,
  WP_STREAM_SHAPE_OUTPUT,
} WpStreamShape;

/*!
 * \brief Flags to be used as WpObjectFeatures on WpStream.
 * \ingroup wpstream
 */
typedef enum { /*< flags >*/
  /*! Activates the stream */
  WP_STREAM_FEATURE_ACTIVE = (1 << 0),
} WpStreamFeatures;

/*!
 * \brief The WpStream GType
 * \ingroup wpstream
 */
#define WP_TYPE_STREAM (wp_stream_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpStream, wp_stream, WP, STREAM, WpObject)

struct _WpStreamClass
{
  WpObjectClass parent_class;

  /* vmethods */
  void (*enable_active) (WpStream *self, WpTransition * transition);
  void (*disable_active) (WpStream *self);
  gboolean (*is_compatible) (WpStream *self, WpStream *other,
      gboolean *encoded_only);
  guint (*get_supported_raw_format_count) (WpStream *self);
  guint (*get_supported_enc_format_count) (WpStream *self);
  WpIterator * (*new_supported_raw_format_iterator) (WpStream *self);
  WpIterator * (*new_supported_enc_format_iterator) (WpStream *self);
  gboolean (*is_exclusive) (WpStream *self);

  /* signals */
  void (*port_added) (WpStream *self, WpPort *port);
  void (*port_removed) (WpStream *self, WpPort *port);
  void (*ports_changed) (WpStream *self);

  WP_PADDING(5)
};

WP_API
WpNode * wp_stream_get_node (WpStream *self);

WP_API
WpProperties * wp_stream_get_properties (WpStream * self);

WP_API
WpDirection wp_stream_get_direction (WpStream * self);

WP_API
const gchar *wp_stream_get_port_group (WpStream *self);

WP_API
gint64 wp_stream_get_creation_time (WpStream *self);

WP_API
guint wp_stream_get_node_bound_id (WpStream * self);

WP_API
WpStreamKind wp_stream_get_kind (WpStream *self);

WP_API
WpStreamShape wp_stream_get_shape (WpStream *self);

WP_API
gboolean wp_stream_is_compatible (WpStream * self, WpStream *stream,
    gboolean *encoded_only);

WP_API
guint wp_stream_get_supported_raw_format_count (WpStream *self);

WP_API
guint wp_stream_get_supported_enc_format_count (WpStream *self);

WP_API
WpIterator * wp_stream_new_supported_raw_format_iterator (WpStream *self);

WP_API
WpIterator * wp_stream_new_supported_enc_format_iterator (WpStream *self);

WP_API
gboolean wp_stream_is_exclusive (WpStream *self);

WP_API
gboolean wp_stream_has_port (WpStream *self, guint32 port_id);

WP_API
gboolean wp_stream_has_monitor_port (WpStream *self, guint32 port_id);

WP_API
gboolean wp_stream_has_control_port (WpStream *self, guint32 port_id);

WP_API
guint wp_stream_get_ports_count (WpStream *self);

WP_API
guint wp_stream_get_monitor_ports_count (WpStream *self);

WP_API
guint wp_stream_get_control_ports_count (WpStream *self);

WP_API
WpIterator *wp_stream_new_ports_iterator (WpStream *self);

WP_API
WpIterator *wp_stream_new_monitor_ports_iterator (WpStream *self);

WP_API
WpIterator *wp_stream_new_control_ports_iterator (WpStream *self);


/*!
 * \brief The WpAudioStream GType
 * \ingroup wpstream
 */
#define WP_TYPE_AUDIO_STREAM (wp_audio_stream_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpAudioStream, wp_audio_stream, WP, AUDIO_STREAM,
    WpStream)

WP_API
WpSpaPod * wp_audio_stream_get_ports_format (WpAudioStream * self);

WP_API
const gchar * wp_audio_stream_get_ports_mode (WpAudioStream * self);

WP_API
gboolean wp_audio_stream_has_monitor_ports_enabled (WpAudioStream * self);

WP_API
gboolean wp_audio_stream_is_monitorable (WpAudioStream * self,
    WpAudioStream * peer);

WP_API
gboolean wp_audio_stream_has_control_ports_enabled (WpAudioStream * self);

WP_API
gboolean wp_audio_stream_has_ports_configured (WpAudioStream * self);

WP_API
gboolean wp_audio_stream_configure_ports (WpAudioStream * self,
    WpSpaPod *format, const gchar *mode, gboolean monitor, gboolean control);

WP_API
gboolean wp_audio_stream_configure_ports_with_props (WpAudioStream * self,
    WpProperties *props);

WP_API
gboolean wp_audio_stream_configure_ports_with_stream (WpAudioStream * self,
    WpAudioStream *stream);

WP_API
gboolean wp_audio_stream_configure_ports_for_peer (WpAudioStream * self,
    WpAudioStream *peer);


/*!
 * \brief The WpVideoStream GType
 * \ingroup wpstream
 */
#define WP_TYPE_VIDEO_STREAM (wp_video_stream_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpVideoStream, wp_video_stream, WP, VIDEO_STREAM,
    WpStream)

G_END_DECLS

#endif
