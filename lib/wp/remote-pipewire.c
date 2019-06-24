/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote-pipewire.h"
#include <pipewire/pipewire.h>

/**
 * Integration between the PipeWire main loop and GMainLoop
 */

#define WP_LOOP_SOURCE(x) ((WpLoopSource *) x)

G_DEFINE_QUARK (node, signal_detail_node)
G_DEFINE_QUARK (port, signal_detail_port)

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
 * WpRemotePipewire
 */

struct _WpRemotePipewire
{
  WpRemote parent;

  struct pw_core *core;
  struct pw_remote *remote;
  struct spa_hook remote_listener;
  struct pw_core_proxy *core_proxy;

  /* Registry */
  struct pw_registry_proxy *registry_proxy;
  struct spa_hook registry_listener;

  GMainContext *context;
};

enum {
  PROP_0,
  PROP_STATE,
  PROP_ERROR_MESSAGE,
  PROP_PW_CORE,
  PROP_PW_REMOTE,
  PROP_CONTEXT,
};

enum
{
  SIGNAL_GLOBAL_ADDED,
  SIGNAL_GLOBAL_REMOVED,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (WpRemotePipewire, wp_remote_pipewire, WP_TYPE_REMOTE)

static void
registry_global(void *data, uint32_t id, uint32_t parent_id,
		uint32_t permissions, uint32_t type, uint32_t version,
		const struct spa_dict *props)
{
  switch (type) {
  case PW_TYPE_INTERFACE_Node:
    g_signal_emit (data, signals[SIGNAL_GLOBAL_ADDED],
        signal_detail_node_quark (), id, parent_id, props);
    break;
  case PW_TYPE_INTERFACE_Port:
    g_signal_emit (data, signals[SIGNAL_GLOBAL_ADDED],
        signal_detail_port_quark (), id, parent_id, props);
    break;
  default:
    break;
  }
}

static void
registry_global_remove (void *data, uint32_t id)
{
  g_signal_emit (data, signals[SIGNAL_GLOBAL_REMOVED], 0, id);
}

static const struct pw_registry_proxy_events registry_proxy_events = {
  PW_VERSION_REGISTRY_PROXY_EVENTS,
  .global = registry_global,
  .global_remove = registry_global_remove,
};

static void
registry_init (WpRemotePipewire *self)
{
  /* Get the core proxy */
  self->core_proxy = pw_remote_get_core_proxy (self->remote);

  /* Registry */
  self->registry_proxy = pw_core_proxy_get_registry (self->core_proxy,
      PW_TYPE_INTERFACE_Registry, PW_VERSION_REGISTRY, 0);
  pw_registry_proxy_add_listener(self->registry_proxy, &self->registry_listener,
      &registry_proxy_events, self);
}

static void
on_remote_state_changed (void *d, enum pw_remote_state old_state,
    enum pw_remote_state new_state, const char *error)
{
  WpRemotePipewire *self = d;

  g_debug ("pipewire remote state changed, old:%s new:%s",
      pw_remote_state_as_string (old_state),
      pw_remote_state_as_string (new_state));

  /* Init the registry when connected */
  if (!self->registry_proxy && new_state == PW_REMOTE_STATE_CONNECTED)
    registry_init (self);

  g_object_notify (G_OBJECT (self), "state");
}

static const struct pw_remote_events remote_events = {
  PW_VERSION_REMOTE_EVENTS,
  .state_changed = on_remote_state_changed,
};

static void
wp_remote_pipewire_init (WpRemotePipewire *self)
{
}

static void
wp_remote_pipewire_constructed (GObject *object)
{
  WpRemotePipewire *self = WP_REMOTE_PIPEWIRE (object);
  GSource *source;

  source = wp_loop_source_new ();
  g_source_attach (source, self->context);

  self->core = pw_core_new (WP_LOOP_SOURCE (source)->loop, NULL, 0);

  self->remote = pw_remote_new (self->core, NULL, 0);
  pw_remote_add_listener (self->remote, &self->remote_listener, &remote_events,
      self);

  G_OBJECT_CLASS (wp_remote_pipewire_parent_class)->constructed (object);
}

static void
wp_remote_pipewire_finalize (GObject *object)
{
  WpRemotePipewire *self = WP_REMOTE_PIPEWIRE (object);

  pw_remote_destroy (self->remote);
  pw_core_destroy (self->core);
  g_clear_pointer (&self->context, g_main_context_unref);
  self->core_proxy= NULL;
  self->registry_proxy = NULL;

  G_OBJECT_CLASS (wp_remote_pipewire_parent_class)->finalize (object);
}

static void
wp_remote_pipewire_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpRemotePipewire *self = WP_REMOTE_PIPEWIRE (object);

