/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpObjectManager
 *
 * The #WpObjectManager class provides a way to collect a set of objects
 * and be notified when objects that fulfill a certain set of criteria are
 * created or destroyed.
 *
 * There are 4 kinds of objects that can be managed by a #WpObjectManager:
 *   * remote PipeWire global objects that are advertised on the registry;
 *     these are bound locally to subclasses of #WpProxy
 *   * remote PipeWire global objects that are created by calling a remote
 *     factory through the WirePlumber API; these are very similar to other
 *     global objects but it should be noted that the same #WpProxy instance
 *     that created them appears in the #WpObjectManager (as soon as its
 *     %WP_PROXY_FEATURE_BOUND is enabled)
 *   * local PipeWire objects that are being exported to PipeWire
 *     (#WpImplNode, WpImplEndpoint [private], etc); these appear in the
 *     #WpObjectManager as soon as they are exported (so, when their
 *     %WP_PROXY_FEATURE_BOUND is enabled)
 *   * WirePlumber-specific objects, such as WirePlumber factories
 *
 * To start an object manager, you first need to declare interest in a certain
 * kind of object by calling wp_object_manager_add_interest() and then install
 * it on the #WpCore with wp_core_install_object_manager().
 *
 * Upon installing a #WpObjectManager on a #WpCore, any pre-existing objects
 * that match the interests of this #WpObjectManager will immediately become
 * available to get through wp_object_manager_iterate() and the
 * #WpObjectManager::object-added signal will be emitted for all of them.
 */

#define G_LOG_DOMAIN "wp-object-manager"

#include "object-manager.h"
#include "debug.h"
#include "private.h"
#include <pipewire/pipewire.h>

/* WpObjectManager */

struct interest
{
  GType g_type;
  WpProxyFeatures wanted_features;
  GVariant *constraints; // aa{sv}
};

struct _WpObjectManager
{
  GObject parent;

  GWeakRef core;

  /* array of struct interest;
    pw_array has a better API for our use case than GArray */
  struct pw_array interests;

  /* objects that we are interested in, without a ref */
  GPtrArray *objects;

  gboolean pending_objchanged;
};

enum {
  PROP_0,
  PROP_CORE,
};

