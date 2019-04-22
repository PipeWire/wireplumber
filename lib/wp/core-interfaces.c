/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "core-interfaces.h"
#include "plugin.h"
#include "session.h"

/* WpPluginRegistry */

G_DEFINE_INTERFACE (WpPluginRegistry, wp_plugin_registry, WP_TYPE_INTERFACE_IMPL)

static void
wp_plugin_registry_default_init (WpPluginRegistryInterface * iface)
{
}

/**
 * wp_plugin_registry_register_static: (skip)
 * @plugin_type: the #GType of the #WpPlugin subclass
 * @metadata: the metadata
 * @metadata_size: the sizeof (@metadata), to allow ABI-compatible future
 *   expansion of the structure
 *
 * Registers a plugin in the registry.
 * This method is used internally by WP_PLUGIN_REGISTER().
 * Avoid using it directly.
 */
void
wp_plugin_registry_register_static (WpPluginRegistry * self,
    GType plugin_type,
    const WpPluginMetadata * metadata,
    gsize metadata_size)
{
  WpPluginRegistryInterface *iface = WP_PLUGIN_REGISTRY_GET_IFACE (self);

  g_return_if_fail (WP_IS_PLUGIN_REGISTRY (self));
  g_return_if_fail (iface->register_plugin != NULL);
  g_return_if_fail (g_type_is_a (plugin_type, WP_TYPE_PLUGIN));
  g_return_if_fail (metadata->name != NULL);
  g_return_if_fail (metadata->description != NULL);
  g_return_if_fail (metadata->author != NULL);
  g_return_if_fail (metadata->license != NULL);
  g_return_if_fail (metadata->version != NULL);
  g_return_if_fail (metadata->origin != NULL);

  WP_PLUGIN_REGISTRY_GET_IFACE (self)->register_plugin (self, plugin_type,
      metadata, metadata_size, TRUE);
}

/**
 * wp_plugin_registry_register: (method)
 * @plugin_type: the #GType of the #WpPlugin subclass
 * @rank: the rank of the plugin
 * @name: the name of the plugin
 * @description: plugin description
 * @author: author <email@domain>, author2 <email@domain>
 * @license: a SPDX license ID or "Proprietary"
 * @version: the version of the plugin
 * @origin: URL or short reference of where this plugin came from
 *
 * Registers a plugin in the registry.
 * This method creates a dynamically allocated #WpPluginMetadata and is meant
 * to be used by bindings that have no way of representing #WpPluginMetadata.
 * In C/C++, you should use WP_PLUGIN_REGISTER()
 */
void
wp_plugin_registry_register (WpPluginRegistry * self,
    GType plugin_type,
    guint16 rank,
    const gchar *name,
    const gchar *description,
    const gchar *author,
    const gchar *license,
    const gchar *version,
    const gchar *origin)
{
  WpPluginRegistryInterface *iface = WP_PLUGIN_REGISTRY_GET_IFACE (self);
  WpPluginMetadata metadata = {0};

  g_return_if_fail (WP_IS_PLUGIN_REGISTRY (self));
  g_return_if_fail (iface->register_plugin != NULL);
  g_return_if_fail (g_type_is_a (plugin_type, WP_TYPE_PLUGIN));
  g_return_if_fail (name != NULL);
  g_return_if_fail (description != NULL);
  g_return_if_fail (author != NULL);
  g_return_if_fail (license != NULL);
  g_return_if_fail (version != NULL);
  g_return_if_fail (origin != NULL);

  metadata.rank = rank;
  metadata.name = name;
  metadata.description = description;
  metadata.author = author;
  metadata.license = license;
  metadata.version = version;
  metadata.origin = origin;

  WP_PLUGIN_REGISTRY_GET_IFACE (self)->register_plugin (self, plugin_type,
      &metadata, sizeof (metadata), FALSE);
}

/* WpProxyRegistry */

G_DEFINE_INTERFACE (WpProxyRegistry, wp_proxy_registry, WP_TYPE_INTERFACE_IMPL)

static void
wp_proxy_registry_default_init (WpProxyRegistryInterface * iface)
{
}

