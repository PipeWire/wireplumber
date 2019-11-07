/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "core.h"
#include "proxy.h"
#include "wpenums.h"
#include "private.h"

#include <pipewire/pipewire.h>
#include <spa/utils/result.h>

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

  /* local proxies */
  GHashTable *proxies;
  GHashTable *default_features;

  /* local global objects */
  GPtrArray *global_objects;
};

struct global_object
{
  GQuark key;
  gpointer object;
  GDestroyNotify destroy;
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
  SIGNAL_GLOBAL_ADDED,
  SIGNAL_GLOBAL_REMOVED,
  SIGNAL_REMOTE_GLOBAL_ADDED,
  SIGNAL_REMOTE_GLOBAL_REMOVED,
  NUM_SIGNALS
};

static guint32 signals[NUM_SIGNALS];

G_DEFINE_TYPE (WpCore, wp_core, G_TYPE_OBJECT)

static void
on_proxy_ready (GObject * obj, GAsyncResult * res, gpointer data)
{
  WpCore *self = WP_CORE (data);
  WpProxy *proxy = WP_PROXY (obj);
  g_autoptr (GError) error = NULL;

  if (!wp_proxy_augment_finish (proxy, res, &error)) {
    g_warning ("Failed to augment WpProxy (%p): %s", obj, error->message);
    return;
  }

  g_signal_emit (self, signals[SIGNAL_REMOTE_GLOBAL_ADDED],
      wp_proxy_get_interface_quark (proxy), proxy);
}

static void
registry_global (void *data, uint32_t id, uint32_t permissions,
    uint32_t type, uint32_t version, const struct spa_dict *props)
{
  WpCore *self = WP_CORE (data);
  WpProxy *proxy;
  WpProxyFeatures features;
  g_autoptr (WpProperties) properties = wp_properties_new_copy_dict (props);

  g_return_if_fail (!g_hash_table_contains (self->proxies, GUINT_TO_POINTER (id)));

  /* construct & store WpProxy */
  proxy = wp_proxy_new_global (self, id, permissions, properties,
      type, version);
  g_hash_table_insert (self->proxies, GUINT_TO_POINTER (id), proxy);

  g_debug ("registry global:%u perm:0x%x type:%u/%u -> %s:%p",
      id, permissions, type, version, G_OBJECT_TYPE_NAME (proxy), proxy);

  /* augment */
  features = GPOINTER_TO_UINT (g_hash_table_lookup (self->default_features,
          GUINT_TO_POINTER (G_TYPE_FROM_INSTANCE (proxy))));
  wp_proxy_augment (proxy, features, NULL, on_proxy_ready, self);
}

static void
registry_global_remove (void *data, uint32_t id)
{
  WpCore *self = WP_CORE (data);
  g_autoptr (WpProxy) proxy = NULL;

  g_hash_table_steal_extended (self->proxies, GUINT_TO_POINTER (id), NULL,
      (gpointer *) &proxy);

  g_debug ("registry global removed: %u (%p)", id, proxy);

  if (proxy)
    g_signal_emit (data, signals[SIGNAL_REMOTE_GLOBAL_REMOVED],
        wp_proxy_get_interface_quark (proxy), proxy);
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
  self->proxies = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      g_object_unref);
  self->default_features = g_hash_table_new (g_direct_hash, g_direct_equal);
  self->global_objects = g_ptr_array_new_with_free_func (free_global_object);
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
  WpCore *self = WP_CORE (obj);

  g_clear_pointer (&self->proxies, g_hash_table_unref);
  g_clear_pointer (&self->default_features, g_hash_table_unref);
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

  signals[SIGNAL_GLOBAL_ADDED] = g_signal_new ("global-added",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_DETAILED | G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);

  signals[SIGNAL_GLOBAL_REMOVED] = g_signal_new ("global-removed",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_DETAILED | G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);

  signals[SIGNAL_REMOTE_GLOBAL_ADDED] = g_signal_new ("remote-global-added",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_DETAILED | G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, WP_TYPE_PROXY);

  signals[SIGNAL_REMOTE_GLOBAL_REMOVED] = g_signal_new ("remote-global-removed",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_DETAILED | G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, WP_TYPE_PROXY);
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

void
wp_core_set_default_proxy_features (WpCore * self,
    GType proxy_type, WpProxyFeatures features)
{
  g_return_if_fail (WP_IS_CORE (self));

  g_hash_table_insert (self->default_features, GUINT_TO_POINTER (proxy_type),
      GUINT_TO_POINTER (features));
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
 * @callback: (scope call): the function to call for each global object
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
