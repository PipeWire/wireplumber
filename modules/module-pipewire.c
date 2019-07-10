/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * module-pipewire provides basic integration between wireplumber and pipewire.
 * It provides the pipewire core and remote, connects to pipewire and provides
 * the most primitive implementations of WpEndpoint and WpEndpointLink
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

void remote_endpoint_init (WpCore * core, struct pw_core * pw_core,
    struct pw_remote * remote);
void simple_endpoint_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data);
void simple_endpoint_link_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data);

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  WpRemotePipewire *rp;
  struct pw_core *pw_core;
  struct pw_remote *pw_remote;

  /* Make sure the remote pipewire is valid */
  rp = wp_core_get_global (core, WP_GLOBAL_REMOTE_PIPEWIRE);
  if (!rp) {
    g_critical ("module-pipewire cannot be loaded without a registered "
        "WpRemotePipewire object");
    return;
  }

  /* Init remoted endpoint */
  g_object_get (rp, "pw-core", &pw_core, "pw-remote", &pw_remote, NULL);
  remote_endpoint_init (core, pw_core, pw_remote);

  /* Register simple-endpoint and simple-endpoint-link */
  wp_factory_new (core, "pipewire-simple-endpoint",
      simple_endpoint_factory);
  wp_factory_new (core, "pipewire-simple-endpoint-link",
      simple_endpoint_link_factory);
}
