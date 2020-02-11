/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "core.h"
#include "wp.h"
#include "private.h"

#include <pipewire/pipewire.h>
#include <pipewire/impl.h>

#include <spa/utils/result.h>
#include <spa/debug/types.h>

/*
 * Integration between the PipeWire main loop and GMainLoop
 */

#define WP_LOOP_SOURCE(x) ((WpLoopSource *) x)

typedef struct _WpLoopSource WpLoopSource;
struct _WpLoopSource
{
  GSource parent;
  struct pw_loop *loop;
};

static gboolean
wp_loop_source_dispatch (GSource * s, GSourceFunc callback, gpointer user_data)
{
  int result;

  pw_loop_enter (WP_LOOP_SOURCE(s)->loop);
  result = pw_loop_iterate (WP_LOOP_SOURCE(s)->loop, 0);
  pw_loop_leave (WP_LOOP_SOURCE(s)->loop);

  if (G_UNLIKELY (result < 0))
    g_warning ("pw_loop_iterate failed: %s", spa_strerror (result));

  return G_SOURCE_CONTINUE;
}

static void
wp_loop_source_finalize (GSource * s)
{
  pw_loop_destroy (WP_LOOP_SOURCE(s)->loop);
}

static GSourceFuncs source_funcs = {
  NULL,
  NULL,
  wp_loop_source_dispatch,
  wp_loop_source_finalize
};

static GSource *
wp_loop_source_new (void)
{
  GSource *s = g_source_new (&source_funcs, sizeof (WpLoopSource));
  WP_LOOP_SOURCE(s)->loop = pw_loop_new (NULL);

  g_source_add_unix_fd (s,
      pw_loop_get_fd (WP_LOOP_SOURCE(s)->loop),
      G_IO_IN | G_IO_ERR | G_IO_HUP);

  return (GSource *) s;
}

/**
 * WpCore
 */

struct _WpCore
{
  GObject parent;

  /* main loop integration */
  GMainContext *context;

  /* extra properties */
  WpProperties *properties;

  /* pipewire main objects */
  struct pw_context *pw_context;
  struct pw_core *pw_core;
  struct pw_registry *pw_registry;

  /* pipewire main listeners */
  struct spa_hook core_listener;
  struct spa_hook proxy_core_listener;
  struct spa_hook registry_listener;

  GPtrArray *globals; // elementy-type: WpGlobal*
  GPtrArray *objects; // element-type: GObject*
  GPtrArray *object_managers; // element-type: WpObjectManager*
  GHashTable *async_tasks; // <int seq, GTask*>
};

enum {
  PROP_0,
  PROP_CONTEXT,
  PROP_PROPERTIES,
  PROP_PW_CONTEXT,
  PROP_PW_CORE,
};

enum {
  SIGNAL_CONNECTED,
  SIGNAL_DISCONNECTED,
  NUM_SIGNALS
};

static guint32 signals[NUM_SIGNALS];


G_DEFINE_TYPE (WpCore, wp_core, G_TYPE_OBJECT)

static void
registry_global (void *data, uint32_t id, uint32_t permissions,
    const char *type, uint32_t version, const struct spa_dict *props)
{
  WpCore *self = WP_CORE (data);
  WpGlobal *global = NULL;
  guint i;

  g_return_if_fail (self->globals->len <= id ||
          g_ptr_array_index (self->globals, id) == NULL);

  g_debug ("registry global:%u perm:0x%x type:%s version:%u",
      id, permissions, type, version);

  /* construct & store the global */
  global = wp_global_new ();
  global->id = id;
  global->type = g_strdup (type);
  global->version = version;
  global->permissions = permissions;
  global->properties = wp_properties_new_copy_dict (props);

  if (self->globals->len <= id)
    g_ptr_array_set_size (self->globals, id + 1);

  g_ptr_array_index (self->globals, id) = global;

  /* notify object managers */
  for (i = 0; i < self->object_managers->len; i++) {
    WpObjectManager *om = g_ptr_array_index (self->object_managers, i);
    wp_object_manager_add_global (om, global);
  }
}

static void
registry_global_remove (void *data, uint32_t id)
{
  WpCore *self = WP_CORE (data);
  g_autoptr (WpGlobal) global = NULL;
  guint i;

  global = g_steal_pointer (&g_ptr_array_index (self->globals, id));

  g_debug ("registry global removed:%u type:%s/%u", id,
      global->type, global->version);

  /* notify object managers */
  for (i = 0; i < self->object_managers->len; i++) {
    WpObjectManager *om = g_ptr_array_index (self->object_managers, i);
    wp_object_manager_rm_global (om, id);
  }
}

