/* WirePlumber
 *
 * Copyright © 2019-2024 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "private/registry.h"
#include "object-manager.h"
#include "log.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-registry")

/*! \defgroup wpregistry WpRegistry */
/*!
 * \struct WpRegistry
 *
 * The registry keeps track of the objects of a WpCore. It is owned by the
 * core and can be retrieved with wp_core_get_registry().
 *
 * The registry has the following responsibilities:
 *  * it maintains a list of registered objects, which other classes use
 *    to keep objects loaded permanently into memory
 *  * it watches the PipeWire registry and keeps track of remote and local
 *    objects that appear in it, making them accessible through
 *    the WpObjectManager API
 *  * it keeps track of the features that are provided by the loaded
 *    components
 *
 * \gproperties
 *
 * \gproperty{core, WpCore *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The core of the registry}
 */

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
 * 2) PipeWire global objects, which were constructed by this process by calling
 *    into a remote factory (see wp_node_new_from_factory()).
 *
 *    These are owned by their creator and are destroyed by dropping the last
 *    reference to the WpProxy, so they must not be conflated with the registry
 *    object for the same global: everything that consumes registry objects
 *    would then reference them, and only release those references once the
 *    global is gone, which cannot happen until they are destroyed.
 *
 *    They therefore do not claim a WpGlobal ("own-global" is FALSE on them);
 *    the global appears on the registry like any other and gets its own proxy,
 *    as in case 1.
 *
 *    WpGlobal still supports being owned by a proxy through the
 *    WP_GLOBAL_FLAG_OWNED_BY_PROXY flag, which wp_registry_prepare_new_global()
 *    sets when the proxy receives its 'bound' event. As the flag name suggests,
 *    such globals are "owned" by the WpProxy and the WpGlobal has no ref on the
 *    WpProxy itself. This allows destroying the proxy in client code by dropping
 *    its last reference.
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
 * 3) WirePlumber global objects (WpModule, WpPlugin).
 *
 *    These are local objects that have nothing to do with PipeWire. They do not
 *    have a global id and they are also not subclasses of WpProxy. The registry
 *    always owns a reference on them, so that they are kept alive for as long
 *    as the WpCore is alive.
 */

struct _WpRegistry
{
  GObject parent;

  /* the core owns the registry */
  GWeakRef core;

  struct pw_registry *pw_registry;
  struct spa_hook listener;
  struct spa_hook proxy_listener;

  GPtrArray *globals; // element-type: WpGlobal*
  GPtrArray *tmp_globals; // element-type: WpGlobal*
  GPtrArray *objects; // element-type: GObject*
  GPtrArray *object_managers; // element-type: WpObjectManager*
  GPtrArray *features; // element-type: gchar*
};

static void
object_manager_destroyed (gpointer data, GObject * om)
{
  WpRegistry *self = data;
  g_ptr_array_remove_fast (self->object_managers, om);
}

/* Takes a reference on every object manager, so that the notify loops
 * can survive a manager that is destroyed from one of their callbacks; that
 * also removes its entry from self->object_managers (object_manager_destroyed)
 * and reorders the rest */
static GPtrArray *
copy_object_managers (WpRegistry *self)
{
  GPtrArray *object_managers = g_ptr_array_copy (self->object_managers,
      (GCopyFunc) g_object_ref, NULL);
  g_ptr_array_set_free_func (object_managers, g_object_unref);
  return object_managers;
}

/* find the subclass of WpPipewireGlobal that can handle
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
  g_autoptr (WpCore) core = wp_registry_get_core (self);

  wp_debug_object (core,
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

  g_autoptr (WpCore) core = wp_registry_get_core (self);
  wp_debug_object (core,
      "global removed:%u type:%s", id, g_type_name (global->type));

  wp_global_rm_flag (global, WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY);
}

static const struct pw_registry_events registry_events = {
  PW_VERSION_REGISTRY_EVENTS,
  .global = registry_global,
  .global_remove = registry_global_remove,
};

enum {
  PROP_0,
  PROP_CORE,
};

G_DEFINE_TYPE (WpRegistry, wp_registry, G_TYPE_OBJECT)

static void
wp_registry_init (WpRegistry *self)
{
  g_weak_ref_init (&self->core, NULL);

  self->globals =
      g_ptr_array_new_with_free_func ((GDestroyNotify) wp_global_unref);
  self->tmp_globals =
      g_ptr_array_new_with_free_func ((GDestroyNotify) wp_global_unref);
  self->objects = g_ptr_array_new_with_free_func (g_object_unref);
  self->object_managers = g_ptr_array_new ();
  self->features = g_ptr_array_new_with_free_func (g_free);
}

static void
wp_registry_finalize (GObject * object)
{
  WpRegistry *self = WP_REGISTRY (object);

  wp_registry_clear (self);
  wp_registry_detach (self);

  /* in case there are any object managers left,
     remove the weak ref on them and let them be... */
  while (self->object_managers->len > 0) {
    GObject *om = g_ptr_array_steal_index_fast (self->object_managers,
        self->object_managers->len - 1);
    g_object_weak_unref (om, object_manager_destroyed, self);
  }

  g_clear_pointer (&self->globals, g_ptr_array_unref);
  g_clear_pointer (&self->tmp_globals, g_ptr_array_unref);
  g_clear_pointer (&self->objects, g_ptr_array_unref);
  g_clear_pointer (&self->object_managers, g_ptr_array_unref);
  g_clear_pointer (&self->features, g_ptr_array_unref);
  g_weak_ref_clear (&self->core);

  G_OBJECT_CLASS (wp_registry_parent_class)->finalize (object);
}

