/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "core.h"
#include "utils.h"

#include <pipewire/pipewire.h>

static GOptionEntry entries[] =
{
  { NULL }
};

gint
main (gint argc, gchar **argv)
{
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpCore) core = NULL;
  gint ret = 0;

  context = g_option_context_new ("- PipeWire Session/Policy Manager");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    goto out;

  pw_init (NULL, NULL);

  core = wp_core_get_instance ();
  wp_core_run (core, &error);

out:
  if (error) {
    ret = error->code;
    if (error->domain != WP_DOMAIN_CORE)
      ret += 100;
    g_message ("exit code %d; %s", ret, error->message);
  }
  return ret;
}
