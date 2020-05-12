/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include "module-config-policy/context.h"

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  WpConfigPolicyContext *ctx = wp_config_policy_context_new (core);
  wp_module_set_destroy_callback (module, g_object_unref, ctx);
}
