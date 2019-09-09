/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: Exported
 *
 * An exported object is an object that is implemented in the local
 * wireplumber process but is exported to pipewire so that it is
 * visible to other clients through the registry.
 *
 * In PipeWire, an object is normally exported with pw_remote_export() or
 * by instantiating a "client-something" object on the server and using
 * the interface of that "client" object to control the exported global
 * object. In both cases, a proxy is created, which is in total control
 * of the object. If the proxy is destroyed, all associated objects
 * - the local exported object, the "client" object and the remote global
 * object - are destroyed.
 */

#include "exported.h"
#include "private.h"

typedef struct _WpExportedPrivate WpExportedPrivate;
struct _WpExportedPrivate
{
  GWeakRef core;
  GTask *task;
};

enum {
  PROP_0,
  PROP_CORE,
  PROP_PROXY,
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpExported, wp_exported, G_TYPE_OBJECT)

static void
wp_exported_init (WpExported * self)
{
  WpExportedPrivate *priv = wp_exported_get_instance_private (self);

  g_weak_ref_init (&priv->core, NULL);
}

static void
wp_exported_finalize (GObject * object)
{
  WpExportedPrivate *priv =
      wp_exported_get_instance_private (WP_EXPORTED (object));

  g_debug ("%s:%p destroyed", G_OBJECT_TYPE_NAME (object), object);

  g_weak_ref_clear (&priv->core);

  G_OBJECT_CLASS (wp_exported_parent_class)->finalize (object);
}

static void
wp_exported_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpExportedPrivate *priv =
      wp_exported_get_instance_private (WP_EXPORTED (object));

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
wp_exported_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpExportedPrivate *priv =
      wp_exported_get_instance_private (WP_EXPORTED (object));

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&priv->core));
    break;
  case PROP_PROXY:
    g_value_take_object (value, wp_exported_get_proxy (WP_EXPORTED (object)));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_exported_class_init (WpExportedClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_exported_finalize;
  object_class->get_property = wp_exported_get_property;
  object_class->set_property = wp_exported_set_property;

  /* Install the properties */

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The WpCore", WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROXY,
      g_param_spec_pointer ("proxy", "proxy", "The controlling proxy object",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

/**
 * wp_exported_get_core:
 * @self: the exported object
 *
 * Returns: (transfer full): the core that owns this exported object
 */
WpCore *
wp_exported_get_core (WpExported * self)
{
  WpExportedPrivate *priv;

  g_return_val_if_fail (WP_IS_EXPORTED (self), NULL);

  priv = wp_exported_get_instance_private (self);
  return g_weak_ref_get (&priv->core);
}

void
wp_exported_export (WpExported * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  WpExportedPrivate *priv;

  g_return_if_fail (WP_IS_EXPORTED (self));
  g_return_if_fail (WP_EXPORTED_GET_CLASS (self)->export);

  priv = wp_exported_get_instance_private (self);
  priv->task = g_task_new (self, cancellable, callback, user_data);

  WP_EXPORTED_GET_CLASS (self)->export (self);
}

gboolean
wp_exported_export_finish (WpExported * self,
    GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_EXPORTED (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);

  return g_task_propagate_boolean (G_TASK (res), error);
}

void
wp_exported_notify_export_done (WpExported * self, GError * error)
{
  WpExportedPrivate *priv;
  g_autoptr (WpCore) core = NULL;

  g_return_if_fail (WP_IS_EXPORTED (self));

  priv = wp_exported_get_instance_private (self);
  g_return_if_fail (priv->task);

  g_task_return_boolean (priv->task, TRUE);
  g_clear_object (&priv->task);

  core = g_weak_ref_get (&priv->core);
  if (core)
    wp_core_register_object (core, g_object_ref (self));
}

void
wp_exported_unexport (WpExported * self)
{
  WpExportedPrivate *priv;
  g_autoptr (WpCore) core = NULL;

  g_return_if_fail (WP_IS_EXPORTED (self));

  priv = wp_exported_get_instance_private (self);
  g_return_if_fail (!priv->task);

  if (WP_EXPORTED_GET_CLASS (self)->unexport)
    WP_EXPORTED_GET_CLASS (self)->unexport (self);

  core = g_weak_ref_get (&priv->core);
  if (core)
    wp_core_remove_object (core, self);
}

/**
 * wp_exported_get_proxy:
 * @self: the exported object
 *
 * Returns: (transfer full): the proxy that controls the export to pipewire
 */
WpProxy *
wp_exported_get_proxy (WpExported * self)
{
  g_return_val_if_fail (WP_IS_EXPORTED (self), NULL);
  g_return_val_if_fail (WP_EXPORTED_GET_CLASS (self)->get_proxy, NULL);

  return WP_EXPORTED_GET_CLASS (self)->get_proxy (self);
}