static const struct pw_registry_events registry_events = {
  PW_VERSION_REGISTRY_EVENTS,
  .global = registry_global,
  .global_remove = registry_global_remove,
};

static void
core_done (void *data, uint32_t id, int seq)
{
  WpCore *self = WP_CORE (data);
  g_autoptr (GTask) task = NULL;

  g_hash_table_steal_extended (self->async_tasks, GINT_TO_POINTER (seq), NULL,
      (gpointer *) &task);
  if (task)
    g_task_return_boolean (task, TRUE);
}

static const struct pw_core_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .done = core_done,
};

static void
proxy_core_destroy (void *data)
{
  WpCore *self = WP_CORE (data);
  self->pw_core = NULL;

  /* Emit the disconnected signal */
  g_signal_emit (self, signals[SIGNAL_DISCONNECTED], 0);
}

static const struct pw_proxy_events proxy_core_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_core_destroy,
};

/* wrapper around wp_global_unref because
  the WpGlobal pointers in self->globals can be NULL */
static inline void
free_global (WpGlobal * g)
{
  if (g)
    wp_global_unref (g);
}

static void
wp_core_init (WpCore * self)
{
  self->globals = g_ptr_array_new_with_free_func ((GDestroyNotify) free_global);
  self->objects = g_ptr_array_new_with_free_func (g_object_unref);
  self->object_managers = g_ptr_array_new ();
  self->async_tasks = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);
}

static void
wp_core_constructed (GObject *object)
{
  WpCore *self = WP_CORE (object);
  g_autoptr (GSource) source = NULL;
  struct pw_properties *p = NULL;

  /* loop */
  source = wp_loop_source_new ();
  g_source_attach (source, self->context);

  /* context */
  p = self->properties ? wp_properties_to_pw_properties (self->properties) : NULL;
  self->pw_context = pw_context_new (WP_LOOP_SOURCE(source)->loop, p, 0);

  G_OBJECT_CLASS (wp_core_parent_class)->constructed (object);
}

static void object_manager_destroyed (gpointer data, GObject * om);

static void
wp_core_dispose (GObject * obj)
{
  WpCore *self = WP_CORE (obj);

  /* remove pipewire globals */
  {
    g_autoptr (GPtrArray) objlist = g_steal_pointer (&self->globals);

    while (objlist->len > 0) {
      guint i;
      g_autoptr (WpGlobal) global = g_ptr_array_steal_index_fast (objlist,
          objlist->len - 1);

      if (!global)
        continue;

      for (i = 0; i < self->object_managers->len; i++) {
        WpObjectManager *om = g_ptr_array_index (self->object_managers, i);
        wp_object_manager_rm_global (om, global->id);
      }
    }
  }

  /* remove all the registered objects
     this will normally also destroy the object managers, eventually, since
     they are normally ref'ed by modules, which are registered objects */
  {
    g_autoptr (GPtrArray) objlist = g_steal_pointer (&self->objects);

    while (objlist->len > 0) {
      guint i;
      g_autoptr (GObject) object = g_ptr_array_steal_index_fast (objlist,
          objlist->len - 1);

      for (i = 0; i < self->object_managers->len; i++) {
        WpObjectManager *om = g_ptr_array_index (self->object_managers, i);
        wp_object_manager_rm_object (om, object);
      }
    }
  }

  /* in case there are any object managers left,
     remove the weak ref on them and let them be... */
  {
    g_autoptr (GPtrArray) object_mgrs;
    GObject *om;

    object_mgrs = g_steal_pointer (&self->object_managers);

    while (object_mgrs->len > 0) {
      om = g_ptr_array_steal_index_fast (object_mgrs, object_mgrs->len - 1);
      g_object_weak_unref (om, object_manager_destroyed, self);
    }
  }

  G_OBJECT_CLASS (wp_core_parent_class)->dispose (obj);
}

static void
wp_core_finalize (GObject * obj)
{
  WpCore *self = WP_CORE (obj);

  wp_core_disconnect (self);

  g_clear_pointer (&self->pw_context, pw_context_destroy);

  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_pointer (&self->context, g_main_context_unref);
  g_clear_pointer (&self->async_tasks, g_hash_table_unref);

  g_debug ("WpCore destroyed");

  G_OBJECT_CLASS (wp_core_parent_class)->finalize (obj);
}

