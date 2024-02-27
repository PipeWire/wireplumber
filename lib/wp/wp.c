/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "wp.h"
#include <pipewire/pipewire.h>
#include <libintl.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp")

/*!
 * \defgroup wp Library Initialization
 * \{
 */

/*!
 * \brief Initializes WirePlumber and PipeWire underneath.
 *
 * \em flags can modify which parts are initialized, in cases where you want
 * to handle part of this initialization externally.
 *
 * \param flags initialization flags
 */
void
wp_init (WpInitFlags flags)
{
  /* Initialize the logging system */
  wp_log_init (flags);

  wp_info ("WirePlumber " WIREPLUMBER_VERSION " initializing");

  if (flags & WP_INIT_PIPEWIRE)
    pw_init (NULL, NULL);

  if (flags & WP_INIT_SPA_TYPES)
    wp_spa_dynamic_type_init ();

  bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

  /* ensure WpProxy subclasses are loaded, which is needed to be able
    to autodetect the GType of proxies created through wp_proxy_new_global() */
  g_type_ensure (WP_TYPE_CLIENT);
  g_type_ensure (WP_TYPE_DEVICE);
  g_type_ensure (WP_TYPE_LINK);
  g_type_ensure (WP_TYPE_METADATA);
  g_type_ensure (WP_TYPE_NODE);
  g_type_ensure (WP_TYPE_PORT);
  g_type_ensure (WP_TYPE_FACTORY);
}

/*!
 * \brief Gets the WirePlumber library version
 * \returns WirePlumber library version
 *
 * \since 0.4.12
 */
const char *
wp_get_library_version (void)
{
  return WIREPLUMBER_VERSION;
}

/*!
 * \brief Gets the WirePlumber library API version
 * \returns WirePlumber library API version
 *
 * \since 0.4.12
 */
const char *
wp_get_library_api_version (void)
{
  return WIREPLUMBER_API_VERSION;
}

/*! \} */