static void
wp_registry_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpRegistry *self = WP_REGISTRY (object);

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&self->core));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_registry_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpRegistry *self = WP_REGISTRY (object);

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&self->core, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_registry_class_init (WpRegistryClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = wp_registry_finalize;
  object_class->get_property = wp_registry_get_property;
  object_class->set_property = wp_registry_set_property;

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The core of the registry",
          WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpRegistry *
wp_registry_new (WpCore * core)
{
  return g_object_new (WP_TYPE_REGISTRY, "core", core, NULL);
}

/*!
 * \brief Gets the core of the registry
 *
 * \ingroup wpregistry
 * \param self the registry
 * \returns (transfer full) (nullable): the core of \a self, or NULL if that
 *   core is gone
 */
WpCore *
wp_registry_get_core (WpRegistry * self)
{
  g_return_val_if_fail (WP_IS_REGISTRY (self), NULL);
  return g_weak_ref_get (&self->core);
}

/* removes all registered objects and provided features; the pipewire globals
   and the installed object managers are kept */
void
wp_registry_clear (WpRegistry *self)
{
  /* disable all objects before removing them */
  for (guint i = 0; i < self->objects->len; i++) {
    gpointer o = g_ptr_array_index (self->objects, i);
    if (WP_IS_OBJECT (o))
      wp_object_deactivate (WP_OBJECT (o), WP_OBJECT_FEATURES_ALL);
  }

  /* now remove all the objects */
  while (self->objects->len > 0) {
    g_autoptr (GObject) object = g_ptr_array_steal_index_fast (self->objects,
        self->objects->len - 1);
    wp_registry_notify_rm_object (self, object);
  }

  g_ptr_array_set_size (self->features, 0);
}

void
wp_registry_mark_feature_provided (WpRegistry * self, const gchar * feature)
{
  g_ptr_array_add (self->features, g_strdup (feature));
}

/* TRUE if there are globals that have not been exposed yet, or if no globals
   have appeared at all yet (early startup) */
gboolean
wp_registry_has_pending_globals (WpRegistry * self)
{
  return self->tmp_globals->len != 0 || self->globals->len == 0;
}

void
wp_registry_destroy_global (WpRegistry * self, guint32 id)
{
  if (self->pw_registry)
    pw_registry_destroy (self->pw_registry, id);
}

/* removes our hooks from the pw_registry proxy and forgets about it; the
   hooks are zeroed, so that removing them again is a no-op */
static void
forget_pw_registry (WpRegistry *self)
{
  spa_hook_remove (&self->listener);
  spa_hook_remove (&self->proxy_listener);
  spa_zero (self->listener);
  spa_zero (self->proxy_listener);
  self->pw_registry = NULL;
}

/* the proxy is removed when the pw_core is disconnected; it is ours to
   destroy, and not doing so would leave us attached to a dead pw_core */
static void
registry_proxy_removed (void *data)
{
  WpRegistry *self = data;
  struct pw_proxy *proxy = (struct pw_proxy *) self->pw_registry;

  forget_pw_registry (self);
  pw_proxy_destroy (proxy);
}

static const struct pw_proxy_events registry_proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .removed = registry_proxy_removed,
};

/* starts watching the pipewire registry of pw_core; does nothing if already
   attached, which can only be to pw_core, as the proxy is destroyed when the
   pw_core it belongs to is disconnected */
void
wp_registry_attach (WpRegistry *self, struct pw_core *pw_core)
{
  g_return_if_fail (pw_core);

  if (self->pw_registry)
    return;

  /* drop any globals left from a previous connection */
  wp_registry_detach (self);

  self->pw_registry = pw_core_get_registry (pw_core, PW_VERSION_REGISTRY, 0);
  pw_proxy_add_listener ((struct pw_proxy *) self->pw_registry,
      &self->proxy_listener, &registry_proxy_events, self);
  pw_registry_add_listener (self->pw_registry, &self->listener,
      &registry_events, self);
}

/* stops watching the pipewire registry and drops all pipewire globals;
   can be called any number of times, also after the proxy was removed */