static void
wp_core_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpCore *self = WP_CORE (object);

  switch (property_id) {
  case PROP_CONTEXT:
    g_value_set_boxed (value, self->context);
    break;
  case PROP_PROPERTIES:
    g_value_set_boxed (value, self->properties);
    break;
  case PROP_PW_CONTEXT:
    g_value_set_pointer (value, self->pw_context);
    break;
  case PROP_PW_CORE:
    g_value_set_pointer (value, self->pw_core);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_core_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpCore *self = WP_CORE (object);

  switch (property_id) {
  case PROP_CONTEXT:
    self->context = g_value_dup_boxed (value);
    break;
  case PROP_PROPERTIES:
    self->properties = g_value_dup_boxed (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_core_class_init (WpCoreClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  pw_init (NULL, NULL);

  object_class->constructed = wp_core_constructed;
  object_class->dispose = wp_core_dispose;
  object_class->finalize = wp_core_finalize;
  object_class->get_property = wp_core_get_property;
  object_class->set_property = wp_core_set_property;

  g_object_class_install_property (object_class, PROP_CONTEXT,
      g_param_spec_boxed ("context", "context", "A GMainContext to attach to",
          G_TYPE_MAIN_CONTEXT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties", "Extra properties",
          WP_TYPE_PROPERTIES,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_CONTEXT,
      g_param_spec_pointer ("pw-context", "pw-context", "The pipewire context",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_CORE,
      g_param_spec_pointer ("pw-core", "pw-core", "The pipewire core",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /* Signals */
  signals[SIGNAL_CONNECTED] = g_signal_new ("connected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
  signals[SIGNAL_DISCONNECTED] = g_signal_new ("disconnected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);

  /* ensure WpProxy subclasses are loaded, which is needed to be able
    to autodetect the GType of proxies created through wp_proxy_new_global() */
  g_type_ensure (WP_TYPE_CLIENT);
  g_type_ensure (WP_TYPE_DEVICE);
  g_type_ensure (WP_TYPE_PROXY_ENDPOINT);
  g_type_ensure (WP_TYPE_LINK);
  g_type_ensure (WP_TYPE_NODE);
  g_type_ensure (WP_TYPE_PORT);
  g_type_ensure (WP_TYPE_SESSION);
}

WpCore *
wp_core_new (GMainContext *context, WpProperties * properties)
{
  return g_object_new (WP_TYPE_CORE,
      "context", context,
      "properties", properties,
      NULL);
}

GMainContext *
wp_core_get_context (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->context;
}

struct pw_context *
wp_core_get_pw_context (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->pw_context;
}

struct pw_core *
wp_core_get_pw_core (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->pw_core;
}

struct pw_registry *
wp_core_get_pw_registry (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->pw_registry;
}

gboolean
wp_core_connect (WpCore *self)
{
  struct pw_properties *p = NULL;

  g_return_val_if_fail (WP_IS_CORE (self), FALSE);

  /* Don't do anything if core is already connected */
  if (self->pw_core)
    return TRUE;

  g_return_val_if_fail (!self->pw_registry, FALSE);

  /* Connect */
  p = self->properties ? wp_properties_to_pw_properties (self->properties) : NULL;
  self->pw_core = pw_context_connect (self->pw_context, p, 0);
  if (!self->pw_core)
    return FALSE;

  /* Add the core listeners */
  pw_core_add_listener (self->pw_core, &self->core_listener, &core_events, self);
  pw_proxy_add_listener((struct pw_proxy*)self->pw_core,
      &self->proxy_core_listener, &proxy_core_events, self);

  /* Add the registry listener */
  self->pw_registry = pw_core_get_registry (self->pw_core,
      PW_VERSION_REGISTRY, 0);
  pw_registry_add_listener(self->pw_registry, &self->registry_listener,
      &registry_events, self);

  /* Emit the connected signal */
  g_signal_emit (self, signals[SIGNAL_CONNECTED], 0);

  return TRUE;
}

void
wp_core_disconnect (WpCore *self)
{
  if (self->pw_registry) {
    pw_proxy_destroy ((struct pw_proxy *)self->pw_registry);
    self->pw_registry = NULL;
  }

  g_clear_pointer (&self->pw_core, pw_core_disconnect);

  /* Emit the disconnected signal */
  g_signal_emit (self, signals[SIGNAL_DISCONNECTED], 0);
}

gboolean
wp_core_is_connected (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), FALSE);
  return self->pw_core != NULL;
}

guint
wp_core_idle_add (WpCore * self, GSourceFunc function, gpointer data,
    GDestroyNotify destroy)
{
  g_autoptr (GSource) source = NULL;

  g_return_val_if_fail (WP_IS_CORE (self), 0);

  source = g_idle_source_new ();
  g_source_set_callback (source, function, data, destroy);
  g_source_attach (source, self->context);
  return g_source_get_id (source);
}

gboolean
wp_core_sync (WpCore * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  g_autoptr (GTask) task = NULL;
  int seq;

  g_return_val_if_fail (WP_IS_CORE (self), FALSE);

  task = g_task_new (self, cancellable, callback, user_data);

  if (G_UNLIKELY (!self->pw_core)) {
    g_warn_if_reached ();
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT, "No pipewire core");
    return FALSE;
  }

  seq = pw_core_sync (self->pw_core, 0, 0);
  if (G_UNLIKELY (seq < 0)) {
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED, "pw_core_sync failed: %s",
        g_strerror (-seq));
    return FALSE;
  }

  g_hash_table_insert (self->async_tasks, GINT_TO_POINTER (seq),
      g_steal_pointer (&task));
  return TRUE;
}

gboolean
wp_core_sync_finish (WpCore * self, GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_CORE (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);

  return g_task_propagate_boolean (G_TASK (res), error);
}

/**
 * wp_core_find_object: (skip)
 * @self: the core
 * @func: (scope call): a function that takes the object being searched
 *   as the first argument and @data as the second. it should return TRUE if
 *   the object is found or FALSE otherwise
 * @data: the second argument to @func
 *
 * Finds a registered object
 *
 * Returns: (transfer full) (type GObject *) (nullable): the registered object
 *   or NULL if not found
 */
gpointer
wp_core_find_object (WpCore * self, GEqualFunc func, gconstpointer data)
{
  GObject *object;
  guint i;

  g_return_val_if_fail (WP_IS_CORE (self), NULL);

  /* prevent bad things when called from within _dispose() */
  if (G_UNLIKELY (!self->objects))
    return NULL;

  for (i = 0; i < self->objects->len; i++) {
    object = g_ptr_array_index (self->objects, i);
    if (func (object, data))
      return g_object_ref (object);
  }

  return NULL;
}

/**
 * wp_core_register_object: (skip)
 * @self: the core
 * @obj: (transfer full) (type GObject*): the object to register
 *
 * Registers @obj with the core, making it appear on #WpObjectManager
 *   instances as well. The core will also maintain a ref to that object
 *   until it is removed.
 */
void
wp_core_register_object (WpCore * self, gpointer obj)
{
  guint i;

  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (G_IS_OBJECT (obj));

  /* prevent bad things when called from within _dispose() */
  if (G_UNLIKELY (!self->objects)) {
    g_object_unref (obj);
    return;
  }

  g_ptr_array_add (self->objects, obj);

  /* notify object managers */
  for (i = 0; i < self->object_managers->len; i++) {
    WpObjectManager *om = g_ptr_array_index (self->object_managers, i);
    wp_object_manager_add_object (om, obj);
  }
}

/**
 * wp_core_remove_object: (skip)
 * @self: the core
 * @obj: (transfer none) (type GObject*): a pointer to the object to remove
 *
 * Detaches and unrefs the specified object from this core
 */
void
wp_core_remove_object (WpCore * self, gpointer obj)
{
  guint i;

  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (G_IS_OBJECT (obj));

  /* prevent bad things when called from within _dispose() */
  if (G_UNLIKELY (!self->objects))
    return;

  /* notify object managers */
  for (i = 0; i < self->object_managers->len; i++) {
    WpObjectManager *om = g_ptr_array_index (self->object_managers, i);
    wp_object_manager_rm_object (om, obj);
  }

  g_ptr_array_remove_fast (self->objects, obj);
}

static void
object_manager_destroyed (gpointer data, GObject * om)
{
  WpCore *self = WP_CORE (data);
  g_ptr_array_remove_fast (self->object_managers, om);
}

/**
 * wp_core_install_object_manager: (method)
 * @self: the core
 * @om: (transfer none): a #WpObjectManager
 *
 * Installs the object manager on this core, activating its internal management
 * engine. This will immediately emit signals about objects added on @om
 * if objects that the @om is interested in were in existence already.
 */
void
wp_core_install_object_manager (WpCore * self, WpObjectManager * om)
{
  guint i;

  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (WP_IS_OBJECT_MANAGER (om));

  g_object_weak_ref (G_OBJECT (om), object_manager_destroyed, self);
  g_ptr_array_add (self->object_managers, om);
  g_object_set (om, "core", self, NULL);

  /* add pre-existing objects to the object manager,
     in case it's interested in them */
  for (i = 0; i < self->globals->len; i++) {
    WpGlobal *g = g_ptr_array_index (self->globals, i);
    /* check if null because the globals array can have gaps */
    if (g)
      wp_object_manager_add_global (om, g);
  }
  for (i = 0; i < self->objects->len; i++) {
    GObject *o = g_ptr_array_index (self->objects, i);
    wp_object_manager_add_object (om, o);
  }
}
