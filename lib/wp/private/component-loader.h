/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_COMPONENT_LOADER_H__
#define __WIREPLUMBER_PRIVATE_COMPONENT_LOADER_H__

#include "component-loader.h"

G_BEGIN_DECLS

WpComponentLoader * wp_component_loader_find (WpCore * core,
    const gchar * type);

void wp_component_loader_load (WpComponentLoader * self, WpCore * core,
    const gchar * component, const gchar * type, WpSpaJson * args,
    GCancellable * cancellable, GAsyncReadyCallback callback, gpointer data);

GObject * wp_component_loader_load_finish (WpComponentLoader * self,
    GAsyncResult * res, GError ** error);

G_END_DECLS

#endif
