/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "object-manager.h"
#include "log.h"
#include "proxy-interfaces.h"
#include "private/registry.h"

#include <pipewire/pipewire.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-object-manager")

/*! \defgroup wpobjectmanager WpObjectManager */
/*!
 * \struct WpObjectManager
 *
 * The WpObjectManager class provides a way to collect a set of objects and
 * be notified when objects that fulfill a certain set of criteria are created
 * or destroyed.
 *
 * There are 4 kinds of objects that can be managed by a
 * WpObjectManager:
 *   * remote PipeWire global objects that are advertised on the registry;
 *     these are bound locally to subclasses of WpGlobalProxy
 *   * remote PipeWire global objects that are created by calling a remote
 *     factory through the WirePlumber API; these are very similar to other
 *     global objects but it should be noted that the same WpGlobalProxy
 *     instance that created them appears in the WpObjectManager (as soon as
 *     its WP_PROXY_FEATURE_BOUND is enabled)
 *   * local PipeWire objects that are being exported to PipeWire
 *     (WpImplMetadata, WpImplNode, etc); these appear in the
 *     WpObjectManager as soon as they are exported (so, when their
 *     WP_PROXY_FEATURE_BOUND is enabled)
 *   * WirePlumber-specific objects, such as plugins, factories and session items
 *
 * To start an object manager, you first need to declare interest in a certain
 * kind of object by calling wp_object_manager_add_interest() and then install
 * it on the WpCore with wp_core_install_object_manager().
 *
 * Upon installing a WpObjectManager on a WpCore, any pre-existing objects
 * that match the interests of this WpObjectManager will immediately become
 * available to get through wp_object_manager_new_iterator() and the
 * WpObjectManager \c object-added signal will be emitted for all of them.
 * However, note that if these objects need to be prepared (to activate some
 * features on them), the emission of \c object-added will be delayed. To know
 * when it is safe to access the initial set of objects, wait until the
 * \c installed signal has been emitted. That signal is emitted asynchronously
 * after all the initial objects have been prepared.
 *
 * \gproperties
 *
 * \gproperty{core, WpCore *, G_PARAM_READABLE, The core}
 *
 * \gsignals
 *
 * \par installed
 * \parblock
 * \code
 * void
 * installed_callback (WpObjectManager * self,
 *                     gpointer user_data)
 * \endcode
 *
 * This is emitted once after the object manager is installed with
 * wp_core_install_object_manager(). If there are objects that need to be
 * prepared asynchronously internally, emission of this signal is delayed
 * until all objects are ready.
 *
 * Flags: G_SIGNAL_RUN_FIRST
 * \endparblock
 *
 * \par object-added
 * \parblock
 * \code
 * void
 * object_added_callback (WpObjectManager * self,
 *                        gpointer object,
 *                        gpointer user_data)
 * \endcode
 *
 * Emitted when an object that matches the interests of this object manager
 * is made available.
 *
 * Parameters:
 * - `object` - the managed object that was just added
 *
 * Flags: G_SIGNAL_RUN_FIRST
 * \endparblock
 *
 * \par object-removed
 * \parblock
 * \code
 * void
 * object_removed_callback (WpObjectManager * self,
 *                          gpointer object,
 *                          gpointer user_data)
 * \endcode
 *
 * Emitted when an object that was previously added on this object manager is
 * now being removed (and most likely destroyed). At the time that this signal
 * is emitted, the object is still alive.
 *
 * Parameters:
 * - `object` - the managed object that is being removed
 *
 * Flags: G_SIGNAL_RUN_FIRST
 * \endparblock
 *
 * \par objects-changed
 * \parblock
 * \code
 * void
 * objects_changed_callback (WpObjectManager * self,
 *                          gpointer user_data)
 * \endcode
 *
 * Emitted when one or more objects have been recently added or removed from
 * this object manager. This signal is useful to get notified only once when
 * multiple changes happen in a short timespan. The receiving callback may
 * retrieve the updated list of objects by calling wp_object_manager_new_iterator()
 *
 * Flags: G_SIGNAL_RUN_FIRST
 * \endparblock
 */

