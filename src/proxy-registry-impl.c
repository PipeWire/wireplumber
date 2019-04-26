/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "proxy-registry-impl.h"
#include "plugin-registry-impl.h"

#include <wp/proxy.h>
#include <wp/plugin.h>

#include <pipewire/pipewire.h>
#include <pipewire/map.h>

struct _WpProxyRegistryImpl
{
  WpInterfaceImpl parent;

  struct pw_remote *remote;
  struct spa_hook remote_listener;

  struct pw_registry_proxy *reg_proxy;
  struct spa_hook reg_proxy_listener;

  struct pw_map globals;
  GArray *new_globals;
};

enum {
  PROP_0,
  PROP_REMOTE,
};

enum {
  SIGNAL_NEW_PROXY_AVAILABLE,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void wp_proxy_registry_impl_iface_init (WpProxyRegistryInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpProxyRegistryImpl, wp_proxy_registry_impl, WP_TYPE_INTERFACE_IMPL,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PROXY_REGISTRY, wp_proxy_registry_impl_iface_init);)

static gint
guint32_compare (const guint32 *a, const guint32 *b)
{
  return (gint) ((gint64)*a - (gint64)*b);
}

static gboolean
idle_notify_new_globals (gpointer data)
{
  WpProxyRegistryImpl *self = WP_PROXY_REGISTRY_IMPL (data);
  guint i;
  guint32 id;

  // TODO verify these globals still exist

  g_array_sort (self->new_globals, (GCompareFunc) guint32_compare);
  for (i = 0; i < self->new_globals->len; i++) {
    id = g_array_index (self->new_globals, guint32, i);
    g_signal_emit (self, signals[SIGNAL_NEW_PROXY_AVAILABLE], 0,
        pw_map_lookup (&self->globals, id));
  }
  g_array_remove_range (self->new_globals, 0, self->new_globals->len);

  return G_SOURCE_REMOVE;
}

static inline void
map_insert (struct pw_map *map, guint32 id, gpointer obj)
{
  size_t size = pw_map_get_size (map);
  while (id > size)
    pw_map_insert_at (map, size++, NULL);
  pw_map_insert_at (map, id, obj);
}

static void
registry_global (void * data, uint32_t id, uint32_t parent_id,
    uint32_t permissions, uint32_t type, uint32_t version,
    const struct spa_dict * props)
{
  WpProxyRegistryImpl *self = WP_PROXY_REGISTRY_IMPL (data);
  WpProxy *proxy;
  WpPluginRegistry *plugin_registry = NULL;
  WpObject *core = wp_interface_impl_get_object (WP_INTERFACE_IMPL (self));

  proxy = g_object_new (WP_TYPE_PROXY,
      "id", id,
      "parent-id", parent_id,
      "spa-type", type,
      "initial-properties", props,
      "core", core,
      NULL);
  map_insert (&self->globals, id, proxy);

  plugin_registry = wp_interface_impl_get_sibling (WP_INTERFACE_IMPL (self),
      WP_TYPE_PLUGIN_REGISTRY);
  wp_plugin_registry_impl_invoke (plugin_registry,
      wp_plugin_provide_interfaces, WP_OBJECT (proxy));

  /*
   * defer notifications until we return to the main loop;
   * this allows the pipewire event loop to finish emitting
   * all new available globals before we use them
   */
  if (self->new_globals->len == 0)
    g_idle_add (idle_notify_new_globals, self);
  g_array_append_val (self->new_globals, id);
}

static void
registry_global_remove (void * data, uint32_t id)
{
  WpProxyRegistryImpl *self = WP_PROXY_REGISTRY_IMPL (data);
  GObject *p = pw_map_lookup (&self->globals, id);
  g_object_unref (p);
  pw_map_insert_at (&self->globals, id, NULL);
}

static const struct pw_registry_proxy_events registry_events = {
  PW_VERSION_REGISTRY_PROXY_EVENTS,
  .global = registry_global,
  .global_remove = registry_global_remove,
};

static void
remote_state_changed (void * data, enum pw_remote_state old_state,
    enum pw_remote_state new_state, const char * error)
{
  WpProxyRegistryImpl *self = WP_PROXY_REGISTRY_IMPL (data);

