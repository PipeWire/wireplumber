/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "core.h"

enum {
  SIGNAL_GLOBAL_ADDED,
  SIGNAL_GLOBAL_REMOVED,
  NUM_SIGNALS
};

static guint32 signals[NUM_SIGNALS];

struct global_object
{
  GQuark key;
  gpointer object;
  GDestroyNotify destroy;
};

struct _WpCore
{
  GObject parent;
  GPtrArray *global_objects;
};

G_DEFINE_TYPE (WpCore, wp_core, G_TYPE_OBJECT)

static void
free_global_object (gpointer p)
{
  struct global_object *g = p;

  /* Destroy the object */
  if (g->destroy)
    g->destroy(g->object);

  g_slice_free (struct global_object, p);
}

static void
wp_core_init (WpCore * self)
{
  self->global_objects = g_ptr_array_new_with_free_func (free_global_object);
}

static void
wp_core_dispose (GObject * obj)
{
  WpCore *self = WP_CORE (obj);
  g_autoptr (GPtrArray) global_objects;
  struct global_object *global;

  global_objects = g_steal_pointer (&self->global_objects);

  /* Remove and emit the removed signal for all globals */
  while (global_objects->len > 0) {
    global = g_ptr_array_steal_index_fast (global_objects,
        global_objects->len - 1);
    g_signal_emit (self, signals[SIGNAL_GLOBAL_REMOVED], global->key,
        global->key, global->object);
    free_global_object (global);
  }

  G_OBJECT_CLASS (wp_core_parent_class)->dispose (obj);
}

static void
wp_core_finalize (GObject * obj)
{
  g_debug ("WpCore destroyed");

  G_OBJECT_CLASS (wp_core_parent_class)->finalize (obj);
}

static void
wp_core_class_init (WpCoreClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->dispose = wp_core_dispose;
  object_class->finalize = wp_core_finalize;

  signals[SIGNAL_GLOBAL_ADDED] = g_signal_new ("global-added",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_DETAILED | G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);

  signals[SIGNAL_GLOBAL_REMOVED] = g_signal_new ("global-removed",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_DETAILED | G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);
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
 *    associated with @key; if multiple globals with the same key exist, the
 *    first one found is returned
 */
gpointer
wp_core_get_global (WpCore * self, GQuark key)
{
  gint i;
  struct global_object *global;

  g_return_val_if_fail (WP_IS_CORE (self), NULL);

  if (G_UNLIKELY (!self->global_objects))
    return NULL;

  for (i = 0; i < self->global_objects->len; i++) {
    global = g_ptr_array_index (self->global_objects, i);
    if (global->key == key)
      return global->object;
  }

  return NULL;
}

/**
 * wp_core_foreach_global: (method)
 * @self: the core
 * @callback: the function to call for each global object
 * @user_data: data to passs to @callback
 *
 * Calls @callback for every global object registered
 */
void
wp_core_foreach_global (WpCore * self, WpCoreForeachGlobalFunc callback,
    gpointer user_data)
{
  gint i;
  struct global_object *global;

  g_return_if_fail (WP_IS_CORE (self));

  if (G_UNLIKELY (!self->global_objects))
    return;

  for (i = 0; i < self->global_objects->len; i++) {
    global = g_ptr_array_index (self->global_objects, i);
    if (!callback (global->key, global->object, user_data))
      break;
  }
}

/**
 * wp_core_register_global: (method)
 * @self: the core
 * @key: the key for this global
 * @obj: (transfer full): the global object to attach
 * @destroy_obj: the destroy function for @obj
 *
 * Registers @obj as a global object associated with @key
 */
void
wp_core_register_global (WpCore * self, GQuark key, gpointer obj,
    GDestroyNotify destroy_obj)
{
  struct global_object *global;

  g_return_if_fail (WP_IS_CORE(self));

  if (G_UNLIKELY (!self->global_objects)) {
    if (destroy_obj)
      destroy_obj (obj);
    return;
  }

  global = g_slice_new0 (struct global_object);
  global->key = key;
  global->object = obj;
  global->destroy = destroy_obj;
  g_ptr_array_add (self->global_objects, global);

  g_signal_emit (self, signals[SIGNAL_GLOBAL_ADDED], key, key, obj);
}

/**
 * wp_core_remove_global: (method)
 * @self: the core
 * @key: the key for this global
 * @obj: (nullable): a pointer to the global object to match, if there are
 *     multiple ones with the same key
 *
 * Detaches and unrefs the specified global from this core
 */
void
wp_core_remove_global (WpCore * self, GQuark key, gpointer obj)
{
  gint i;
  struct global_object *global;

  g_return_if_fail (WP_IS_CORE (self));

  if (G_UNLIKELY (!self->global_objects))
    return;

  for (i = 0; i < self->global_objects->len; i++) {
    global = g_ptr_array_index (self->global_objects, i);
    if (global->key == key && (!obj || global->object == obj))
      break;
  }

  if (i < self->global_objects->len) {
    global = g_ptr_array_steal_index_fast (self->global_objects, i);

    g_signal_emit (self, signals[SIGNAL_GLOBAL_REMOVED], key,
        key, global->object);

    free_global_object (global);
  }
}

G_DEFINE_QUARK (endpoint, wp_global_endpoint)
G_DEFINE_QUARK (factory, wp_global_factory)
G_DEFINE_QUARK (module, wp_global_module)
G_DEFINE_QUARK (policy-manager, wp_global_policy_manager)
G_DEFINE_QUARK (remote-pipewire, wp_global_remote_pipewire)
