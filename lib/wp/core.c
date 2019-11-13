/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "core.h"
#include "object-manager.h"
#include "proxy.h"
#include "wpenums.h"
#include "private.h"

#include <pipewire/pipewire.h>
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
  struct pw_core *pw_core;
  struct pw_remote *pw_remote;
  struct spa_hook remote_listener;

  /* remote core */
  struct pw_core_proxy *core_proxy;

  /* remote registry */
  struct pw_registry_proxy *registry_proxy;
  struct spa_hook registry_listener;

  GPtrArray *globals; // elementy-type: WpGlobal*
  GPtrArray *objects; // element-type: GObject*
  GPtrArray *object_managers; // element-type: WpObjectManager*
};

enum {
  PROP_0,
  PROP_CONTEXT,
  PROP_PROPERTIES,
  PROP_PW_CORE,
  PROP_PW_REMOTE,
  PROP_REMOTE_STATE,
};

enum {
  SIGNAL_REMOTE_STATE_CHANGED,
  NUM_SIGNALS
};

static guint32 signals[NUM_SIGNALS];

G_DEFINE_TYPE (WpCore, wp_core, G_TYPE_OBJECT)

static void
registry_global (void *data, uint32_t id, uint32_t permissions,
    uint32_t type, uint32_t version, const struct spa_dict *props)
{
  WpCore *self = WP_CORE (data);
  WpGlobal *global = NULL;
  guint i;

  g_return_if_fail (self->globals->len <= id ||
          g_ptr_array_index (self->globals, id) == NULL);

  g_debug ("registry global:%u perm:0x%x type:%u/%u (%s)",
      id, permissions, type, version,
      spa_debug_type_find_name (pw_type_info (), type));

  /* construct & store the global */
  global = wp_global_new ();
  global->id = id;
  global->type = type;
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

  g_debug ("registry global removed:%u type:%u/%u (%s)", id,
      global->type, global->version,
      spa_debug_type_find_name (pw_type_info (), global->type));

  /* notify object managers */
  for (i = 0; i < self->object_managers->len; i++) {
    WpObjectManager *om = g_ptr_array_index (self->object_managers, i);
    wp_object_manager_rm_global (om, id);
  }
}

static const struct pw_registry_proxy_events registry_proxy_events = {
  PW_VERSION_REGISTRY_PROXY_EVENTS,
  .global = registry_global,
  .global_remove = registry_global_remove,
};

static void
registry_init (WpCore *self)
{
  /* Get the core proxy */
  self->core_proxy = pw_remote_get_core_proxy (self->pw_remote);

  /* Registry */
  self->registry_proxy = pw_core_proxy_get_registry (self->core_proxy,
      PW_VERSION_REGISTRY_PROXY, 0);
  pw_registry_proxy_add_listener(self->registry_proxy, &self->registry_listener,
      &registry_proxy_events, self);
}

static void
on_remote_state_changed (void *d, enum pw_remote_state old_state,
    enum pw_remote_state new_state, const char *error)
{
  WpCore *self = d;
  GQuark detail;

  g_debug ("pipewire remote state changed, old:%s new:%s",
      pw_remote_state_as_string (old_state),
      pw_remote_state_as_string (new_state));

  /* Init the registry when connected */
  if (!self->registry_proxy && new_state == PW_REMOTE_STATE_CONNECTED)
    registry_init (self);

  /* enum pw_remote_state matches values with WpRemoteState */
  detail = g_quark_from_static_string (pw_remote_state_as_string (new_state));
  g_signal_emit (self, signals[SIGNAL_REMOTE_STATE_CHANGED], detail, new_state);
}

static const struct pw_remote_events remote_events = {
  PW_VERSION_REMOTE_EVENTS,
  .state_changed = on_remote_state_changed,
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
}

