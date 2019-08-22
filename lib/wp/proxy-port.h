/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROXY_PORT_H__
#define __WIREPLUMBER_PROXY_PORT_H__

#include "proxy.h"

G_BEGIN_DECLS

typedef enum { /*< flags >*/
  WP_PROXY_PORT_FEATURE_FORMAT = (WP_PROXY_FEATURE_LAST << 0),
} WpProxyPortFeatures;

#define WP_TYPE_PROXY_PORT (wp_proxy_port_get_type ())
G_DECLARE_FINAL_TYPE (WpProxyPort, wp_proxy_port, WP, PROXY_PORT, WpProxy)

static inline const struct pw_port_info *
wp_proxy_port_get_info (WpProxyPort * self)
{
  return (const struct pw_port_info *)
      wp_proxy_get_native_info (WP_PROXY (self));
}

const struct spa_audio_info_raw *wp_proxy_port_get_format (WpProxyPort * self);

G_END_DECLS

#endif
