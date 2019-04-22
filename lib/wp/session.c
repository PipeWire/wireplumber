/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "session.h"
#include "wpenums.h"

enum {
  PROP_0,
  PROP_DIRECTION
};

typedef struct
{
  WpSessionDirection direction;
} WpSessionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (WpSession, wp_session, WP_TYPE_OBJECT)

static void
wp_session_init (WpSession * self)
{
}

static void
wp_session_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (object));

  switch (property_id) {
  case PROP_DIRECTION:
    priv->direction = g_value_get_enum (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_session_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (WP_SESSION (object));

  switch (property_id) {
  case PROP_DIRECTION:
    g_value_set_enum (value, priv->direction);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_session_class_init (WpSessionClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->set_property = wp_session_set_property;
  object_class->get_property = wp_session_get_property;

  g_object_class_install_property (object_class, PROP_DIRECTION,
      g_param_spec_enum ("direction", "direction",
          "The media flow direction of the session",
          WP_TYPE_SESSION_DIRECTION, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpSessionDirection
wp_session_get_direction (WpSession * self)
{
  WpSessionDirection dir;
  g_object_get (self, "direction", &dir, NULL);
  return dir;
}