/**
 * wp_proxy_registry_get_proxy: (method)
 * @self: the registry
 * @global_id: the ID of the pw_global that is represented by the proxy
 *
 * Returns: (transfer full): the #WpProxy that represents the global with
 *    @global_id
 */
WpProxy *
wp_proxy_registry_get_proxy (WpProxyRegistry * self, guint32 global_id)
{
  WpProxyRegistryInterface * iface = WP_PROXY_REGISTRY_GET_IFACE (self);

  g_return_val_if_fail (WP_IS_PROXY_REGISTRY (self), NULL);
  g_return_val_if_fail (iface->get_proxy != NULL, NULL);

  return iface->get_proxy (self, global_id);
}

/**
 * wp_proxy_registry_get_pw_registry_proxy: (skip)
 * @self: the registry
 *
 * Returns: the underlying `pw_registry_proxy`
 */
struct pw_registry_proxy *
wp_proxy_registry_get_pw_registry_proxy (WpProxyRegistry * self)
{
  WpProxyRegistryInterface * iface = WP_PROXY_REGISTRY_GET_IFACE (self);

  g_return_val_if_fail (WP_IS_PROXY_REGISTRY (self), NULL);
  g_return_val_if_fail (iface->get_pw_registry_proxy != NULL, NULL);

  return iface->get_pw_registry_proxy (self);
}

/* WpSessionRegistry */

G_DEFINE_INTERFACE (WpSessionRegistry, wp_session_registry, G_TYPE_OBJECT)

enum {
  SIG_SESSION_REGISTERED,
  SIG_SESSION_UNREGISTERED,
  N_SESSION_REGISTRY_SIGNALS
};

static guint session_registry_signals[N_SESSION_REGISTRY_SIGNALS];

static void
wp_session_registry_default_init (WpSessionRegistryInterface * iface)
{
  session_registry_signals[SIG_SESSION_REGISTERED] = g_signal_new (
      "session-registered", G_TYPE_FROM_INTERFACE (iface),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT,
      WP_TYPE_SESSION);

  session_registry_signals[SIG_SESSION_UNREGISTERED] = g_signal_new (
      "session-unregistered", G_TYPE_FROM_INTERFACE (iface),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

guint32
wp_session_registry_register_session (WpSessionRegistry * self,
    WpSession * session,
    GError ** error)
{
  WpSessionRegistryInterface *iface = WP_SESSION_REGISTRY_GET_IFACE (self);
  guint32 id;

  g_return_val_if_fail (WP_IS_SESSION_REGISTRY (self), -1);
  g_return_val_if_fail (session != NULL, -1);
  g_return_val_if_fail (iface->register_session, -1);

  id = iface->register_session (self, session, error);
  if (id != -1) {
    g_signal_emit (self, session_registry_signals[SIG_SESSION_REGISTERED], 0,
        id, session);
  }
  return id;
}

gboolean
wp_session_registry_unregister_session (WpSessionRegistry * self,
    guint32 session_id)
{
  WpSessionRegistryInterface *iface = WP_SESSION_REGISTRY_GET_IFACE (self);
  gboolean ret;

  g_return_val_if_fail (WP_IS_SESSION_REGISTRY (self), FALSE);
  g_return_val_if_fail (iface->unregister_session, FALSE);

  ret = iface->unregister_session (self, session_id);
  if (ret) {
    g_signal_emit (self, session_registry_signals[SIG_SESSION_UNREGISTERED], 0,
        session_id);
  }
  return ret;
}

GArray *
wp_session_registry_list_sessions (WpSessionRegistry * self,
    const gchar * media_class)
{
  WpSessionRegistryInterface *iface = WP_SESSION_REGISTRY_GET_IFACE (self);

  g_return_val_if_fail (WP_IS_SESSION_REGISTRY (self), NULL);
  g_return_val_if_fail (iface->list_sessions, NULL);

  return iface->list_sessions (self, media_class);
}

WpSession *
wp_session_registry_get_session (WpSessionRegistry * self,
    guint32 session_id)
{
  WpSessionRegistryInterface *iface = WP_SESSION_REGISTRY_GET_IFACE (self);

  g_return_val_if_fail (WP_IS_SESSION_REGISTRY (self), NULL);
  g_return_val_if_fail (iface->get_session, NULL);

  return iface->get_session (self, session_id);
}
