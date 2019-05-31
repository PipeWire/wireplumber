/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WP_ERROR_H__
#define __WP_ERROR_H__

#include <glib.h>

G_BEGIN_DECLS

GQuark wp_domain_library_quark (void);
#define WP_DOMAIN_LIBRARY (wp_domain_library_quark ())

typedef enum {
  WP_LIBRARY_ERROR_INVARIANT,
  WP_LIBRARY_ERROR_INVALID_ARGUMENT,
  WP_LIBRARY_ERROR_OPERATION_FAILED,
} WpLibraryErrorEnum;

G_END_DECLS

#endif