void
wp_registry_detach (WpRegistry *self)
{
  if (self->pw_registry) {
    struct pw_proxy *proxy = (struct pw_proxy *) self->pw_registry;
    forget_pw_registry (self);
    pw_proxy_destroy (proxy);
  }

  /* remove pipewire globals */
  while (self->globals->len > 0) {
    g_autoptr (WpGlobal) global = g_ptr_array_steal_index_fast (self->globals,
        self->globals->len - 1);

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
  while (self->tmp_globals->len > 0) {
    g_autoptr (WpGlobal) global = g_ptr_array_steal_index_fast (
        self->tmp_globals, self->tmp_globals->len - 1);
    wp_global_rm_flag (global, WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY);
  }
}

static gboolean
expose_tmp_globals (WpCore *core)
{
  WpRegistry *self = wp_core_get_registry (core);
  g_autoptr (GPtrArray) tmp_globals = NULL;
  g_autoptr (GPtrArray) object_managers = NULL;

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

  object_managers = copy_object_managers (self);

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
  g_autoptr (WpCore) core = wp_registry_get_core (self);

  g_return_if_fail (flag != 0);
  g_return_if_fail (core);

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
      WpMainContext *wp_ctx =
          wp_core_get_main_context (core);
      wp_context_idle_add_closure (WP_CONTEXT (wp_ctx),
          NULL,
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
  g_autoptr (GPtrArray) object_managers = copy_object_managers (self);

  for (guint i = 0; i < object_managers->len; i++) {
    WpObjectManager *om = g_ptr_array_index (object_managers, i);
    wp_object_manager_add_object (om, object);
    wp_object_manager_maybe_objects_changed (om);
  }
}

void
wp_registry_notify_rm_object (WpRegistry *self, gpointer object)
{
  g_autoptr (GPtrArray) object_managers = copy_object_managers (self);

  for (guint i = 0; i < object_managers->len; i++) {
    WpObjectManager *om = g_ptr_array_index (object_managers, i);
    wp_object_manager_rm_object (om, object);
    wp_object_manager_maybe_objects_changed (om);
  }
}

void
wp_registry_add_object_manager (WpRegistry * self, WpObjectManager * om)
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

/*!
 * \brief Finds a registered object
 *
 * \ingroup wpregistry
 * \param self the registry
 * \param func (scope call): a function that takes the object being searched
 *   as the first argument and \a data as the second. it should return TRUE if
 *   the object is found or FALSE otherwise
 * \param data the second argument to \a func
 * \returns (transfer full) (type GObject*) (nullable): the registered object
 *   or NULL if not found
 */
gpointer
wp_registry_find_object (WpRegistry * self, GEqualFunc func,
    gconstpointer data)
{
  GObject *object;
  guint i;

  g_return_val_if_fail (WP_IS_REGISTRY (self), NULL);

  for (i = 0; i < self->objects->len; i++) {
    object = g_ptr_array_index (self->objects, i);
    if (func (object, data))
      return g_object_ref (object);
  }

  return NULL;
}

/*!
 * \brief Registers \a obj with the registry, making it appear on
 * WpObjectManager instances as well.
 *
 * The registry will also maintain a ref to that object until it
 * is removed.
 *
 * \ingroup wpregistry
 * \param self the registry
 * \param obj (transfer full) (type GObject*): the object to register
 */
void
wp_registry_register_object (WpRegistry * self, gpointer obj)
{
  g_return_if_fail (WP_IS_REGISTRY (self));
  g_return_if_fail (G_IS_OBJECT (obj));

  /* ensure the registered object is associated with our core */
  if (WP_IS_OBJECT (obj)) {
    g_autoptr (WpCore) obj_core = wp_object_get_core (WP_OBJECT (obj));
    g_autoptr (WpCore) core = wp_registry_get_core (self);
    g_return_if_fail (obj_core == core);
  }

  g_ptr_array_add (self->objects, obj);

  /* notify object managers */
  wp_registry_notify_add_object (self, obj);
}

/*!
 * \brief Detaches and unrefs the specified object from this registry.
 *
 * \ingroup wpregistry
 * \param self the registry
 * \param obj (transfer none) (type GObject*): a pointer to the object to remove
 */
void
wp_registry_remove_object (WpRegistry * self, gpointer obj)
{
  g_return_if_fail (WP_IS_REGISTRY (self));
  g_return_if_fail (G_IS_OBJECT (obj));

  /* notify object managers */
  wp_registry_notify_rm_object (self, obj);

  g_ptr_array_remove_fast (self->objects, obj);
}

/*!
 * \brief Test if a global feature is provided
 *
 * \ingroup wpregistry
 * \param self the registry
 * \param feature the feature name
 * \returns TRUE if the feature is provided, FALSE otherwise
 */
gboolean
wp_registry_test_feature (WpRegistry * self, const gchar * feature)
{
  g_return_val_if_fail (WP_IS_REGISTRY (self), FALSE);

  return g_ptr_array_find_with_equal_func (self->features, feature,
      g_str_equal, NULL);
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
