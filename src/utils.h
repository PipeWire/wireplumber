/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WIREPLUMBER_UTILS_H__
#define __WIREPLUMBER_UTILS_H__

#include <glib.h>

G_BEGIN_DECLS

GQuark wp_domain_core_quark (void);
#define WP_DOMAIN_CORE (wp_domain_core_quark ())

enum WpCoreCode {
  WP_CODE_DISCONNECTED = 0,
  WP_CODE_INTERRUPTED,
  WP_CODE_OPERATION_FAILED,
  WP_CODE_INVALID_ARGUMENT,
  WP_CODE_REMOTE_ERROR,
};

G_END_DECLS

#endif