  switch (property_id) {
  case PROP_STATE:
    /* enum pw_remote_state matches values with WpRemoteState */
    g_value_set_enum (value, pw_remote_get_state (self->remote, NULL));
    break;
  case PROP_ERROR_MESSAGE:
  {
    const gchar *msg;
    (void) pw_remote_get_state (self->remote, &msg);
    g_value_set_string (value, msg);
    break;
  }
  case PROP_PW_CORE:
    g_value_set_pointer (value, self->core);
    break;
  case PROP_PW_REMOTE:
    g_value_set_pointer (value, self->remote);
    break;
  case PROP_CONTEXT:
    g_value_set_boxed (value, self->context);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_remote_pipewire_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpRemotePipewire *self = WP_REMOTE_PIPEWIRE (object);

  switch (property_id) {
  case PROP_CONTEXT:
    self->context = g_value_dup_boxed (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static gboolean
connect_in_idle (WpRemotePipewire *self)
{
  pw_remote_connect (self->remote);
  return G_SOURCE_REMOVE;
}

static gboolean
wp_remote_pipewire_connect (WpRemote *remote)
{
  WpRemotePipewire *self = WP_REMOTE_PIPEWIRE (remote);
  g_autoptr (GSource) source;

  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) connect_in_idle, self, NULL);
  g_source_attach (source, self->context);

  return TRUE;
}

static void
wp_remote_pipewire_class_init (WpRemotePipewireClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpRemoteClass *remote_class = (WpRemoteClass *) klass;

  pw_init (NULL, NULL);

  object_class->constructed = wp_remote_pipewire_constructed;
  object_class->finalize = wp_remote_pipewire_finalize;
  object_class->get_property = wp_remote_pipewire_get_property;
  object_class->set_property = wp_remote_pipewire_set_property;

  remote_class->connect = wp_remote_pipewire_connect;

  g_object_class_override_property (object_class, PROP_STATE, "state");
  g_object_class_override_property (object_class, PROP_ERROR_MESSAGE,
      "error-message");

  g_object_class_install_property (object_class, PROP_CONTEXT,
      g_param_spec_boxed ("context", "context", "A GMainContext to attach to",
          G_TYPE_MAIN_CONTEXT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_CORE,
      g_param_spec_pointer ("pw-core", "pw-core", "The pipewire core",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_REMOTE,
      g_param_spec_pointer ("pw-remote", "pw-remote", "The pipewire remote",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /* Signals */
  signals[SIGNAL_GLOBAL_ADDED] = g_signal_new ("global-added",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_DETAILED | G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT,
      G_TYPE_POINTER);
  signals[SIGNAL_GLOBAL_REMOVED] = g_signal_new ("global-removed",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

WpRemote *
wp_remote_pipewire_new (WpCore *core, GMainContext *context)
{
  WpRemote *remote;

  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  remote = g_object_new (WP_TYPE_REMOTE_PIPEWIRE,
      "core", core,
      "context", context,
      NULL);
  wp_core_register_global (core, WP_GLOBAL_REMOTE_PIPEWIRE,
      g_object_ref (remote), g_object_unref);

  return remote;
}

gpointer
wp_remote_pipewire_proxy_bind (WpRemotePipewire *self, guint global_id,
    guint global_type)
{
  g_return_val_if_fail (WP_IS_REMOTE_PIPEWIRE(self), NULL);
  g_return_val_if_fail (self->registry_proxy, NULL);

  return pw_registry_proxy_bind (self->registry_proxy, global_id, global_type,
      0, 0);
}

gpointer
wp_remote_pipewire_create_object (WpRemotePipewire *self,
    const char *factory_name, guint global_type, gconstpointer props)
{
  g_return_val_if_fail (WP_IS_REMOTE_PIPEWIRE(self), NULL);
  g_return_val_if_fail (self->core_proxy, NULL);

  return pw_core_proxy_create_object(self->core_proxy, factory_name,
      global_type, 0, props, 0);
}
