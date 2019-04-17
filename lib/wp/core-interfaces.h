/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WP_CORE_INTERFACES_H__
#define __WP_CORE_INTERFACES_H__

#include "interface-impl.h"

G_BEGIN_DECLS

/* WpPluginRegistry */

#define WP_TYPE_PLUGIN_REGISTRY (wp_plugin_registry_get_type ())
G_DECLARE_INTERFACE (WpPluginRegistry, wp_plugin_registry, WP, PLUGIN_REGISTRY, WpInterfaceImpl)

typedef struct _WpPluginMetadata WpPluginMetadata;

struct _WpPluginRegistryInterface
{
  GTypeInterface parent;

  void (*register_plugin) (WpPluginRegistry * self,
      GType plugin_type,
      const WpPluginMetadata * metadata,
      gsize metadata_size,
      gboolean static_data);
};

void wp_plugin_registry_register_static (WpPluginRegistry * self,
    GType plugin_type,
    const WpPluginMetadata * metadata,
    gsize metadata_size);

void wp_plugin_registry_register (WpPluginRegistry * self,
    GType plugin_type,
    guint16 rank,
    const gchar *name,
    const gchar *description,
    const gchar *author,
    const gchar *license,
    const gchar *version,
    const gchar *origin);

/* WpProxyRegistry */

struct pw_registry_proxy;
typedef struct _WpProxy WpProxy;

#define WP_TYPE_PROXY_REGISTRY (wp_proxy_registry_get_type ())
G_DECLARE_INTERFACE (WpProxyRegistry, wp_proxy_registry, WP, PROXY_REGISTRY, WpInterfaceImpl)

struct _WpProxyRegistryInterface
{
  GTypeInterface parent;

  WpProxy * (*get_proxy) (WpProxyRegistry * self, guint32 global_id);
  struct pw_registry_proxy * (*get_pw_registry_proxy) (WpProxyRegistry * self);
};

WpProxy * wp_proxy_registry_get_proxy (WpProxyRegistry * self,
    guint32 global_id);

struct pw_registry_proxy * wp_proxy_registry_get_pw_registry_proxy (
    WpProxyRegistry * self);

G_END_DECLS

#endif
