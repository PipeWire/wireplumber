/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CONFIG_POLICY_CONTEXT_H__
#define __WIREPLUMBER_CONFIG_POLICY_CONTEXT_H__

#include <wp/wp.h>

G_BEGIN_DECLS

static const guint CONFIG_POLICY_CONTEXT_ID_NONE = G_MAXUINT;

G_DECLARE_FINAL_TYPE (WpConfigPolicyContext, wp_config_policy_context, WP,
    CONFIG_POLICY_CONTEXT, GObject);

WpConfigPolicyContext *wp_config_policy_context_new (WpCore *core,
    const char *config_path);
WpEndpoint *wp_config_policy_context_add_endpoint (WpConfigPolicyContext *self,
    const char *name, const char *media_class, guint direction,
    WpProperties *props, const char *role, guint streams,
    WpEndpointLink **link);
void wp_config_policy_context_remove_endpoint (WpConfigPolicyContext *self,
    WpEndpoint *ep);

G_END_DECLS

#endif
