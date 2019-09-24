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

void simple_endpoint_link_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data);

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  struct pw_core *pw_core = wp_core_get_pw_core (core);

  pw_module_load (pw_core, "libpipewire-module-client-device", NULL, NULL);
  pw_module_load (pw_core, "libpipewire-module-adapter", NULL, NULL);

  /* Register simple-endpoint-link */
  wp_factory_new (core, "pipewire-simple-endpoint-link",
      simple_endpoint_link_factory);
}
