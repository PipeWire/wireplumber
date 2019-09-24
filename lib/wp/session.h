/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_SESSION_H__
#define __WIREPLUMBER_SESSION_H__

#include "exported.h"
#include "proxy.h"

G_BEGIN_DECLS

#define WP_TYPE_SESSION (wp_session_get_type ())
G_DECLARE_INTERFACE (WpSession, wp_session, WP, SESSION, GObject)

typedef enum {
  WP_DEFAULT_ENDPOINT_TYPE_AUDIO_SOURCE = 0x1000000 /* SPA_PROP_START_CUSTOM */,
  WP_DEFAULT_ENDPOINT_TYPE_AUDIO_SINK,
  WP_DEFAULT_ENDPOINT_TYPE_VIDEO_SOURCE,
} WpDefaultEndpointType;

struct _WpSessionInterface
{
  GTypeInterface parent;

  WpProperties * (*get_properties) (WpSession * self);

  guint32 (*get_default_endpoint) (WpSession * self,
      WpDefaultEndpointType type);
  void (*set_default_endpoint) (WpSession * self,
      WpDefaultEndpointType type, guint32 id);
};

WpProperties * wp_session_get_properties (WpSession * self);

guint32 wp_session_get_default_endpoint (WpSession * self,
    WpDefaultEndpointType type);
void wp_session_set_default_endpoint (WpSession * self,
    WpDefaultEndpointType type, guint32 id);

/* proxy */

typedef enum { /*< flags >*/
  WP_PROXY_SESSION_FEATURE_DEFAULT_ENDPOINT = WP_PROXY_FEATURE_LAST,
} WpProxySessionFeatures;

#define WP_TYPE_PROXY_SESSION (wp_proxy_session_get_type ())
G_DECLARE_FINAL_TYPE (WpProxySession, wp_proxy_session, WP, PROXY_SESSION, WpProxy)

const struct pw_session_info * wp_proxy_session_get_info (WpProxySession * self);

/* exported */

#define WP_TYPE_EXPORTED_SESSION (wp_exported_session_get_type ())
G_DECLARE_DERIVABLE_TYPE (WpExportedSession, wp_exported_session, WP, EXPORTED_SESSION, WpExported)

struct _WpExportedSessionClass
{
  WpExportedClass parent_class;
};

WpExportedSession * wp_exported_session_new (WpCore * core);

guint32 wp_exported_session_get_global_id (WpExportedSession * self);

void wp_exported_session_set_property (WpExportedSession * self,
    const gchar * key, const gchar * value);
void wp_exported_session_update_properties (WpExportedSession * self,
    WpProperties * updates);

G_END_DECLS

#endif