static void
wp_core_constructed (GObject *object)
{
  WpCore *self = WP_CORE (object);
  g_autoptr (GSource) source = NULL;
  struct pw_properties *p;

  source = wp_loop_source_new ();
  g_source_attach (source, self->context);

  p = self->properties ? wp_properties_to_pw_properties (self->properties) : NULL;
  self->pw_core = pw_core_new (WP_LOOP_SOURCE (source)->loop, p, 0);

  p = self->properties ? wp_properties_to_pw_properties (self->properties) : NULL;
  self->pw_remote = pw_remote_new (self->pw_core, p, 0);
  pw_remote_add_listener (self->pw_remote, &self->remote_listener,
      &remote_events, self);

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

  g_clear_pointer (&self->pw_remote, pw_remote_destroy);
  self->core_proxy= NULL;
  self->registry_proxy = NULL;
  g_clear_pointer (&self->pw_core, pw_core_destroy);
  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_pointer (&self->context, g_main_context_unref);

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
  case PROP_PW_CORE:
    g_value_set_pointer (value, self->pw_core);
    break;
  case PROP_PW_REMOTE:
    g_value_set_pointer (value, self->pw_remote);
    break;
  case PROP_REMOTE_STATE:
    g_value_set_enum (value, wp_core_get_remote_state (self, NULL));
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

  g_object_class_install_property (object_class, PROP_PW_CORE,
      g_param_spec_pointer ("pw-core", "pw-core", "The pipewire core",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_REMOTE,
      g_param_spec_pointer ("pw-remote", "pw-remote", "The pipewire remote",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_REMOTE_STATE,
      g_param_spec_enum ("remote-state", "remote-state",
          "The state of the remote",
          WP_TYPE_REMOTE_STATE, WP_REMOTE_STATE_UNCONNECTED,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /* Signals */
  signals[SIGNAL_REMOTE_STATE_CHANGED] = g_signal_new ("remote-state-changed",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_DETAILED | G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, WP_TYPE_REMOTE_STATE);
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

struct pw_core *
wp_core_get_pw_core (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->pw_core;
}

struct pw_remote *
wp_core_get_pw_remote (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->pw_remote;
}

static gboolean
connect_in_idle (WpCore *self)
{
  pw_remote_connect (self->pw_remote);
  return G_SOURCE_REMOVE;
}

gboolean
wp_core_connect (WpCore *self)
{
  g_autoptr (GSource) source = NULL;

  g_return_val_if_fail (WP_IS_CORE (self), FALSE);

  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) connect_in_idle, self, NULL);
  g_source_attach (source, self->context);

  return TRUE;
}

WpRemoteState
wp_core_get_remote_state (WpCore * self, const gchar ** error)
{
  g_return_val_if_fail (WP_IS_CORE (self), WP_REMOTE_STATE_UNCONNECTED);

  /* enum pw_remote_state matches values with WpRemoteState */
  G_STATIC_ASSERT ((gint) WP_REMOTE_STATE_ERROR == (gint) PW_REMOTE_STATE_ERROR);
  G_STATIC_ASSERT ((gint) WP_REMOTE_STATE_UNCONNECTED == (gint) PW_REMOTE_STATE_UNCONNECTED);
  G_STATIC_ASSERT ((gint) WP_REMOTE_STATE_CONNECTING == (gint) PW_REMOTE_STATE_CONNECTING);
  G_STATIC_ASSERT ((gint) WP_REMOTE_STATE_CONNECTED == (gint) PW_REMOTE_STATE_CONNECTED);

  return (WpRemoteState) pw_remote_get_state (self->pw_remote, error);
}

WpProxy *
wp_core_create_remote_object (WpCore *self,
    const gchar *factory_name, guint32 interface_type,
    guint32 interface_version, WpProperties * properties)
{
  struct pw_proxy *pw_proxy;

  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  g_return_val_if_fail (self->core_proxy, NULL);

  pw_proxy = pw_core_proxy_create_object (self->core_proxy, factory_name,
      interface_type, interface_version,
      properties ? wp_properties_peek_dict (properties) : NULL, 0);
  return wp_proxy_new_wrap (self, pw_proxy, interface_type,
      interface_version);
}

struct pw_core_proxy *
wp_core_get_pw_core_proxy (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->core_proxy;
}

struct pw_registry_proxy *
wp_core_get_pw_registry_proxy (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->registry_proxy;
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
