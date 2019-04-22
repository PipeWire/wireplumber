/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef __WP_SESSION_H__
#define __WP_SESSION_H__

#include "object.h"

G_BEGIN_DECLS

#define WP_TYPE_SESSION (wp_session_get_type ())
G_DECLARE_DERIVABLE_TYPE (WpSession, wp_session, WP, SESSION, WpObject)

typedef enum {
  WP_SESSION_DIRECTION_INPUT,
  WP_SESSION_DIRECTION_OUTPUT
} WpSessionDirection;

struct _WpSessionClass
{
  WpObjectClass parent_class;
};

WpSessionDirection wp_session_get_direction (WpSession * session);

#define WP_SESSION_PW_PROP_MEDIA_CLASS "media.class"

G_END_DECLS

#endif
