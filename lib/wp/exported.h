/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_EXPORTED_H__
#define __WIREPLUMBER_EXPORTED_H__

#include <gio/gio.h>
#include "proxy.h"

G_BEGIN_DECLS

#define WP_TYPE_EXPORTED (wp_exported_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpExported, wp_exported, WP, EXPORTED, GObject)

struct _WpExportedClass
{
  GObjectClass parent_class;

  void (*export) (WpExported * self);
  void (*unexport) (WpExported * self);
  WpProxy * (*get_proxy) (WpExported * self);
};

WP_API
WpCore * wp_exported_get_core (WpExported * self);

WP_API
void wp_exported_export (WpExported * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

WP_API
gboolean wp_exported_export_finish (WpExported * self,
    GAsyncResult * res, GError ** error);

WP_API
void wp_exported_unexport (WpExported * self);

WP_API
WpProxy * wp_exported_get_proxy (WpExported * self);

G_END_DECLS

#endif
