/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CONFIG_POLICY_H__
#define __WIREPLUMBER_CONFIG_POLICY_H__

#include <wp/wp.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE (WpConfigPolicy, wp_config_policy, WP, CONFIG_POLICY, WpPolicy)

WpConfigPolicy *wp_config_policy_new (WpConfiguration *config);

G_END_DECLS

#endif
