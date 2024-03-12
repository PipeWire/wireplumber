/* WirePlumber
 *
 * Copyright © 2019-2024 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "registry.h"
#include "object-manager.h"
#include "log.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-registry")

/*
 * WpRegistry:
 *
 * The registry keeps track of registered objects on the wireplumber core.
 * There are 3 kinds of registered objects:
 *
 * 1) PipeWire global objects, which live in another process.
 *
 *    These objects are represented by a WpGlobal with the
 *    WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY flag set. They appear when
 *    the registry_global() event is fired and are removed by
 *    registry_global_remove(). These objects do not have an associated
 *    WpProxy, unless there is at least one WpObjectManager that is interested
 *    in them. In this case, a WpProxy is constructed and it is owned by the
 *    WpGlobal until the global is removed by the registry_global_remove() event.
 *
 * 2) PipeWire global objects, which were constructed by this process, either
 *    by calling into a remove factory (see wp_node_new_from_factory()) or
 *    by exporting a local object (WpImplSession etc...).
 *
 *    These objects are also represented by a WpGlobal, which may however be
 *    constructed before they appear on the registry. The associated WpProxy
 *    calls into wp_registry_prepare_new_global() at the time it receives
 *    the 'bound' event and creates a global that has the
 *    WP_GLOBAL_FLAG_OWNED_BY_PROXY flag enabled. As the flag name suggests,
 *    these globals are "owned" by the WpProxy and the WpGlobal has no ref
 *    on the WpProxy itself. This allows destroying the proxy in client code
 *    by dropping its last reference.
 *
 *    Normally, these global objects also appear on the pipewire registry. When
 *    this happens, the WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY flag is also added
 *    and that keeps an additional reference on the global (both flags must
 *    be dropped before the WpGlobal is destroyed).
 *
 *    In some cases, such an object might appear first on the registry and
 *    then receive the 'bound' event. In order to handle this situation, globals
 *    are not advertised immediately when they appear on the registry, but
 *    they are added on a tmp_globals list instead, which is emptied on the
 *    next core sync. In all cases, the proxy 'bound' and the registry 'global'
 *    events will be fired in the same sync cycle, so we can catch a late
 *    'bound' event and still associate the proxy with the WpGlobal before
 *    object managers are notified about the existence of this global.
 *
 * 3) WirePlumber global objects (WpModule, WpPlugin, WpSiFactory).
 *
 *    These are local objects that have nothing to do with PipeWire. They do not
 *    have a global id and they are also not subclasses of WpProxy. The registry
 *    always owns a reference on them, so that they are kept alive for as long
 *    as the WpCore is alive.
 */

static void
object_manager_destroyed (gpointer data, GObject * om)
{
  WpRegistry *self = data;
  g_ptr_array_remove_fast (self->object_managers, om);
}

/* find the subclass of WpPipewireGloabl that can handle
   the given pipewire interface type of the given version */
static inline GType
find_proxy_instance_type (const char * type, guint32 version)
{
  g_autofree GType *children;
  guint n_children;

  children = g_type_children (WP_TYPE_GLOBAL_PROXY, &n_children);

  for (guint i = 0; i < n_children; i++) {
    WpProxyClass *klass = (WpProxyClass *) g_type_class_ref (children[i]);
    if (g_strcmp0 (klass->pw_iface_type, type) == 0 &&
        klass->pw_iface_version == version) {
      g_type_class_unref (klass);
      return children[i];
    }

    g_type_class_unref (klass);
  }

  return WP_TYPE_GLOBAL_PROXY;
}

/* called by the registry when a global appears */
static void
registry_global (void *data, uint32_t id, uint32_t permissions,
    const char *type, uint32_t version, const struct spa_dict *props)
{
  WpRegistry *self = data;
  GType gtype = find_proxy_instance_type (type, version);

  wp_debug_object (wp_registry_get_core (self),
      "global:%u perm:0x%x type:%s/%u -> %s",
      id, permissions, type, version, g_type_name (gtype));

  wp_registry_prepare_new_global (self, id, permissions,
      WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY, gtype, NULL, props, NULL);
}

