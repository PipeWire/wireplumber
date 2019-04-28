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
  PROP_DIRECTION,
  PROP_MEDIA_CLASS,
};

typedef struct
{
  WpSessionDirection direction;
  gchar media_class[41];
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
  case PROP_MEDIA_CLASS:
    strncpy (priv->media_class, g_value_get_string (value), 40);
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
  case PROP_MEDIA_CLASS:
    g_value_set_string (value, priv->media_class);
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

  g_object_class_install_property (object_class, PROP_MEDIA_CLASS,
      g_param_spec_string ("media-class", "media-class",
          "The media class of the session", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpSessionDirection
wp_session_get_direction (WpSession * self)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return priv->direction;
}

const gchar *
wp_session_get_media_class (WpSession * self)
{
  WpSessionPrivate *priv = wp_session_get_instance_private (self);
  return priv->media_class;
}
