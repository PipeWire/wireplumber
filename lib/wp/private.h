/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_H__
#define __WIREPLUMBER_PRIVATE_H__

G_BEGIN_DECLS

/* core */

struct pw_core_proxy;
struct pw_registry_proxy;

struct pw_core_proxy * wp_core_get_pw_core_proxy (WpCore * self);
struct pw_registry_proxy * wp_core_get_pw_registry_proxy (WpCore * self);

enum {
  WP_CORE_FOREACH_GLOBAL_DONE = FALSE,
  WP_CORE_FOREACH_GLOBAL_CONTINUE = TRUE,
};

typedef gboolean (*WpCoreForeachGlobalFunc) (GQuark key, gpointer global,
    gpointer user_data);

gpointer wp_core_get_global (WpCore * self, GQuark key);
void wp_core_foreach_global (WpCore * self, WpCoreForeachGlobalFunc callback,
    gpointer user_data);

void wp_core_register_global (WpCore * self, GQuark key, gpointer obj,
    GDestroyNotify destroy_obj);
void wp_core_remove_global (WpCore * self, GQuark key, gpointer obj);

#define WP_GLOBAL_ENDPOINT (wp_global_endpoint_quark ())
GQuark wp_global_endpoint_quark (void);

#define WP_GLOBAL_FACTORY (wp_global_factory_quark ())
GQuark wp_global_factory_quark (void);

#define WP_GLOBAL_MODULE (wp_global_module_quark ())
GQuark wp_global_module_quark (void);

#define WP_GLOBAL_POLICY_MANAGER (wp_global_policy_manager_quark ())
GQuark wp_global_policy_manager_quark (void);

/* proxy */

void wp_proxy_set_feature_ready (WpProxy * self, WpProxyFeatures feature);
void wp_proxy_augment_error (WpProxy * self, GError * error);

void wp_proxy_register_async_task (WpProxy * self, int seq, GTask * task);
GTask * wp_proxy_find_async_task (WpProxy * self, int seq, gboolean steal);

G_END_DECLS

#endif
