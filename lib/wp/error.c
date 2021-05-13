/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "error.h"

/**
 * wp_domain_library_quark:
 *
 * @file error.c
 *
 * @section error_section Error codes
 *
 * Error domain and codes for
 * <a href="https://developer.gnome.org/glib/stable/glib-Error-Reporting.html#GError">
 * GError</a>
 */
G_DEFINE_QUARK (wireplumber-library, wp_domain_library);