  switch (new_state) {
  case PW_REMOTE_STATE_CONNECTED:
    self->reg_proxy = pw_core_proxy_get_registry (
        pw_remote_get_core_proxy (self->remote),
        PW_TYPE_INTERFACE_Registry, PW_VERSION_REGISTRY, 0);
    pw_registry_proxy_add_listener (self->reg_proxy,
        &self->reg_proxy_listener, &registry_events, self);
    break;

  case PW_REMOTE_STATE_UNCONNECTED:
    self->reg_proxy = NULL;
    break;

  default:
    break;
  }
}

static const struct pw_remote_events remote_events = {
  PW_VERSION_REMOTE_EVENTS,
  .state_changed = remote_state_changed,
};

static void
wp_proxy_registry_impl_init (WpProxyRegistryImpl * self)
{
  pw_map_init (&self->globals, 64, 64);
  self->new_globals = g_array_sized_new (FALSE, FALSE, sizeof (guint32), 64);
}

static void
wp_proxy_registry_impl_constructed (GObject * obj)
{
  WpProxyRegistryImpl *self = WP_PROXY_REGISTRY_IMPL (obj);

  pw_remote_add_listener (self->remote, &self->remote_listener, &remote_events,
      self);

  G_OBJECT_CLASS (wp_proxy_registry_impl_parent_class)->constructed (obj);
}

static void
wp_proxy_registry_impl_finalize (GObject * obj)
{
  WpProxyRegistryImpl *self = WP_PROXY_REGISTRY_IMPL (obj);

  pw_map_clear (&self->globals);
  g_array_unref (self->new_globals);

  G_OBJECT_CLASS (wp_proxy_registry_impl_parent_class)->finalize (obj);
}

static void
wp_proxy_registry_impl_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpProxyRegistryImpl *self = WP_PROXY_REGISTRY_IMPL (object);

  switch (property_id) {
  case PROP_REMOTE:
    self->remote = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_registry_impl_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpProxyRegistryImpl *self = WP_PROXY_REGISTRY_IMPL (object);

  switch (property_id) {
  case PROP_REMOTE:
    g_value_set_pointer (value, self->remote);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_registry_impl_class_init (WpProxyRegistryImplClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;

  object_class->constructed = wp_proxy_registry_impl_constructed;
  object_class->finalize = wp_proxy_registry_impl_finalize;
  object_class->get_property = wp_proxy_registry_impl_get_property;
  object_class->set_property = wp_proxy_registry_impl_set_property;

  g_object_class_install_property (object_class, PROP_REMOTE,
      g_param_spec_pointer ("remote", "remote",
          "The underlying struct pw_remote *",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_NEW_PROXY_AVAILABLE] = g_signal_new ("new-proxy-available",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_PROXY);
}

static WpProxy *
wp_proxy_registry_impl_get_proxy (WpProxyRegistry * r, guint32 global_id)
{
  WpProxyRegistryImpl *self = WP_PROXY_REGISTRY_IMPL (r);
  WpProxy *p = pw_map_lookup (&self->globals, global_id);
  if (p)
    g_object_ref (p);
  return p;
}

static struct pw_registry_proxy *
wp_proxy_registry_impl_get_pw_registry_proxy (WpProxyRegistry * r)
{
  WpProxyRegistryImpl *self = WP_PROXY_REGISTRY_IMPL (r);
  return self->reg_proxy;
}

static void
wp_proxy_registry_impl_iface_init (WpProxyRegistryInterface * iface)
{
  iface->get_proxy = wp_proxy_registry_impl_get_proxy;
  iface->get_pw_registry_proxy = wp_proxy_registry_impl_get_pw_registry_proxy;
}

WpProxyRegistryImpl *
wp_proxy_registry_impl_new (struct pw_remote * remote)
{
  return g_object_new (wp_proxy_registry_impl_get_type (), "remote", remote,
      NULL);
}

void
wp_proxy_registry_impl_unload (WpProxyRegistryImpl * self)
{
  size_t i, size = pw_map_get_size (&self->globals);
  for (i = 0; i < size; i++) {
    g_clear_object (&pw_map_get_item (&self->globals, i)->data);
  }
}