enum {
  SIGNAL_OBJECT_ADDED,
  SIGNAL_OBJECT_REMOVED,
  SIGNAL_OBJECTS_CHANGED,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (WpObjectManager, wp_object_manager, G_TYPE_OBJECT)

static void
wp_object_manager_init (WpObjectManager * self)
{
  g_weak_ref_init (&self->core, NULL);
  pw_array_init (&self->interests, sizeof (struct interest));
  self->objects = g_ptr_array_new ();
  self->pending_objchanged = FALSE;
}

static void
wp_object_manager_finalize (GObject * object)
{
  WpObjectManager *self = WP_OBJECT_MANAGER (object);
  struct interest *i;

  g_clear_pointer (&self->objects, g_ptr_array_unref);

  pw_array_for_each (i, &self->interests) {
    g_clear_pointer (&i->constraints, g_variant_unref);
  }
  pw_array_clear (&self->interests);

  g_weak_ref_clear (&self->core);

  G_OBJECT_CLASS (wp_object_manager_parent_class)->finalize (object);
}

static void
wp_object_manager_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpObjectManager *self = WP_OBJECT_MANAGER (object);

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
wp_object_manager_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpObjectManager *self = WP_OBJECT_MANAGER (object);

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
wp_object_manager_class_init (WpObjectManagerClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_object_manager_finalize;
  object_class->get_property = wp_object_manager_get_property;
  object_class->set_property = wp_object_manager_set_property;

  /* Install the properties */

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The WpCore", WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * WpObjectManager::object-added:
   * @self: the object manager
   * @object: (transfer none): the managed object that was just added
   *
   * Emitted when an object that matches the interests of this object manager
   * is made available.
   */
  signals[SIGNAL_OBJECT_ADDED] = g_signal_new (
      "object-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_OBJECT);

  /**
   * WpObjectManager::object-removed:
   * @self: the object manager
   * @object: (transfer none): the managed object that is being removed
   *
   * Emitted when an object that was previously added on this object manager
   * is now being removed (and most likely destroyed). At the time that this
   * signal is emitted, the object is still alive.
   */
  signals[SIGNAL_OBJECT_REMOVED] = g_signal_new (
      "object-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_OBJECT);

  /**
   * WpObjectManager::objects-changed:
   * @self: the object manager
   *
   * Emitted when one or more objects have been recently added or removed
   * from this object manager. This signal is useful to get notified only once
   * when multiple changes happen in a short timespan. The receiving callback
   * may retrieve the updated list of objects by calling
   * wp_object_manager_iterate()
   */
  signals[SIGNAL_OBJECTS_CHANGED] = g_signal_new (
      "objects-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/**
 * wp_object_manager_new:
 *
 * Constructs a new object manager.
 *
 * Returns: (transfer full): the newly constructed object manager
 */
WpObjectManager *
wp_object_manager_new (void)
{
  return g_object_new (WP_TYPE_OBJECT_MANAGER, NULL);
}

/**
 * wp_object_manager_add_interest:
 * @self: the object manager
 * @gtype: the #GType of the objects that we are declaring interest in
 * @constraints: (nullable): a variant of type "aa{sv}" (array of dictionaries)
 *   with additional constraints on the managed objects
 * @wanted_features: a set of features that will automatically be enabled
 *   on managed objects, if they are subclasses of #WpProxy
 *
 * Declares interest in a certain kind of object. Interest consists of a #GType
 * that the object must be an ancestor of (g_type_is_a must match) and
 * optionally, a set of additional constraints on certain properties of the
 * object.
 *
 * The @constraints #GVariant should contain an array of dictionaries ("aa{sv}").
 * Each dictionary must have the following fields:
 *  - "type" (i): The constraint type, #WpObjectManagerConstraintType
 *  - "name" (s): The name of the constrained property
 *  - "value" (s): The value that the property must have
 *
 * For example, to discover all the 'node' objects in the PipeWire graph,
 * the following code can be used:
 * |[
 *   WpObjectManager *om = wp_object_manager_new ();
 *   wp_object_manager_add_interest (om, WP_TYPE_NODE, NULL,
 *       WP_PROXY_FEATURES_STANDARD);
 *   wp_core_install_object_manager (core, om);
 *   WpIterator *nodes_it = wp_object_manager_iterate (om);
 * ]|
 *
 * and to discover all 'port' objects that belong to a specific 'node':
 * |[
 *   WpObjectManager *om = wp_object_manager_new ();
 *
 *   GVariantBuilder b;
 *   g_variant_builder_init (&b, G_VARIANT_TYPE ("aa{sv}"));
 *   g_variant_builder_open (&b, G_VARIANT_TYPE_VARDICT);
 *   g_variant_builder_add (&b, "{sv}", "type",
 *       g_variant_new_int32 (WP_OBJECT_MANAGER_CONSTRAINT_PW_GLOBAL_PROPERTY));
 *   g_variant_builder_add (&b, "{sv}", "name",
 *       g_variant_new_string (PW_KEY_NODE_ID));
 *   g_variant_builder_add (&b, "{sv}", "value",
 *       g_variant_new_string (node_id));
 *   g_variant_builder_close (&b);
 *
 *   wp_object_manager_add_interest (om, WP_TYPE_PORT,
 *       g_variant_builder_end (&b),
 *       WP_PROXY_FEATURES_STANDARD);
 *
 *   wp_core_install_object_manager (core, om);
 *   WpIterator *ports_it = wp_object_manager_iterate (om);
 * ]|
 */
void
wp_object_manager_add_interest (WpObjectManager *self,
    GType gtype, GVariant * constraints,
    WpProxyFeatures wanted_features)
{
  struct interest *i;

  g_return_if_fail (WP_IS_OBJECT_MANAGER (self));
  g_return_if_fail (constraints == NULL ||
      g_variant_is_of_type (constraints, G_VARIANT_TYPE ("aa{sv}")));

  /* grow the array by 1 struct interest and fill it in */
  i = pw_array_add (&self->interests, sizeof (struct interest));
  i->g_type = gtype;
  i->wanted_features = wanted_features;
  i->constraints = constraints ? g_variant_ref_sink (constraints) : NULL;
}

/**
 * wp_object_manager_get_n_objects:
 * @self: the object manager
 *
 * Returns: the number of objects managed by this #WpObjectManager
 */
guint
wp_object_manager_get_n_objects (WpObjectManager * self)
{
  g_return_val_if_fail (WP_IS_OBJECT_MANAGER (self), 0);
  return self->objects->len;
}

struct om_iterator_data
{
  WpObjectManager *om;
  guint index;
};

static void
om_iterator_reset (WpIterator *it)
{
  struct om_iterator_data *it_data = wp_iterator_get_user_data (it);
  it_data->index = 0;
}

static gboolean
om_iterator_next (WpIterator *it, GValue *item)
{
  struct om_iterator_data *it_data = wp_iterator_get_user_data (it);
  GPtrArray *objects = it_data->om->objects;

  if (G_LIKELY (it_data->index < objects->len)) {
    g_value_init_from_instance (item,
        g_ptr_array_index (objects, it_data->index++));
    return TRUE;
  }
  return FALSE;
}

static gboolean
om_iterator_fold (WpIterator *it, WpIteratorFoldFunc func, GValue *ret,
    gpointer data)
{
  struct om_iterator_data *it_data = wp_iterator_get_user_data (it);
  gpointer *obj, *base;
  guint len;

  obj = base = it_data->om->objects->pdata;
  len = it_data->om->objects->len;

  while ((obj - base) < len) {
    g_auto (GValue) item = G_VALUE_INIT;
    g_value_init_from_instance (&item, *obj);
    if (!func (&item, ret, data))
      return FALSE;
    obj++;
  }

  return TRUE;
}

static void
om_iterator_finalize (WpIterator *it)
{
  struct om_iterator_data *it_data = wp_iterator_get_user_data (it);
  g_object_unref (it_data->om);
}

static const WpIteratorMethods om_iterator_methods = {
  .reset = om_iterator_reset,
  .next = om_iterator_next,
  .fold = om_iterator_fold,
  .finalize = om_iterator_finalize,
};

/**
 * wp_object_manager_iterate:
 * @self: the object manager
 *
 * Returns: (transfer full): a #WpIterator that iterates over all the managed
 *   objects of this object manager
 */
WpIterator *
wp_object_manager_iterate (WpObjectManager * self)
{
  WpIterator *it;
  struct om_iterator_data *it_data;

  g_return_val_if_fail (WP_IS_OBJECT_MANAGER (self), NULL);

  it = wp_iterator_new (&om_iterator_methods, sizeof (struct om_iterator_data));
  it_data = wp_iterator_get_user_data (it);
  it_data->om = g_object_ref (self);
  it_data->index = 0;
  return it;
}

static gboolean
find_proxy_fold_func (const GValue *item, GValue *ret, gpointer data)
{
  if (g_type_is_a (G_VALUE_TYPE (item), WP_TYPE_PROXY)) {
    WpProxy *proxy = g_value_get_object (item);
    if (wp_proxy_get_bound_id (proxy) == GPOINTER_TO_UINT (data)) {
      g_value_init_from_instance (ret, proxy);
      return FALSE;
    }
  }
  return TRUE;
}

/**
 * wp_object_manager_find_proxy:
 * @self: the object manager
 * @bound_id: the bound id of the proxy to get
 *
 * Searches the managed objects to find a #WpProxy that has the given @bound_id
 *
 * Returns: (transfer full) (nullable): the proxy that has the given @bound_id,
 *    or %NULL if there is no such proxy managed by this object manager
 */
WpProxy *
wp_object_manager_find_proxy (WpObjectManager *self, guint bound_id)
{
  g_autoptr (WpIterator) it = wp_object_manager_iterate (self);
  g_auto (GValue) ret = G_VALUE_INIT;

  if (!wp_iterator_fold (it, find_proxy_fold_func, &ret,
          GUINT_TO_POINTER (bound_id)))
    return g_value_dup_object (&ret);

  return NULL;
}

static gboolean
check_constraints (GVariant *constraints,
    WpProperties *global_props,
    GObject *object)
{
  GVariantIter iter;
  GVariant *c;
  WpObjectManagerConstraintType ctype;
  g_autoptr (WpProperties) props = NULL;
  const gchar *prop_name, *prop_value;

  /* pipewire properties are contained in a GObj property called "properties" */
  if (object &&
      g_object_class_find_property (G_OBJECT_GET_CLASS (object), "properties"))
    g_object_get (object, "properties", &props, NULL);

  g_variant_iter_init (&iter, constraints);
  while (g_variant_iter_next (&iter, "@a{sv}", &c)) {
    GVariantDict dict = G_VARIANT_DICT_INIT (c);

    if (!g_variant_dict_lookup (&dict, "type", "i", &ctype)) {
      g_critical ("Invalid object manager constraint without a type");
      goto error;
    }

    switch (ctype) {
    case WP_OBJECT_MANAGER_CONSTRAINT_PW_GLOBAL_PROPERTY:
      if (!global_props)
        goto next;

      if (!g_variant_dict_lookup (&dict, "name", "&s", &prop_name)) {
        g_critical ("property constraint is without a property name");
        goto error;
      }
      if (!g_variant_dict_lookup (&dict, "value", "&s", &prop_value)) {
        g_critical ("property constraint is without a property value");
        goto error;
      }
      if (!g_strcmp0 (wp_properties_get (global_props, prop_name), prop_value))
        goto match;

      break;
    case WP_OBJECT_MANAGER_CONSTRAINT_PW_PROPERTY:
      if (!props)
        goto next;

      if (!g_variant_dict_lookup (&dict, "name", "&s", &prop_name)) {
        g_critical ("property constraint is without a property name");
        goto error;
      }
      if (!g_variant_dict_lookup (&dict, "value", "&s", &prop_value)) {
        g_critical ("property constraint is without a property value");
        goto error;
      }
      if (!g_strcmp0 (wp_properties_get (props, prop_name), prop_value))
        goto match;

      break;
    case WP_OBJECT_MANAGER_CONSTRAINT_G_PROPERTY:
      if (!object)
        goto next;

      if (!g_variant_dict_lookup (&dict, "name", "&s", &prop_name)) {
        g_critical ("property constraint is without a property name");
        goto error;
      }
      if (!g_variant_dict_lookup (&dict, "value", "&s", &prop_value)) {
        g_critical ("property constraint is without a property value");
        goto error;
      }

      if (!g_object_class_find_property (G_OBJECT_GET_CLASS (object), prop_name))
        goto next;

      if (({
        g_auto (GValue) value = G_VALUE_INIT;
        g_auto (GValue) str_value = G_VALUE_INIT;

        g_object_get_property (object, prop_name, &value);
        g_value_init (&str_value, G_TYPE_STRING);

        g_value_transform (&value, &str_value) &&
            !g_strcmp0 (g_value_get_string (&str_value), prop_value);
      }))
        goto match;

      break;
    default:
      g_critical ("Unknown constraint type '%d'", ctype);
      goto error;
    }

  next:
    {
      g_variant_dict_clear (&dict);
      g_clear_pointer (&c, g_variant_unref);
      continue;
    }
  match:
    {
      g_variant_dict_clear (&dict);
      g_clear_pointer (&c, g_variant_unref);
      return TRUE;
    }
  error:
    {
      g_autofree gchar *dbgstr = g_variant_print (c, TRUE);
      g_critical ("offending constraint was: %s", dbgstr);
      goto next;
    }
  }

  return FALSE;
}

static gboolean
wp_object_manager_is_interested_in_object (WpObjectManager * self,
    GObject * object)
{
  struct interest *i;

  pw_array_for_each (i, &self->interests) {
    if (g_type_is_a (G_OBJECT_TYPE (object), i->g_type)
        && (!i->constraints ||
            check_constraints (i->constraints, NULL, object)))
    {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
wp_object_manager_is_interested_in_global (WpObjectManager * self,
    WpGlobal * global, WpProxyFeatures * wanted_features)
{
  struct interest *i;

  pw_array_for_each (i, &self->interests) {
    if (g_type_is_a (global->type, i->g_type)
        && (!i->constraints ||
            check_constraints (i->constraints, global->properties,
                G_OBJECT (global->proxy))))
    {
      *wanted_features = i->wanted_features;
      return TRUE;
    }
  }

  return FALSE;
}

static void
sync_emit_objects_changed (WpCore *core, GAsyncResult *res, gpointer data)
{
  g_autoptr (WpObjectManager) self = WP_OBJECT_MANAGER (data);

  g_signal_emit (self, signals[SIGNAL_OBJECTS_CHANGED], 0);
  self->pending_objchanged = FALSE;
}

static inline void
schedule_emit_objects_changed (WpObjectManager * self)
{
  if (self->pending_objchanged)
    return;

  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  if (core) {
    wp_core_sync (core, NULL, (GAsyncReadyCallback)sync_emit_objects_changed,
        g_object_ref (self));
    self->pending_objchanged = TRUE;
  }
}

static void
on_proxy_ready (GObject * proxy, GAsyncResult * res, gpointer data)
{
  g_autoptr (WpObjectManager) self = WP_OBJECT_MANAGER (data);
  g_autoptr (GError) error = NULL;

  if (!wp_proxy_augment_finish (WP_PROXY (proxy), res, &error)) {
    wp_message_object (self, "proxy augment failed: %s", error->message);
    return;
  }

  g_ptr_array_add (self->objects, proxy);
  g_signal_emit (self, signals[SIGNAL_OBJECT_ADDED], 0, proxy);
  schedule_emit_objects_changed (self);
}

static void
wp_object_manager_add_global (WpObjectManager * self, WpGlobal * global)
{
  WpProxyFeatures features = 0;

  if (wp_object_manager_is_interested_in_global (self, global, &features)) {
    g_autoptr (WpCore) core = g_weak_ref_get (&self->core);

    if (!global->proxy)
      global->proxy = g_object_new (global->type,
          "core", core,
          "global", global,
          NULL);

    wp_proxy_augment (global->proxy, features, NULL, on_proxy_ready,
        g_object_ref (self));
  }
}

static void
wp_object_manager_add_object (WpObjectManager * self, gpointer object)
{
  if (wp_object_manager_is_interested_in_object (self, object)) {
    g_ptr_array_add (self->objects, object);
    g_signal_emit (self, signals[SIGNAL_OBJECT_ADDED], 0, object);
    schedule_emit_objects_changed (self);
  }
}

static void
wp_object_manager_rm_object (WpObjectManager * self, gpointer object)
{
  guint index;
  if (g_ptr_array_find (self->objects, object, &index)) {
    g_signal_emit (self, signals[SIGNAL_OBJECT_REMOVED], 0, object);
    g_ptr_array_remove_index_fast (self->objects, index);
    schedule_emit_objects_changed (self);
  }
}

/*
 * WpRegistry
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
 *    by exporting a local object (WpImplNode etc...).
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
 * 3) WirePlumber global objects (WpModule, WpFactory).
 *
 *    These are local objects that have nothing to do with PipeWire. They do not
 *    have a global id and they are also not subclasses of WpProxy. The registry
 *    always owns a reference on them, so that they are kept alive for as long
 *    as the WpCore is alive.
 */

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "wp-registry"

static void
wp_registry_notify_add_object (WpRegistry *self, gpointer object)
{
  for (guint i = 0; i < self->object_managers->len; i++) {
    WpObjectManager *om = g_ptr_array_index (self->object_managers, i);
    wp_object_manager_add_object (om, object);
  }
}

static void
wp_registry_notify_rm_object (WpRegistry *self, gpointer object)
{
  for (guint i = 0; i < self->object_managers->len; i++) {
    WpObjectManager *om = g_ptr_array_index (self->object_managers, i);
    wp_object_manager_rm_object (om, object);
  }
}

static void
object_manager_destroyed (gpointer data, GObject * om)
{
  WpRegistry *self = data;
  g_ptr_array_remove_fast (self->object_managers, om);
}

/* find the subclass of WpProxy that can handle
   the given pipewire interface type of the given version */
static inline GType
find_proxy_instance_type (const char * type, guint32 version)
{
  g_autofree GType *children;
  guint n_children;

  children = g_type_children (WP_TYPE_PROXY, &n_children);

  for (gint i = 0; i < n_children; i++) {
    WpProxyClass *klass = (WpProxyClass *) g_type_class_ref (children[i]);
    if (g_strcmp0 (klass->pw_iface_type, type) == 0 &&
        klass->pw_iface_version == version) {
      g_type_class_unref (klass);
      return children[i];
    }

    g_type_class_unref (klass);
  }

  return WP_TYPE_PROXY;
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
}

void
wp_registry_clear (WpRegistry *self)
{
  wp_registry_detach (self);
  g_clear_pointer (&self->globals, g_ptr_array_unref);
  g_clear_pointer (&self->tmp_globals, g_ptr_array_unref);

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

static void
expose_tmp_globals (WpCore *core, GAsyncResult *res, WpRegistry *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GPtrArray) tmp_globals = NULL;

  if (!wp_core_sync_finish (core, res, &error))
    wp_warning_object (core, "core sync error: %s", error->message);

  /* in case the registry was cleared in the meantime... */
  if (G_UNLIKELY (!self->tmp_globals))
    return;

  /* steal the tmp_globals list and replace it with an empty one */
  tmp_globals = self->tmp_globals;
  self->tmp_globals =
      g_ptr_array_new_with_free_func ((GDestroyNotify) wp_global_unref);

  wp_debug_object (core, "exposing %u new globals", tmp_globals->len);

  /* traverse in the order that the globals appeared on the registry */
  for (guint i = 0; i < tmp_globals->len; i++) {
    WpGlobal *g = g_ptr_array_index (tmp_globals, i);

    /* if global was already removed, drop it */
    if (g->flags == 0)
      continue;

    /* set the registry, so that wp_global_rm_flag() can work full-scale */
    g->registry = self;

    /* store it in the globals list */
    if (self->globals->len <= g->id)
      g_ptr_array_set_size (self->globals, g->id + 1);
    g_ptr_array_index (self->globals, g->id) = wp_global_ref (g);

    /* notify object managers */
    for (guint i = 0; i < self->object_managers->len; i++) {
      WpObjectManager *om = g_ptr_array_index (self->object_managers, i);
      wp_object_manager_add_global (om, g);
    }
  }
}

/*
 * wp_registry_prepare_new_global:
 * @new_global: (out) (transfer full) (optional): the new global
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
    WpProxy *proxy, const struct spa_dict *props,
    WpGlobal ** new_global)
{
  g_autoptr (WpGlobal) global = NULL;
  WpCore *core = wp_registry_get_core (self);

  g_return_if_fail (flag != 0);
  g_return_if_fail (self->globals->len <= id ||
      g_ptr_array_index (self->globals, id) == NULL);

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

    /* schedule exposing when adding the first global */
    if (self->tmp_globals->len == 1) {
      wp_core_sync (core, NULL, (GAsyncReadyCallback) expose_tmp_globals, self);
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

/*
 * wp_registry_find_object:
 * @reg: the registry
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
wp_registry_find_object (WpRegistry *reg, GEqualFunc func, gconstpointer data)
{
  GObject *object;
  guint i;

  /* prevent bad things when called from within wp_registry_clear() */
  if (G_UNLIKELY (!reg->objects))
    return NULL;

  for (i = 0; i < reg->objects->len; i++) {
    object = g_ptr_array_index (reg->objects, i);
    if (func (object, data))
      return g_object_ref (object);
  }

  return NULL;
}

/*
 * wp_registry_register_object:
 * @reg: the registry
 * @obj: (transfer full) (type GObject*): the object to register
 *
 * Registers @obj with the core, making it appear on #WpObjectManager
 * instances as well. The core will also maintain a ref to that object
 * until it is removed.
 */
void
wp_registry_register_object (WpRegistry *reg, gpointer obj)
{
  g_return_if_fail (G_IS_OBJECT (obj));

  /* prevent bad things when called from within wp_registry_clear() */
  if (G_UNLIKELY (!reg->objects)) {
    g_object_unref (obj);
    return;
  }

  g_ptr_array_add (reg->objects, obj);

  /* notify object managers */
  wp_registry_notify_add_object (reg, obj);
}

/*
 * wp_registry_remove_object:
 * @reg: the registry
 * @obj: (transfer none) (type GObject*): a pointer to the object to remove
 *
 * Detaches and unrefs the specified object from this core
 */
void
wp_registry_remove_object (WpRegistry *reg, gpointer obj)
{
  g_return_if_fail (G_IS_OBJECT (obj));

  /* prevent bad things when called from within wp_registry_clear() */
  if (G_UNLIKELY (!reg->objects))
    return;

  /* notify object managers */
  wp_registry_notify_rm_object (reg, obj);

  g_ptr_array_remove_fast (reg->objects, obj);
}

/**
 * wp_core_install_object_manager:
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
  WpRegistry *reg;
  guint i;

  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (WP_IS_OBJECT_MANAGER (om));

  reg = wp_core_get_registry (self);

  g_object_weak_ref (G_OBJECT (om), object_manager_destroyed, reg);
  g_ptr_array_add (reg->object_managers, om);
  g_object_set (om, "core", self, NULL);

  /* add pre-existing objects to the object manager,
     in case it's interested in them */
  for (i = 0; i < reg->globals->len; i++) {
    WpGlobal *g = g_ptr_array_index (reg->globals, i);
    /* check if null because the globals array can have gaps */
    if (g)
      wp_object_manager_add_global (om, g);
  }
  for (i = 0; i < reg->objects->len; i++) {
    GObject *o = g_ptr_array_index (reg->objects, i);
    wp_object_manager_add_object (om, o);
  }
}

/* WpGlobal */

G_DEFINE_BOXED_TYPE (WpGlobal, wp_global, wp_global_ref, wp_global_unref)

void
wp_global_rm_flag (WpGlobal *global, guint rm_flag)
{
  WpRegistry *reg = global->registry;

  /* no flag to remove */
  if (!(global->flags & rm_flag))
    return;

  /* global was owned by the proxy; by removing the flag, we clear out
     also the proxy pointer, which is presumably no longer valid and we
     notify all listeners that the proxy is gone */
  if (rm_flag == WP_GLOBAL_FLAG_OWNED_BY_PROXY) {
    global->flags &= ~WP_GLOBAL_FLAG_OWNED_BY_PROXY;
    if (reg)
      wp_registry_notify_rm_object (reg, global->proxy);
    global->proxy = NULL;
  }
  /* registry removed the global */
  else if (rm_flag == WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY) {
    global->flags &= ~WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY;

    /* if there is a proxy and it's not owning the global, destroy it */
    if (global->flags == 0 && global->proxy) {
      if (reg)
        wp_registry_notify_rm_object (reg, global->proxy);
      wp_proxy_destroy (global->proxy);
      g_clear_object (&global->proxy);
    }
  }

  /* drop the registry's ref on global when it has no flags anymore */
  if (global->flags == 0 && reg) {
    g_clear_pointer (&g_ptr_array_index (reg->globals, global->id), wp_global_unref);
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