/* called by the registry when a global is removed */
static void
registry_global_remove (void *data, uint32_t id)
{
  WpRegistry *self = data;
  WpGlobal *global = NULL;

  if (id < self->globals->len)
    global = g_ptr_array_index (self->globals, id);

  /* if not found, look in the tmp_globals, as it may still not be exposed */
  if (!global) {
    for (guint i = 0; i < self->tmp_globals->len; i++) {
      WpGlobal *g = g_ptr_array_index (self->tmp_globals, i);
      if (g->id == id) {
        global = g;
        break;
      }
    }
  }

  g_return_if_fail (global &&
      global->flags & WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY);

  wp_debug_object (wp_registry_get_core (self),
      "global removed:%u type:%s", id, g_type_name (global->type));

  wp_global_rm_flag (global, WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY);
}

static const struct pw_registry_events registry_events = {
  PW_VERSION_REGISTRY_EVENTS,
  .global = registry_global,
  .global_remove = registry_global_remove,
};

void
wp_registry_init (WpRegistry *self)
{
  self->globals =
      g_ptr_array_new_with_free_func ((GDestroyNotify) wp_global_unref);
  self->tmp_globals =
      g_ptr_array_new_with_free_func ((GDestroyNotify) wp_global_unref);
  self->objects = g_ptr_array_new_with_free_func (g_object_unref);
  self->object_managers = g_ptr_array_new ();
  self->features = g_ptr_array_new_with_free_func (g_free);
}

