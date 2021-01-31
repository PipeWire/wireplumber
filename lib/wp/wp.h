/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_WP_H__
#define __WIREPLUMBER_WP_H__

#include "client.h"
#include "component-loader.h"
#include "configuration.h"
#include "core.h"
#include "debug.h"
#include "device.h"
#include "endpoint.h"
#include "endpoint-link.h"
#include "endpoint-stream.h"
#include "error.h"
#include "global-proxy.h"
#include "iterator.h"
#include "link.h"
#include "metadata.h"
#include "module.h"
#include "node.h"
#include "object-interest.h"
#include "object-manager.h"
#include "object.h"
#include "plugin.h"
#include "port.h"
#include "properties.h"
#include "proxy.h"
#include "proxy-interfaces.h"
#include "session.h"
#include "session-bin.h"
#include "session-item.h"
#include "si-factory.h"
#include "si-interfaces.h"
#include "spa-pod.h"
#include "spa-type.h"
#include "state.h"
#include "transition.h"
#include "wpenums.h"
#include "wpversion.h"

G_BEGIN_DECLS

typedef enum {
  WP_INIT_PIPEWIRE = (1<<0),
  WP_INIT_SPA_TYPES = (1<<1),
  WP_INIT_SET_PW_LOG = (1<<2),
  WP_INIT_SET_GLIB_LOG = (1<<3),
  WP_INIT_ALL = 0xf,
} WpInitFlags;

WP_API
void wp_init (WpInitFlags flags);

G_END_DECLS

#endif
