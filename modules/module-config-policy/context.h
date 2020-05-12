/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CONFIG_POLICY_CONTEXT_H__
#define __WIREPLUMBER_CONFIG_POLICY_CONTEXT_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_TYPE_CONFIG_POLICY_CONTEXT (wp_config_policy_context_get_type ())
G_DECLARE_FINAL_TYPE (WpConfigPolicyContext, wp_config_policy_context,
    WP, CONFIG_POLICY_CONTEXT, GObject);

WpConfigPolicyContext * wp_config_policy_context_new (WpCore *core);

G_END_DECLS

#endif
