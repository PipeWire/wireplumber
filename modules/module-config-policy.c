/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include "module-config-policy/config-policy.h"

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);

  /* Create and register the config policy */
  WpConfigPolicy *cp = wp_config_policy_new (config);
  wp_policy_register (WP_POLICY (cp), core);
}
