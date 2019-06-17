/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote.h"
#include "wpenums.h"

enum {
  PROP_0,
  PROP_CORE,
  PROP_STATE,
  PROP_ERROR_MESSAGE,
};

enum {
  SIGNAL_STATE_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _WpRemotePrivate WpRemotePrivate;
struct _WpRemotePrivate
{
  GWeakRef core;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpRemote, wp_remote, G_TYPE_OBJECT)

static void
wp_remote_init (WpRemote *self)
{
  WpRemotePrivate *priv = wp_remote_get_instance_private (self);
  g_weak_ref_init (&priv->core, NULL);
}

static void
wp_remote_finalize (GObject *object)
{
  WpRemotePrivate *priv = wp_remote_get_instance_private (WP_REMOTE (object));

  g_weak_ref_clear (&priv->core);

  G_OBJECT_CLASS (wp_remote_parent_class)->finalize (object);
}

static void
wp_remote_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpRemotePrivate *priv = wp_remote_get_instance_private (WP_REMOTE (object));

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&priv->core, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_remote_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpRemotePrivate *priv = wp_remote_get_instance_private (WP_REMOTE (object));

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&priv->core));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_remote_notify (GObject *object, GParamSpec *param)
{
  if (!g_strcmp0 (param->name, "state")) {
    WpRemoteState state;
    GParamSpecEnum *param_enum = (GParamSpecEnum *) param;
    GEnumValue *value;
    GQuark detail;

    g_object_get (object, "state", &state, NULL);
    value = g_enum_get_value (param_enum->enum_class, state);
    detail = g_quark_from_static_string (value->value_nick);
    g_signal_emit (object, signals[SIGNAL_STATE_CHANGED], detail, state);
  }

  if (G_OBJECT_CLASS (wp_remote_parent_class)->notify)
    G_OBJECT_CLASS (wp_remote_parent_class)->notify (object, param);
}

static void
wp_remote_class_init (WpRemoteClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_remote_finalize;
  object_class->set_property = wp_remote_set_property;
  object_class->get_property = wp_remote_get_property;
  object_class->notify = wp_remote_notify;

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The wireplumber core",
          WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_STATE,
      g_param_spec_enum ("state", "state", "The state of the remote",
          WP_TYPE_REMOTE_STATE, WP_REMOTE_STATE_UNCONNECTED,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ERROR_MESSAGE,
      g_param_spec_string ("error-message", "error-message",
          "The last error message of the remote", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_STATE_CHANGED] = g_signal_new ("state-changed",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_DETAILED | G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, WP_TYPE_REMOTE_STATE);
}

WpCore *
wp_remote_get_core (WpRemote *self)
{
  WpRemotePrivate *priv = wp_remote_get_instance_private (self);
  return g_weak_ref_get (&priv->core);
}

gboolean
wp_remote_connect (WpRemote *self)
{
  if (WP_REMOTE_GET_CLASS (self)->connect)
    return WP_REMOTE_GET_CLASS (self)->connect (self);
  return FALSE;
}
