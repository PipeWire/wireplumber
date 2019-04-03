/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#define G_LOG_DOMAIN "wireplumber"

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
