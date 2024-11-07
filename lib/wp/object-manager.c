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
 * \param interest (transfer full): the interest
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

/*!
 * \brief Checks if the object manager should emit the 'objects-changed' signal
 * \private
 * \ingroup wpobjectmanager
 * \param self the object manager
 */
void
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

/*!
 * \brief Adds an object to the object manager.
 * \private
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \param object (transfer none): the object to add
 * \note caller must also call wp_object_manager_maybe_objects_changed() after
 */
void
wp_object_manager_add_object (WpObjectManager * self, gpointer object)
{
  if (wp_object_manager_is_interested_in_object (self, object)) {
    wp_trace_object (self, "added: " WP_OBJECT_FORMAT, WP_OBJECT_ARGS (object));
    g_ptr_array_add (self->objects, object);
    g_signal_emit (self, signals[SIGNAL_OBJECT_ADDED], 0, object);
    self->changed = TRUE;
  }
}

/*!
 * \brief Removes an object from the object manager.
 * \private
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \param object the object to remove
 * \note caller must also call wp_object_manager_maybe_objects_changed() after
 */
void
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

/*!
 * \brief Adds a global object to the object manager.
 * \private
 * \ingroup wpobjectmanager
 * \param self the object manager
 * \param global the global object to add
 * \note caller must also call wp_object_manager_maybe_objects_changed() after
 */
void
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

  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (WP_IS_OBJECT_MANAGER (om));

  g_weak_ref_set (&om->core, self);

  reg = wp_core_get_registry (self);
  wp_registry_install_object_manager (reg, om);
}
