/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WP_PLUGIN_H__
#define __WP_PLUGIN_H__

#include "object.h"
#include "proxy.h"
#include "plugin-registry.h"

#include <gmodule.h>

G_BEGIN_DECLS

/**
 * WpPluginRank:
 * @WP_PLUGIN_RANK_UPSTREAM: should only be used inside WirePlumber
 * @WP_PLUGIN_RANK_PLATFORM_OVERRIDE: plugins provided by the platform,
 *   possibly to provide a platform-specific policy
 * @WP_PLUGIN_RANK_VENDOR_OVERRIDE: plugins provided by hardware vendors
 *   to provide hardware-specific device handling and/or policies
 *
 * The rank of a plugin is an unsigned integer that can take an arbitrary
 * value. On invocation, plugins ranked with a higher number are tried first,
 * which is how one can implement overrides. This enum provides default
 * values for certain kinds of plugins. Feel free to add/substract numbers
 * to these constants in order to make a hierarchy, if you are implementing
 * multiple different plugins that need to be tried in a certain order.
 */
typedef enum {
  WP_PLUGIN_RANK_UPSTREAM = 0,
  WP_PLUGIN_RANK_PLATFORM_OVERRIDE = 128,
  WP_PLUGIN_RANK_VENDOR_OVERRIDE = 256,
} WpPluginRank;

/**
 * WpPluginMetadata: (skip)
 * @gtype: the #GType of the plugin
 * @rank: the rank of the plugin
 * @name: the name of the plugin
 * @description: plugin description
 * @author: author <email@domain>, author2 <email@domain>
 * @license: a SPDX license ID or "Proprietary"
 * @version: the version of the plugin
 * @origin: URL or short reference of where this plugin came from
 *
 * Metadata for registering a plugin (for the C API).
 * You should normally never need to use this directly.
 * Use WP_PLUGIN_DEFINE() instead.
 */
struct _WpPluginMetadata
{
  union {
    struct {
      GType gtype;
      guint rank;
    };
    gpointer _unused_for_alignment[2];
  };
  const gchar *name;
  const gchar *description;
  const gchar *author;
  const gchar *license;
  const gchar *version;
  const gchar *origin;
};

G_DECLARE_DERIVABLE_TYPE (WpPlugin, wp_plugin, WP, PLUGIN, GObject)

struct _WpPluginClass
{
  GObjectClass parent_class;

  /**
   * handle_pw_proxy:
   * @self: the plugin
   * @proxy: (transfer none): the proxy
   *
   * This method is called for every new proxy that appears in PipeWire.
   * The default implementation will inspect the proxy type and will dispatch
   * the call to one of the specialized methods available below.
   * Override only for very special cases.
   */
  gboolean (*handle_pw_proxy) (WpPlugin * self, WpProxy * proxy);

  /**
   * handle_pw_device:
   * @self: the plugin
   * @proxy: (transfer none): the device proxy
   *
   * This method is called for every new PipeWire proxy of type
   * `PipeWire:Interface:Device`. The implementation is expected to create
   * a new #WpDevice and register it with the #WpDeviceManager.
   *
   * The default implementation returns FALSE.
   * Override if you are implementing custom device management.
   *
   * Returns: TRUE if the device was handled, FALSE otherwise.
   */
  gboolean (*handle_pw_device) (WpPlugin * self, WpProxy * proxy);

  /**
   * handle_pw_device_node:
   * @self: the plugin
   * @proxy: (transfer none): the node proxy
   *
   * This method is called for every new PipeWire proxy of type
   * `PipeWire:Interface:Node` whose parent proxy is a
   * `PipeWire:Interface:Device`.
   *
   * The default implementation returns FALSE.
   * Override if you are implementing custom device management.
   *
   * Returns: TRUE if the node was handled, FALSE otherwise.
   */
  gboolean (*handle_pw_device_node) (WpPlugin * self, WpProxy * proxy);

  /**
   * handle_pw_client:
   * @self: the plugin
   * @proxy: (transfer none): the client proxy
   *
   * This method is called for every new PipeWire proxy of type
   * `PipeWire:Interface:Client`. The implementation is expected to update
   * the client's permissions, if necessary.
   *
   * The default implementation returns FALSE.
   * Override if you are implementing custom policy management.
   *
   * Returns: TRUE if the client was handled, FALSE otherwise.
   */
  gboolean (*handle_pw_client) (WpPlugin * self, WpProxy * proxy);

