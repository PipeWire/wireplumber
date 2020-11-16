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
#include "session-item.h"
#include "spa-type.h"

#include <stdint.h>
#include <pipewire/pipewire.h>

G_BEGIN_DECLS

struct spa_pod;
struct spa_pod_builder;

/* props */

void wp_props_handle_proxy_param_event (WpProps * self, guint32 id,
    WpSpaPod * pod);

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

G_END_DECLS

#endif
