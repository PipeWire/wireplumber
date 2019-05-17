/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * module-pipewire provides basic integration between wireplumber and pipewire.
 * It provides the pipewire core and remote, connects to pipewire and provides
 * the most primitive implementations of WpEndpoint and WpEndpointLink
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

#include "module-pipewire/loop-source.h"

gpointer simple_endpoint_factory (WpFactory * factory, GType type,
    GVariant * properties);
gpointer simple_endpoint_link_factory (WpFactory * factory, GType type,
    GVariant * properties);

static gboolean
connect_in_idle (struct pw_remote *remote)
{
  pw_remote_connect (remote);
  return G_SOURCE_REMOVE;
}

static void
module_destroy (gpointer r)
{
  struct pw_remote *remote = r;
  struct pw_core *core = pw_remote_get_core (remote);

  pw_remote_destroy (remote);
  pw_core_destroy (core);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  GSource *source;
  struct pw_core *pw_core;
  struct pw_remote *pw_remote;

  pw_init (NULL, NULL);

  source = wp_loop_source_new ();
  g_source_attach (source, NULL);

  pw_core = pw_core_new (WP_LOOP_SOURCE (source)->loop, NULL, 0);
  wp_core_register_global (core, WP_GLOBAL_PW_CORE, pw_core, NULL);

  pw_remote = pw_remote_new (pw_core, NULL, 0);
  wp_core_register_global (core, WP_GLOBAL_PW_REMOTE, pw_remote, NULL);

  wp_module_set_destroy_callback (module, module_destroy, pw_remote);

  wp_core_register_factory (core, wp_factory_new (
          "pipewire-simple-endpoint", simple_endpoint_factory));
  wp_core_register_factory (core, wp_factory_new (
          "pipewire-simple-endpoint-link", simple_endpoint_link_factory));

  g_idle_add ((GSourceFunc) connect_in_idle, pw_remote);
}
