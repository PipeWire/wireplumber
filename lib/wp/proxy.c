/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "proxy.h"
#include <pipewire/pipewire.h>

struct _WpProxy
{
  GObject parent;

  struct pw_proxy *proxy;
  guint32 id;
  guint32 parent_id;
  guint32 type;
  const gchar *type_string;
};

enum {
  PROP_0,
  PROP_PROXY,
  PROP_ID,
  PROP_PARENT_ID,
  PROP_SPA_TYPE,
  PROP_SPA_TYPE_STRING,
};

G_DEFINE_TYPE (WpProxy, wp_proxy, G_TYPE_OBJECT);

static void
wp_proxy_init (WpProxy * self)
{
}

static void
wp_proxy_constructed (GObject * object)
{
  WpProxy *self = WP_PROXY (object);
  const struct spa_type_info *info = pw_type_info ();

  while (info->type) {
    if (info->type == self->type) {
      self->type_string = info->name;
      break;
    }
    info++;
  }

  G_OBJECT_CLASS (wp_proxy_parent_class)->constructed (object);
}

static void
wp_proxy_finalize (GObject * object)
{
  WpProxy *self = WP_PROXY (object);

  G_OBJECT_CLASS (wp_proxy_parent_class)->finalize (object);
}

static void
wp_proxy_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpProxy *self = WP_PROXY (object);

  switch (property_id) {
  case PROP_PROXY:
    self->proxy = g_value_get_pointer (value);
    break;
  case PROP_ID:
    self->id = g_value_get_uint (value);
    break;
  case PROP_PARENT_ID:
    self->parent_id = g_value_get_uint (value);
    break;
  case PROP_SPA_TYPE:
    self->type = g_value_get_uint (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpProxy *self = WP_PROXY (object);

  switch (property_id) {
  case PROP_PROXY:
    g_value_set_pointer (value, self->proxy);
    break;
  case PROP_ID:
    g_value_set_uint (value, self->id);
    break;
  case PROP_PARENT_ID:
    g_value_set_uint (value, self->parent_id);
    break;
  case PROP_SPA_TYPE:
    g_value_set_uint (value, self->type);
    break;
  case PROP_SPA_TYPE_STRING:
    g_value_set_string (value, wp_proxy_get_spa_type_string (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_class_init (WpProxyClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_proxy_constructed;
  object_class->finalize = wp_proxy_finalize;
  object_class->get_property = wp_proxy_get_property;
  object_class->set_property = wp_proxy_set_property;

  g_object_class_install_property (object_class, PROP_PROXY,
      g_param_spec_pointer ("proxy", "proxy",
          "The underlying struct pw_proxy *",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ID,
      g_param_spec_uint ("id", "id",
          "The global ID of the object", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PARENT_ID,
      g_param_spec_uint ("parent-id", "parent-id",
          "The global ID of the parent object", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SPA_TYPE,
      g_param_spec_uint ("spa-type", "spa-type",
          "The SPA type of the object", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SPA_TYPE_STRING,
      g_param_spec_string ("spa-type-string", "spa-type-string",
          "The string representation of the SPA type of the object", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

guint32
wp_proxy_get_id (WpProxy * self)
{
  return self->id;
}

guint32
wp_proxy_get_parent_id (WpProxy * self)
{
  return self->parent_id;
}

guint32
wp_proxy_get_spa_type (WpProxy * self)
{
  return self->type;
}

const gchar *
wp_proxy_get_spa_type_string (WpProxy * self)
{
  return self->type_string;
}
