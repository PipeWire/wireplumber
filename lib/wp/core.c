/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "core.h"

struct _WpCore
{
  GObject parent;
  GData *global_objects;
};

G_DEFINE_TYPE (WpCore, wp_core, G_TYPE_OBJECT)

static void
wp_core_init (WpCore * self)
{
  g_datalist_init (&self->global_objects);
}

static void
wp_core_finalize (GObject * obj)
{
  WpCore *self = WP_CORE (obj);

  g_datalist_clear (&self->global_objects);

  G_OBJECT_CLASS (wp_core_parent_class)->finalize (obj);
}

static void
wp_core_class_init (WpCoreClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  object_class->finalize = wp_core_finalize;
}

WpCore *
wp_core_new (void)
{
  return g_object_new (WP_TYPE_CORE, NULL);
}

/**
 * wp_core_get_global: (method)
 * @self: the core
 * @key: the key of the global
 *
 * Returns: (type GObject*) (nullable) (transfer none): the global object
 *    implementing @type
 */
gpointer
wp_core_get_global (WpCore * self, GQuark key)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);

  return g_datalist_id_get_data (&self->global_objects, key);
}

/**
 * wp_core_register_global: (method)
 * @self: the core
 * @key: the key for this global
 * @obj: (transfer full): the global object to attach
 * @destroy_obj: the destroy function for @obj
 *
 * Returns: TRUE one success, FALSE if the global already exists
 */
gboolean
wp_core_register_global (WpCore * self, GQuark key, gpointer obj,
    GDestroyNotify destroy_obj)
{
  gpointer other = NULL;

  g_return_val_if_fail (WP_IS_CORE(self), FALSE);

  if ((other = g_datalist_id_get_data (&self->global_objects, key)) != NULL) {
    g_warning ("cannot register global '%s': it already exists",
        g_quark_to_string (key));
    return FALSE;
  }

  g_datalist_id_set_data_full (&self->global_objects, key, obj, destroy_obj);
  return TRUE;
}

/**
 * wp_core_remove_global: (method)
 * @self: the core
 * @key: the key for this global
 *
 * Detaches and unrefs the specified global from this core
 */
void
wp_core_remove_global (WpCore * self, GQuark key)
{
  g_return_if_fail (WP_IS_CORE (self));

  g_datalist_id_remove_data (&self->global_objects, key);
}

G_DEFINE_QUARK (WP_GLOBAL_PW_CORE, wp_global_pw_core)
G_DEFINE_QUARK (WP_GLIBAL_PW_REMOTE, wp_global_pw_remote)
