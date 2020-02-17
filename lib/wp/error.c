/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: Error
 *
 * Error domain and codes for #GError
 */

#include "error.h"

/**
 * wp_domain_library_quark:
 */
G_DEFINE_QUARK (wireplumber-library, wp_domain_library);
