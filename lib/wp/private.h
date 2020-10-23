/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_H__
#define __WIREPLUMBER_PRIVATE_H__

#include "core.h"
#include "object-manager.h"
#include "props.h"
#include "proxy.h"
#include "endpoint.h"
#include "endpoint-stream.h"
#include "si-interfaces.h"
#include "iterator.h"
#include "spa-type.h"
#include "private/registry.h"

#include <stdint.h>
#include <pipewire/pipewire.h>

G_BEGIN_DECLS

struct spa_pod;
struct spa_pod_builder;

/* props */

void wp_props_handle_proxy_param_event (WpProps * self, guint32 id,
    WpSpaPod * pod);

/* proxy */

void wp_proxy_destroy (WpProxy *self);
void wp_proxy_set_pw_proxy (WpProxy * self, struct pw_proxy * proxy);

void wp_proxy_set_feature_ready (WpProxy * self, WpProxyFeatures feature);
void wp_proxy_augment_error (WpProxy * self, GError * error);

void wp_proxy_handle_event_param (void * proxy, int seq, uint32_t id,
    uint32_t index, uint32_t next, const struct spa_pod *param);

WpProps * wp_proxy_get_props (WpProxy * self);
void wp_proxy_set_props (WpProxy * self, WpProps * props);

/* iterator */

struct _WpIteratorMethods {
  void (*reset) (WpIterator *self);
  gboolean (*next) (WpIterator *self, GValue *item);
  gboolean (*fold) (WpIterator *self, WpIteratorFoldFunc func,
      GValue *ret, gpointer data);
  gboolean (*foreach) (WpIterator *self, WpIteratorForeachFunc func,
      gpointer data);
  void (*finalize) (WpIterator *self);
};
typedef struct _WpIteratorMethods WpIteratorMethods;

WpIterator * wp_iterator_new (const WpIteratorMethods *methods,
    size_t user_size);
gpointer wp_iterator_get_user_data (WpIterator *self);

/* spa pod */

typedef struct _WpSpaPod WpSpaPod;
WpSpaPod * wp_spa_pod_new_wrap (struct spa_pod *pod);
WpSpaPod * wp_spa_pod_new_wrap_const (const struct spa_pod *pod);
WpSpaPod * wp_spa_pod_new_property_wrap (WpSpaTypeTable table, guint32 key,
    guint32 flags, struct spa_pod *pod);
WpSpaPod * wp_spa_pod_new_property_wrap_const (WpSpaTypeTable table,
    guint32 key, guint32 flags, const struct spa_pod *pod);
WpSpaPod * wp_spa_pod_new_control_wrap (guint32 offset, guint32 type,
    struct spa_pod *pod);
WpSpaPod * wp_spa_pod_new_control_wrap_const (guint32 offset, guint32 type,
    const struct spa_pod *pod);
const struct spa_pod *wp_spa_pod_get_spa_pod (const WpSpaPod *self);

/* session item */

void wp_session_item_set_parent (WpSessionItem *self, WpSessionItem *parent);

/* impl endpoint */

#define WP_TYPE_IMPL_ENDPOINT (wp_impl_endpoint_get_type ())
G_DECLARE_FINAL_TYPE (WpImplEndpoint, wp_impl_endpoint,
                      WP, IMPL_ENDPOINT, WpEndpoint)

WpImplEndpoint * wp_impl_endpoint_new (WpCore * core, WpSiEndpoint * item);

/* impl endpoint stream */

#define WP_TYPE_IMPL_ENDPOINT_STREAM (wp_impl_endpoint_stream_get_type ())
G_DECLARE_FINAL_TYPE (WpImplEndpointStream, wp_impl_endpoint_stream,
                      WP, IMPL_ENDPOINT_STREAM, WpEndpointStream)

WpImplEndpointStream * wp_impl_endpoint_stream_new (WpCore * core,
    WpSiStream * item);

/* impl endpoint link */

#define WP_TYPE_IMPL_ENDPOINT_LINK (wp_impl_endpoint_link_get_type ())
G_DECLARE_FINAL_TYPE (WpImplEndpointLink, wp_impl_endpoint_link,
                      WP, IMPL_ENDPOINT_LINK, WpEndpointLink)

WpImplEndpointLink * wp_impl_endpoint_link_new (WpCore * core, WpSiLink * item);

G_END_DECLS

#endif