struct _WpObjectManager
{
  GObject parent;
  GWeakRef core;

  /* element-type: WpObjectInterest* */
  GPtrArray *interests;
  /* element-type: <GType, WpProxyFeatures> */
  GHashTable *features;
  /* objects that we are interested in, without a ref */
  GPtrArray *objects;

  gboolean installed;
  gboolean changed;
  guint pending_objects;
  GSource *idle_source;
};

enum {
  PROP_0,
  PROP_CORE,
};

enum {
  SIGNAL_OBJECT_ADDED,
  SIGNAL_OBJECT_REMOVED,
  SIGNAL_OBJECTS_CHANGED,
  SIGNAL_INSTALLED,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (WpObjectManager, wp_object_manager, G_TYPE_OBJECT)

static void
wp_object_manager_init (WpObjectManager * self)
{
  g_weak_ref_init (&self->core, NULL);
  self->interests = g_ptr_array_new_with_free_func (
      (GDestroyNotify) wp_object_interest_unref);
  self->features = g_hash_table_new (g_direct_hash, g_direct_equal);
  self->objects = g_ptr_array_new ();
  self->installed = FALSE;
  self->changed = FALSE;
  self->pending_objects = 0;
}

static void
wp_object_manager_finalize (GObject * object)
{
  WpObjectManager *self = WP_OBJECT_MANAGER (object);

  if (self->idle_source) {
    g_source_destroy (self->idle_source);
    g_clear_pointer (&self->idle_source, g_source_unref);
  }
  g_clear_pointer (&self->objects, g_ptr_array_unref);
  g_clear_pointer (&self->features, g_hash_table_unref);
  g_clear_pointer (&self->interests, g_ptr_array_unref);
  g_weak_ref_clear (&self->core);

  G_OBJECT_CLASS (wp_object_manager_parent_class)->finalize (object);
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

  /* Install the properties */

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The WpCore", WP_TYPE_CORE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_OBJECT_ADDED] = g_signal_new (
      "object-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_OBJECT);

  signals[SIGNAL_OBJECT_REMOVED] = g_signal_new (
      "object-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_OBJECT);

  signals[SIGNAL_OBJECTS_CHANGED] = g_signal_new (
      "objects-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  signals[SIGNAL_INSTALLED] = g_signal_new (
      "installed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*!
 * \brief Constructs a new object manager.
 * \ingroup wpobjectmanager
 * \returns (transfer full): the newly constructed object manager
 */
WpObjectManager *
wp_object_manager_new (void)
{
  return g_object_new (WP_TYPE_OBJECT_MANAGER, NULL);
}

/*!
 * \brief Checks if an object manager is installed.
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \returns TRUE if the object manager is installed (i.e. the
 *   WpObjectManager \c installed signal has been emitted), FALSE otherwise
 */
gboolean
wp_object_manager_is_installed (WpObjectManager * self)
{
  g_return_val_if_fail (WP_IS_OBJECT_MANAGER (self), FALSE);
  return self->installed;
}

/*!
 * \brief Equivalent to:
 * \code
 * WpObjectInterest *i = wp_object_interest_new (gtype, ...);
 * wp_object_manager_add_interest_full (self, i);
 * \endcode
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \param gtype the GType of the objects that we are declaring interest in
 * \param ... a list of constraints, terminated by NULL
 */
void
wp_object_manager_add_interest (WpObjectManager * self, GType gtype, ...)
{
  WpObjectInterest *interest;
  va_list args;

  g_return_if_fail (WP_IS_OBJECT_MANAGER (self));

  va_start (args, gtype);
  interest = wp_object_interest_new_valist (gtype, &args);
  wp_object_manager_add_interest_full (self, interest);
  va_end (args);
}

/*!
 * \brief Declares interest in a certain kind of object.
 *
 * Interest consists of a GType that the object must be an ancestor of
 * (g_type_is_a() must match) and optionally, a set of additional constraints
 * on certain properties of the object. Refer to WpObjectInterest for more details.
 *
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \param interest (transfer full): the interest
 */
void
wp_object_manager_add_interest_full (WpObjectManager *self,
    WpObjectInterest * interest)
{
  g_autoptr (GError) error = NULL;

  g_return_if_fail (WP_IS_OBJECT_MANAGER (self));

  if (G_UNLIKELY (!wp_object_interest_validate (interest, &error))) {
    wp_critical_object (self, "interest validation failed: %s",
        error->message);
    wp_object_interest_unref (interest);
    return;
  }
  g_ptr_array_add (self->interests, interest);
}

static void
store_children_object_features (GHashTable *store, GType object_type,
    WpObjectFeatures wanted_features)
{
  g_autofree GType *children = NULL;
  GType *child;

  child = children = g_type_children (object_type, NULL);
  while (*child) {
    WpObjectFeatures existing_ft = (WpObjectFeatures) GPOINTER_TO_UINT (
        g_hash_table_lookup (store, GSIZE_TO_POINTER (*child)));
    g_hash_table_insert (store, GSIZE_TO_POINTER (*child),
        GUINT_TO_POINTER (existing_ft | wanted_features));
    store_children_object_features (store, *child, wanted_features);
    child++;
  }
}

/*!
 * \brief Requests the object manager to automatically prepare the
 * \a wanted_features on any managed object that is of the specified
 * \a object_type.
 *
 * These features will always be prepared before the object appears on the
 * object manager.
 *
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \param object_type the WpProxy descendant type
 * \param wanted_features the features to enable on this kind of object
 */
void
wp_object_manager_request_object_features (WpObjectManager *self,
    GType object_type, WpObjectFeatures wanted_features)
{
  g_return_if_fail (WP_IS_OBJECT_MANAGER (self));
  g_return_if_fail (g_type_is_a (object_type, WP_TYPE_OBJECT));

  g_hash_table_insert (self->features, GSIZE_TO_POINTER (object_type),
      GUINT_TO_POINTER (wanted_features));
  store_children_object_features (self->features, object_type, wanted_features);
}

/*!
 * \brief Gets the number of objects managed by the object manager.
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \returns the number of objects managed by this WpObjectManager
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
  GPtrArray *objects;
  WpObjectInterest *interest;
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

  while (it_data->index < it_data->objects->len) {
    gpointer obj = g_ptr_array_index (it_data->objects, it_data->index++);

    /* take the next object that matches the interest, if any */
    if (!it_data->interest ||
        wp_object_interest_matches (it_data->interest, obj)) {
      g_value_init_from_instance (item, obj);
      return TRUE;
    }
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

  obj = base = it_data->objects->pdata;
  len = it_data->objects->len;

  while ((obj - base) < len) {
    /* only pass matching objects to the fold func if we have an interest */
    if (!it_data->interest ||
        wp_object_interest_matches (it_data->interest, *obj)) {
      g_auto (GValue) item = G_VALUE_INIT;
      g_value_init_from_instance (&item, *obj);
      if (!func (&item, ret, data))
        return FALSE;
    }
    obj++;
  }
  return TRUE;
}

static void
om_iterator_finalize (WpIterator *it)
{
  struct om_iterator_data *it_data = wp_iterator_get_user_data (it);
  g_clear_pointer (&it_data->objects, g_ptr_array_unref);
  g_clear_pointer (&it_data->interest, wp_object_interest_unref);
  g_object_unref (it_data->om);
}

static const WpIteratorMethods om_iterator_methods = {
  .version = WP_ITERATOR_METHODS_VERSION,
  .reset = om_iterator_reset,
  .next = om_iterator_next,
  .fold = om_iterator_fold,
  .finalize = om_iterator_finalize,
};

/*!
 * \brief Iterates through all the objects managed by this object manager.
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \returns (transfer full): a WpIterator that iterates over all the managed
 *   objects of this object manager
 */
WpIterator *
wp_object_manager_new_iterator (WpObjectManager * self)
{
  WpIterator *it;
  struct om_iterator_data *it_data;

  g_return_val_if_fail (WP_IS_OBJECT_MANAGER (self), NULL);

  it = wp_iterator_new (&om_iterator_methods, sizeof (struct om_iterator_data));
  it_data = wp_iterator_get_user_data (it);
  it_data->om = g_object_ref (self);
  it_data->objects = g_ptr_array_copy (self->objects, NULL, NULL);
  it_data->index = 0;
  return it;
}

/*!
 * \brief Equivalent to:
 * \code
 * WpObjectInterest *i = wp_object_interest_new (gtype, ...);
 * return wp_object_manager_new_filtered_iterator_full (self, i);
 * \endcode
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \param gtype the GType of the objects to iterate through
 * \param ... a list of constraints, terminated by NULL
 * \returns (transfer full): a WpIterator that iterates over all the matching
 *   objects of this object manager
 */
WpIterator *
wp_object_manager_new_filtered_iterator (WpObjectManager * self, GType gtype,
    ...)
{
  WpObjectInterest *interest;
  va_list args;

  g_return_val_if_fail (WP_IS_OBJECT_MANAGER (self), NULL);

  va_start (args, gtype);
  interest = wp_object_interest_new_valist (gtype, &args);
  va_end (args);

  return wp_object_manager_new_filtered_iterator_full (self, interest);
}

/*!
 * \brief Iterates through all the objects managed by this object manager that
 * match the specified \a interest.
 *
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \param interest (transfer full): the interest
 * \returns (transfer full): a WpIterator that iterates over all the matching
 *   objects of this object manager
 */
WpIterator *
wp_object_manager_new_filtered_iterator_full (WpObjectManager * self,
    WpObjectInterest * interest)
{
  WpIterator *it;
  struct om_iterator_data *it_data;
  g_autoptr (GError) error = NULL;

  g_return_val_if_fail (WP_IS_OBJECT_MANAGER (self), NULL);

  if (G_UNLIKELY (!wp_object_interest_validate (interest, &error))) {
    wp_critical_object (self, "interest validation failed: %s",
        error->message);
    wp_object_interest_unref (interest);
    return NULL;
  }

  it = wp_iterator_new (&om_iterator_methods, sizeof (struct om_iterator_data));
  it_data = wp_iterator_get_user_data (it);
  it_data->om = g_object_ref (self);
  it_data->objects = g_ptr_array_copy (self->objects, NULL, NULL);
  it_data->interest = interest;
  it_data->index = 0;
  return it;
}

/*!
 * \brief Equivalent to:
 * \code
 * WpObjectInterest *i = wp_object_interest_new (gtype, ...);
 * return wp_object_manager_lookup_full (self, i);
 * \endcode
 *
 * The constraints specified in the variable arguments must follow the rules
 * documented in wp_object_interest_new().
 *
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \param gtype the GType of the object to lookup
 * \param ... a list of constraints, terminated by NULL
 * \returns (type GObject)(transfer full)(nullable): the first managed object
 *    that matches the lookup interest, or NULL if no object matches
 */
gpointer
wp_object_manager_lookup (WpObjectManager * self, GType gtype, ...)
{
  WpObjectInterest *interest;
  va_list args;

  g_return_val_if_fail (WP_IS_OBJECT_MANAGER (self), NULL);

  va_start (args, gtype);
  interest = wp_object_interest_new_valist (gtype, &args);
  va_end (args);

  return wp_object_manager_lookup_full (self, interest);
}

/*!
 * \brief Searches for an object that matches the specified \a interest and
 * returns it, if found.
 *
 * If more than one objects match, only the first one is returned.
 * To find multiple objects that match certain criteria,
 * wp_object_manager_new_filtered_iterator() is more suitable.
 *
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \param interest (transfer full): the interst
 * \returns (type GObject)(transfer full)(nullable): the first managed object
 *    that matches the lookup interest, or NULL if no object matches
 */
gpointer
wp_object_manager_lookup_full (WpObjectManager * self,
    WpObjectInterest * interest)
{
  g_auto (GValue) ret = G_VALUE_INIT;
  g_autoptr (WpIterator) it =
      wp_object_manager_new_filtered_iterator_full (self, interest);

  if (wp_iterator_next (it, &ret))
    return g_value_dup_object (&ret);

  return NULL;
}

static gboolean
wp_object_manager_is_interested_in_object (WpObjectManager * self,
    GObject * object)
{
  guint i;
  WpObjectInterest *interest = NULL;

  for (i = 0; i < self->interests->len; i++) {
    interest = g_ptr_array_index (self->interests, i);
    if (wp_object_interest_matches (interest, object))
      return TRUE;
  }
  return FALSE;
}

static gboolean
wp_object_manager_is_interested_in_global (WpObjectManager * self,
    WpGlobal * global, WpObjectFeatures * wanted_features)
{
  guint i;
  WpObjectInterest *interest = NULL;

  for (i = 0; i < self->interests->len; i++) {
    interest = g_ptr_array_index (self->interests, i);

    /* check all constraints */
    WpInterestMatch match = wp_object_interest_matches_full (interest,
        WP_INTEREST_MATCH_FLAGS_CHECK_ALL, global->type, global->proxy,
        NULL, global->properties);

    /* and consider the manager interested if the type and the globals match...
       if pw_properties / g_properties fail, that's ok because they are not
       known yet (the proxy is likely NULL and properties not yet retrieved) */
    if (SPA_FLAG_IS_SET (match, (WP_INTEREST_MATCH_GTYPE |
                                 WP_INTEREST_MATCH_PW_GLOBAL_PROPERTIES))) {
      gpointer ft = g_hash_table_lookup (self->features,
          GSIZE_TO_POINTER (global->type));
      *wanted_features = (WpObjectFeatures) GPOINTER_TO_UINT (ft);

      /* force INFO to be present so that we can check PW_PROPERTIES constraints */
      if (!(match & WP_INTEREST_MATCH_PW_PROPERTIES) &&
            !(*wanted_features & WP_PIPEWIRE_OBJECT_FEATURE_INFO) &&
            g_type_is_a (global->type, WP_TYPE_PIPEWIRE_OBJECT))
        *wanted_features |= WP_PIPEWIRE_OBJECT_FEATURE_INFO;

      return TRUE;
    }
  }
  return FALSE;
}

static gboolean
idle_emit_objects_changed (WpObjectManager * self)
{
  g_clear_pointer (&self->idle_source, g_source_unref);

  if (G_UNLIKELY (!self->installed)) {
    wp_trace_object (self, "installed");
    self->installed = TRUE;
    g_signal_emit (self, signals[SIGNAL_INSTALLED], 0);
  }
  wp_trace_object (self, "emit objects-changed");
  g_signal_emit (self, signals[SIGNAL_OBJECTS_CHANGED], 0);

  return G_SOURCE_REMOVE;
}

static void
wp_object_manager_maybe_objects_changed (WpObjectManager * self)
{
  wp_trace_object (self, "pending:%u changed:%d idle_source:%p installed:%d",
      self->pending_objects, self->changed, self->idle_source, self->installed);

  /* always wait until there are no pending objects */
  if (self->pending_objects > 0)
    return;

  /* Emit 'objects-changed' when:
   * - there are no pending objects
   * - object-added or object-removed has been emitted at least once
   */
  if (self->changed) {
    self->changed = FALSE;

    /* schedule emission in idle; if it is already scheduled from earlier,
       there is nothing to do; we will emit objects-changed once for all
       changes... win-win */
    if (!self->idle_source) {
      g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
      if (core) {
        wp_core_idle_add_closure (core, &self->idle_source,
            g_cclosure_new_object (
                G_CALLBACK (idle_emit_objects_changed), G_OBJECT (self)));
      }
    }
  }
  /* Emit 'installed' when:
   * - there are no pending objects
   * - !changed: there was no object added
   * - !installed: not already installed
   * - the registry does not have pending globals; these may be interesting
   * to our object manager, so let's wait a bit until they are released
   * and re-evaluate again later
   * - the registry has globals; if we are on early startup where we don't
   * have any globals yet, wait...
   */
  else if (!self->installed) {
    g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
    if (core) {
      WpRegistry *reg = wp_core_get_registry (core);
      if (reg->tmp_globals->len == 0 && reg->globals->len != 0) {
        wp_trace_object (self, "installed");
        self->installed = TRUE;
        g_signal_emit (self, signals[SIGNAL_INSTALLED], 0);
      }
    }
  }
}

/* caller must also call wp_object_manager_maybe_objects_changed() after */
static void
wp_object_manager_add_object (WpObjectManager * self, gpointer object)
{
  if (wp_object_manager_is_interested_in_object (self, object)) {
    wp_trace_object (self, "added: " WP_OBJECT_FORMAT, WP_OBJECT_ARGS (object));
    g_ptr_array_add (self->objects, object);
    g_signal_emit (self, signals[SIGNAL_OBJECT_ADDED], 0, object);
    self->changed = TRUE;
  }
}

/* caller must also call wp_object_manager_maybe_objects_changed() after */
static void
wp_object_manager_rm_object (WpObjectManager * self, gpointer object)
{
  guint index;
  if (g_ptr_array_find (self->objects, object, &index)) {
    g_ptr_array_remove_index_fast (self->objects, index);
    g_signal_emit (self, signals[SIGNAL_OBJECT_REMOVED], 0, object);
    self->changed = TRUE;
  }
}

static void
on_proxy_ready (GObject * proxy, GAsyncResult * res, gpointer data)
{
  g_autoptr (WpObjectManager) self = WP_OBJECT_MANAGER (data);
  g_autoptr (GError) error = NULL;

  self->pending_objects--;

  if (!wp_object_activate_finish (WP_OBJECT (proxy), res, &error)) {
    wp_debug_object (self, "proxy activation failed: %s", error->message);
  } else {
    wp_object_manager_add_object (self, proxy);
  }

  wp_object_manager_maybe_objects_changed (self);
}

/* caller must also call wp_object_manager_maybe_objects_changed() after */
static void
wp_object_manager_add_global (WpObjectManager * self, WpGlobal * global)
{
  WpProxyFeatures features = 0;

  /* do not allow proxies that don't have a defined subclass;
     bind will fail because proxy_class->pw_iface_type is NULL */
  if (global->type == WP_TYPE_GLOBAL_PROXY)
    return;

  if (wp_object_manager_is_interested_in_global (self, global, &features)) {
    g_autoptr (WpCore) core = g_weak_ref_get (&self->core);

    self->pending_objects++;

    if (!global->proxy)
      global->proxy = g_object_new (global->type,
          "core", core,
          "global", global,
          NULL);

    wp_trace_object (self, "adding global:%u -> " WP_OBJECT_FORMAT,
        global->id, WP_OBJECT_ARGS (global->proxy));

    wp_object_activate (WP_OBJECT (global->proxy), features, NULL,
        on_proxy_ready, g_object_ref (self));
  }
}

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

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "wp-registry"

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

/*!
 * \brief Installs the object manager on this core, activating its internal
 * management engine.
 *
 * This will immediately emit signals about objects added on \a om
 * if objects that the \a om is interested in were in existence already.
 *
 * \ingroup wpobjectmanager
 * \param self the core
 * \param om (transfer none): a WpObjectManager
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
  g_weak_ref_set (&om->core, self);

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