void
wp_registry_clear (WpRegistry *self)
{
  wp_registry_detach (self);
  g_clear_pointer (&self->globals, g_ptr_array_unref);
  g_clear_pointer (&self->tmp_globals, g_ptr_array_unref);
  g_clear_pointer (&self->features, g_ptr_array_unref);

  /* remove all the registered objects
     this will normally also destroy the object managers, eventually, since
     they are normally ref'ed by modules, which are registered objects */
  {
    g_autoptr (GPtrArray) objlist = g_steal_pointer (&self->objects);

    while (objlist->len > 0) {
      g_autoptr (GObject) object = g_ptr_array_steal_index_fast (objlist,
          objlist->len - 1);
      wp_registry_notify_rm_object (self, object);
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
}

void
wp_registry_attach (WpRegistry *self, struct pw_core *pw_core)
{
  self->pw_registry = pw_core_get_registry (pw_core,
      PW_VERSION_REGISTRY, 0);
  pw_registry_add_listener (self->pw_registry, &self->listener,
      &registry_events, self);
}

void
wp_registry_detach (WpRegistry *self)
{
  if (self->pw_registry) {
    spa_hook_remove (&self->listener);
    pw_proxy_destroy ((struct pw_proxy *) self->pw_registry);
    self->pw_registry = NULL;
  }

  /* remove pipewire globals */
  GPtrArray *objlist = self->globals;
  while (objlist && objlist->len > 0) {
    g_autoptr (WpGlobal) global = g_ptr_array_steal_index_fast (objlist,
        objlist->len - 1);

    if (!global)
      continue;

    if (global->proxy)
      wp_registry_notify_rm_object (self, global->proxy);

    /* remove the APPEARS_ON_REGISTRY flag to unref the proxy if it is owned
      by the registry; set registry to NULL to avoid further interference */
    global->registry = NULL;
    wp_global_rm_flag (global, WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY);

    /* the registry's ref on global is dropped here; it may still live if
      there is a proxy that owns a ref on it, but global->registry is set
      to NULL, so there is no further interference */
  }

  /* drop tmp globals as well */
  objlist = self->tmp_globals;
  while (objlist && objlist->len > 0) {
    g_autoptr (WpGlobal) global = g_ptr_array_steal_index_fast (objlist,
        objlist->len - 1);
    wp_global_rm_flag (global, WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY);
  }
}

static gboolean
expose_tmp_globals (WpCore *core)
{
  WpRegistry *self = wp_core_get_registry (core);
  g_autoptr (GPtrArray) tmp_globals = NULL;
  g_autoptr (GPtrArray) object_managers = NULL;

  /* in case the registry was cleared in the meantime... */
  if (G_UNLIKELY (!self->tmp_globals))
    return G_SOURCE_REMOVE;

  /* steal the tmp_globals list and replace it with an empty one */
  tmp_globals = self->tmp_globals;
  self->tmp_globals =
      g_ptr_array_new_with_free_func ((GDestroyNotify) wp_global_unref);

  wp_debug_object (core, "exposing %u new globals", tmp_globals->len);

  /* traverse in the order that the globals appeared on the registry */
  for (guint i = 0; i < tmp_globals->len; i++) {
    WpGlobal *g = g_ptr_array_index (tmp_globals, i);

    /* if global was already removed, drop it */
    if (g->flags == 0 || g->id == SPA_ID_INVALID)
      continue;

    /* if old global is owned by proxy, remove it */
    if (self->globals->len > g->id) {
      WpGlobal *old_g = g_ptr_array_index (self->globals, g->id);
      if (old_g && (old_g->flags & WP_GLOBAL_FLAG_OWNED_BY_PROXY))
        wp_global_rm_flag (old_g, WP_GLOBAL_FLAG_OWNED_BY_PROXY);
    }

    g_return_val_if_fail (self->globals->len <= g->id ||
        g_ptr_array_index (self->globals, g->id) == NULL, G_SOURCE_REMOVE);

    /* set the registry, so that wp_global_rm_flag() can work full-scale */
    g->registry = self;

    /* store it in the globals list */
    if (self->globals->len <= g->id)
      g_ptr_array_set_size (self->globals, g->id + 1);
    g_ptr_array_index (self->globals, g->id) = wp_global_ref (g);
  }

  object_managers = g_ptr_array_copy (self->object_managers,
      (GCopyFunc) g_object_ref, NULL);
  g_ptr_array_set_free_func (object_managers, g_object_unref);

  /* notify object managers */
  for (guint i = 0; i < object_managers->len; i++) {
    WpObjectManager *om = g_ptr_array_index (object_managers, i);

    for (guint i = 0; i < tmp_globals->len; i++) {
      WpGlobal *g = g_ptr_array_index (tmp_globals, i);

      /* if global was already removed, drop it */
      if (g->flags == 0 || g->id == SPA_ID_INVALID)
        continue;

      wp_object_manager_add_global (om, g);
    }
    wp_object_manager_maybe_objects_changed (om);
  }

  return G_SOURCE_REMOVE;
}

/*
 * \param new_global (out) (transfer full) (optional): the new global
 *
 * This is normally called up to 2 times in the same sync cycle:
 * one from registry_global(), another from the proxy bound event
 * Unfortunately the order in which those 2 events happen is specific
 * to the implementation of the object, which is why this is implemented
 * with a temporary globals list that get exposed later to the object managers
 */
void
wp_registry_prepare_new_global (WpRegistry * self, guint32 id,
    guint32 permissions, guint32 flag, GType type,
    WpGlobalProxy *proxy, const struct spa_dict *props,
    WpGlobal ** new_global)
{
  g_autoptr (WpGlobal) global = NULL;
  WpCore *core = wp_registry_get_core (self);

  g_return_if_fail (flag != 0);

  for (guint i = 0; i < self->tmp_globals->len; i++) {
    WpGlobal *g = g_ptr_array_index (self->tmp_globals, i);
    if (g->id == id) {
      global = wp_global_ref (g);
      break;
    }
  }

  wp_debug_object (core, "%s WpGlobal:%u type:%s proxy:%p",
      global ? "reuse" : "new", id, g_type_name (type),
      (global && global->proxy) ? global->proxy : proxy);

  if (!global) {
    global = g_rc_box_new0 (WpGlobal);
    global->flags = flag;
    global->id = id;
    global->type = type;
    global->permissions = permissions;
    global->properties = props ?
        wp_properties_new_copy_dict (props) : wp_properties_new_empty ();
    global->proxy = proxy;
    g_ptr_array_add (self->tmp_globals, wp_global_ref (global));

    /* ensure we have 'object.id' so that we can filter by id on object managers */
    wp_properties_setf (global->properties, PW_KEY_OBJECT_ID, "%u", global->id);

    /* schedule exposing when adding the first global */
    if (self->tmp_globals->len == 1) {
      wp_core_idle_add_closure (core, NULL,
          g_cclosure_new_object (G_CALLBACK (expose_tmp_globals), G_OBJECT (core)));
    }
  } else {
    /* store the most permissive permissions */
    if (permissions > global->permissions)
      global->permissions = permissions;

    global->flags |= flag;

    /* store the most deep type (i.e. WpImplNode instead of WpNode),
       so that object-manager interests can work more accurately
       if the interest is on a specific subclass */
    if (g_type_depth (type) > g_type_depth (global->type))
      global->type = type;

    if (proxy) {
      g_return_if_fail (global->proxy == NULL);
      global->proxy = proxy;
    }

    if (props)
      wp_properties_update_from_dict (global->properties, props);
  }

  if (new_global)
    *new_global = g_steal_pointer (&global);
}

void
wp_registry_notify_add_object (WpRegistry *self, gpointer object)
{
  for (guint i = 0; i < self->object_managers->len; i++) {
    WpObjectManager *om = g_ptr_array_index (self->object_managers, i);
    wp_object_manager_add_object (om, object);
    wp_object_manager_maybe_objects_changed (om);
  }
}

void
wp_registry_notify_rm_object (WpRegistry *self, gpointer object)
{
  for (guint i = 0; i < self->object_managers->len; i++) {
    WpObjectManager *om = g_ptr_array_index (self->object_managers, i);
    wp_object_manager_rm_object (om, object);
    wp_object_manager_maybe_objects_changed (om);
  }
}

void
wp_registry_install_object_manager (WpRegistry * self, WpObjectManager * om)
{
  guint i;

  g_object_weak_ref (G_OBJECT (om), object_manager_destroyed, self);
  g_ptr_array_add (self->object_managers, om);

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

  wp_object_manager_maybe_objects_changed (om);
}

/* WpGlobal */

G_DEFINE_BOXED_TYPE (WpGlobal, wp_global, wp_global_ref, wp_global_unref)

void
wp_global_rm_flag (WpGlobal *global, guint rm_flag)
{
  WpRegistry *reg = global->registry;
  guint32 id = global->id;

  /* no flag to remove */
  if (!(global->flags & rm_flag))
    return;

  wp_trace_boxed (WP_TYPE_GLOBAL, global,
      "remove global %u flag 0x%x [flags:0x%x, reg:%p]",
      id, rm_flag, global->flags, reg);

  /* global was owned by the proxy; by removing the flag, we clear out
     also the proxy pointer, which is presumably no longer valid and we
     notify all listeners that the proxy is gone */
  if (rm_flag == WP_GLOBAL_FLAG_OWNED_BY_PROXY) {
    global->flags &= ~WP_GLOBAL_FLAG_OWNED_BY_PROXY;
    if (reg && global->proxy) {
      wp_registry_notify_rm_object (reg, global->proxy);
    }
    global->proxy = NULL;
  }
  /* registry removed the global */
  else if (rm_flag == WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY) {
    global->flags &= ~WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY;

    /* destroy the proxy if it exists */
    if (global->proxy) {
      /* steal the proxy to avoid calling wp_registry_notify_rm_object()
         again while removing OWNED_BY_PROXY;
         keep a temporary ref so that _deactivate() doesn't crash in case the
         pw-proxy-destroyed signal causes external references to be dropped */
      g_autoptr (WpGlobalProxy) proxy =
          g_object_ref (g_steal_pointer (&global->proxy));

      /* notify all listeners that the proxy is gone */
      if (reg)
        wp_registry_notify_rm_object (reg, proxy);

      /* remove FEATURE_BOUND to destroy the underlying pw_proxy */
      wp_object_deactivate (WP_OBJECT (proxy), WP_PROXY_FEATURE_BOUND);

      /* stop all in-progress activations */
      wp_object_abort_activation (WP_OBJECT (proxy), "PipeWire proxy removed");

      /* if the proxy is not owning the global, unref it */
      if (global->flags == 0)
        g_object_unref (proxy);
    }

    /* It's possible to receive consecutive {add, remove, add} events for the
     * same id. Since the WpGlobal might not be destroyed immediately below,
     * (e.g. it's in tmp_globals list), we must invalidate the id now, so that
     * this WpGlobal is not used in reference to objects added later.
     */
    global->id = SPA_ID_INVALID;
    wp_properties_setf (global->properties, PW_KEY_OBJECT_ID, NULL);
  }

  /* drop the registry's ref on global when it does not appear on the registry anymore */
  if (!(global->flags & WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY) && reg) {
    g_clear_pointer (&g_ptr_array_index (reg->globals, id), wp_global_unref);
  }
}

struct pw_proxy *
wp_global_bind (WpGlobal * global)
{
  g_return_val_if_fail (global->proxy, NULL);
  g_return_val_if_fail (global->registry, NULL);

  WpProxyClass *klass = WP_PROXY_GET_CLASS (global->proxy);
  return pw_registry_bind (global->registry->pw_registry, global->id,
      klass->pw_iface_type, klass->pw_iface_version, 0);
}