  /**
   * handle_pw_client_node:
   * @self: the plugin
   * @proxy: (transfer none): the node proxy
   *
   * This method is called for every new PipeWire proxy of type
   * `PipeWire:Interface:Node` whose parent proxy is a
   * `PipeWire:Interface:Client`. The implementation is expected to create
   * a new #WpStream in some #WpSession.
   *
   * The default implementation returns FALSE.
   * Override if you are implementing custom policy management.
   *
   * Returns: TRUE if the node was handled, FALSE otherwise.
   */
  gboolean (*handle_pw_client_node) (WpPlugin * self, WpProxy * proxy);

  /**
   * provide_interfaces:
   * @self: the plugin
   * @object: (transfer none): a #WpObject
   *
   * This method is called for every new #WpObject created in WirePlumber.
   * The implementation is expected to attach any interface implementations
   * that it can provide for this kind of object, if necessary, only if
   * these interfaces have not already been attached on the @object.
   *
   * The default implementation returns FALSE.
   * Override if you are providing custom interface implementations for objects.
   *
   * Returns: TRUE if the node was handled, FALSE otherwise.
   */
  gboolean (*provide_interfaces) (WpPlugin * self, WpObject * object);
};

gboolean wp_plugin_handle_pw_proxy (WpPlugin * self, WpProxy * proxy);
gboolean wp_plugin_handle_pw_device (WpPlugin * self, WpProxy * proxy);
gboolean wp_plugin_handle_pw_device_node (WpPlugin * self, WpProxy * proxy);
gboolean wp_plugin_handle_pw_client (WpPlugin * self, WpProxy * proxy);
gboolean wp_plugin_handle_pw_client_node (WpPlugin * self, WpProxy * proxy);
gboolean wp_plugin_provide_interfaces (WpPlugin * self, WpObject * object);

WpPluginRegistry * wp_plugin_get_registry (WpPlugin * self);
const WpPluginMetadata * wp_plugin_get_metadata (WpPlugin * self);


/**
 * WP_MODULE_INIT_SYMBOL: (skip)
 *
 * The linker symbol that serves as an entry point in modules
 */
#define WP_MODULE_INIT_SYMBOL wireplumber__module_init

/**
 * WP_MODULE_DEFINE: (skip)
 *
 * A convenience macro to register modules in C/C++.
 * A module can contain multiple plugins, which are meant to be registered
 * with WP_PLUGIN_REGISTER in the place of @plugin_reg
 *
 * Example usage:
 * |[
 * WP_MODULE_DEFINE (
 *   WP_PLUGIN_REGISTER (
 *     MY_PLUGIN_TYPE,
 *     WP_PLUGIN_RANK_PLATFORM_OVERRIDE,
 *     "myplugin",
 *     "A custom policy plugin for Awesome Platform",
 *     "George Kiagiadakis <george.kiagiadakis@collabora.com>",
 *     "LGPL-2.1-or-later",
 *     "3.0.1",
 *     "https://awesome-platform.example"
 *   );
 *   WP_PLUGIN_REGISTER (
 *     SECONDARY_PLUGIN_TYPE,
 *     WP_PLUGIN_RANK_PLATFORM_OVERRIDE - 1,
 *     "secondaryplugin",
 *     "A secondary policy plugin for Awesome Platform",
 *     "George Kiagiadakis <george.kiagiadakis@collabora.com>",
 *     "LGPL-2.1-or-later",
 *     "3.0.1",
 *     "https://awesome-platform.example"
 *   );
 * )
 * ]|
 */
#define WP_MODULE_DEFINE(plugin_reg) \
  G_MODULE_EXPORT void \
  WP_MODULE_INIT_SYMBOL (WpPluginRegistry * registry) \
  { \
    plugin_reg; \
  }

/**
 * WP_PLUGIN_REGISTER: (skip)
 *
 * A convenience macro to register plugins in C/C++.
 * See WP_MODULE_DEFINE() for a usage example.
 * See wp_plugin_registry_register() for a description of the parameters.
 */
#define WP_PLUGIN_REGISTER(gtype_, rank_, name_, description_, author_, license_, version_, origin_) \
  G_STMT_START \
    static const WpPluginMetadata plugin_metadata = { \
      .gtype = gtype_, \
      .rank = rank_, \
      .name = name_, \
      .description = description_, \
      .author = author_, \
      .license = license_, \
      .version = version_, \
      .origin = origin_ \
    }; \
    wp_plugin_registry_register_with_metadata (registry, &plugin_metadata, \
        sizeof (plugin_metadata)); \
  G_STMT_END


G_END_DECLS

#endif
