/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include "module-config-static-objects/context.h"

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  WpConfigStaticObjectsContext *ctx = wp_config_static_objects_context_new (module);
  wp_plugin_register (WP_PLUGIN (ctx));
}
