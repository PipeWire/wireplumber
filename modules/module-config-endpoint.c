/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>

#include <wp/wp.h>

#include "module-config-endpoint/context.h"

struct module_data
{
  WpConfigEndpointContext *ctx;
};

static void
module_destroy (gpointer d)
{
  struct module_data *data = d;
  g_clear_object (&data->ctx);
  g_slice_free (struct module_data, data);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  struct module_data *data;

  /* Create the module data */
  data = g_slice_new0 (struct module_data);
  data->ctx = wp_config_endpoint_context_new (core);

  /* Set the module destroy callback */
  wp_module_set_destroy_callback (module, module_destroy, data);
}
