/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include "module-config-endpoint/context.h"

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  WpConfigEndpointContext *ctx = wp_config_endpoint_context_new (core);
  wp_module_set_destroy_callback (module, g_object_unref, ctx);
}
