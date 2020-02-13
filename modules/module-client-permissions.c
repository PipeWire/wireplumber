/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

static void
client_added (WpObjectManager * om, WpClient *client, gpointer data)
{
  g_autoptr (WpProperties) properties = NULL;
  const char *access;
  guint32 id = wp_proxy_get_bound_id (WP_PROXY (client));

  g_debug ("Client added: %d", id);

  properties = wp_proxy_get_properties (WP_PROXY (client));
  access = wp_properties_get (properties, PW_KEY_ACCESS);

  if (!g_strcmp0 (access, "flatpak") || !g_strcmp0 (access, "restricted")) {
    g_debug ("Granting full access to client %d", id);
    wp_client_update_permissions (client, 1, -1, PW_PERM_RWX);
  }
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  WpObjectManager *om;

  om = wp_object_manager_new ();
  wp_object_manager_add_interest (om, WP_TYPE_CLIENT, NULL,
      WP_PROXY_FEATURES_STANDARD);

  g_signal_connect (om, "object-added", (GCallback) client_added, NULL);

  wp_core_install_object_manager (core, om);
  wp_module_set_destroy_callback (module, g_object_unref, om);
}
