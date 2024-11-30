/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Raghavendra Rao <raghavendra.rao@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "metadata.h"
#include "core.h"
#include "log.h"
#include "error.h"
#include "wpenums.h"

#include <pipewire/impl.h>
#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-metadata")

/*! \defgroup wpmetadata WpMetadata */
/*!
 * \struct WpMetadata
 *
 * The WpMetadata class allows accessing the properties and methods of
 * PipeWire metadata object (`struct pw_metadata`).
 *
 * A WpMetadata is constructed internally when a new metadata object appears on the
 * PipeWire registry and it is made available through the WpObjectManager API.
 *
 * \gsignals
 *
 * \par changed
 * \parblock
 * \code
 * void
 * changed_callback (WpMetadata * self,
 *                   guint subject,
 *                   gchar * key,
 *                   gchar * type,
 *                   gchar * value,
 *                   gpointer user_data)
 * \endcode
 * Emitted when metadata change
 *
 * Parameters:
 * - `subject` - the metadata subject id
 * - `key` - the metadata key
 * - `type` - the value type
 * - `value` - the metadata value
 *
 * Flags: G_SIGNAL_RUN_LAST
 * \endparblock
 */
enum {
  SIGNAL_CHANGED,
  N_SIGNALS,
};

static guint32 signals[N_SIGNALS] = {0};

/* data structure */

struct item
{
  uint32_t subject;
  gchar *key;
  gchar *type;
  gchar *value;
};

static void
set_item (struct item * item, uint32_t subject, const char * key,
    const char * type, const char * value)
{
  item->subject = subject;
  item->key = g_strdup (key);
  item->type = g_strdup (type);
  item->value = g_strdup (value);
}

static void
clear_item (struct item * item)
{
  g_free (item->key);
  g_free (item->type);
  g_free (item->value);
  spa_zero (*item);
}

static struct item *
find_item (struct pw_array * metadata, uint32_t subject, const char * key)
{
  struct item *item;

  pw_array_for_each (item, metadata) {
    if (item->subject == subject && (key == NULL || !strcmp (item->key, key))) {
      return item;
    }
  }
  return NULL;
}

static int
clear_subject (struct pw_array * metadata, uint32_t subject)
{
  struct item *item;
  uint32_t removed = 0;

  while (true) {
    item = find_item (metadata, subject, NULL);
    if (item == NULL)
      break;
    clear_item (item);
    pw_array_remove (metadata, item);
    removed++;
  }

  return removed;
}

static void
clear_items (struct pw_array * metadata)
{
  struct item *item;

  pw_array_consume (item, metadata) {
    clear_item (item);
    pw_array_remove (metadata, item);
  }
  pw_array_reset (metadata);
}

typedef struct _WpMetadataPrivate WpMetadataPrivate;
struct _WpMetadataPrivate
{
  struct pw_metadata *iface;
  struct spa_hook listener;
  struct pw_array metadata;
  gboolean remove_listener;
};

G_DEFINE_TYPE_WITH_PRIVATE (WpMetadata, wp_metadata, WP_TYPE_GLOBAL_PROXY)

static void
wp_metadata_init (WpMetadata * self)
{
  WpMetadataPrivate *priv = wp_metadata_get_instance_private (self);
  pw_array_init (&priv->metadata, 4096);
}

static void
wp_metadata_finalize (GObject * object)
{
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (object));

  pw_array_clear (&priv->metadata);

  G_OBJECT_CLASS (wp_metadata_parent_class)->finalize (object);
}

static WpObjectFeatures
wp_metadata_get_supported_features (WpObject * object)
{
  return WP_PROXY_FEATURE_BOUND | WP_METADATA_FEATURE_DATA;
}

enum {
  STEP_BIND = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_CACHE
};

static guint
wp_metadata_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  g_return_val_if_fail (
      missing & (WP_PROXY_FEATURE_BOUND | WP_METADATA_FEATURE_DATA),
      WP_TRANSITION_STEP_ERROR);

  /* bind if not already bound */
  if (missing & WP_PROXY_FEATURE_BOUND)
    return STEP_BIND;
  else
    return STEP_CACHE;
}

static void
wp_metadata_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case STEP_CACHE:
    /* just wait for initial_sync_done() */
    break;
  default:
    WP_OBJECT_CLASS (wp_metadata_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static int
metadata_event_property (void *object, uint32_t subject, const char *key,
    const char *type, const char *value)
{
  WpMetadata *self = WP_METADATA (object);
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));
  struct item *item = NULL;

  if (key == NULL) {
    if (clear_subject (&priv->metadata, subject) > 0) {
      wp_debug_object (self, "remove id:%d", subject);
      g_signal_emit (self, signals[SIGNAL_CHANGED], 0, subject, NULL, NULL,
          NULL);
    }
    return 0;
  }

  item = find_item (&priv->metadata, subject, key);
  if (item == NULL) {
    if (value == NULL)
      return 0;
    item = pw_array_add (&priv->metadata, sizeof (*item));
    if (item == NULL)
      return -errno;
  } else {
    clear_item (item);
  }

  if (value != NULL) {
    if (type == NULL)
      type = "string";
    set_item (item, subject, key, type, value);
    wp_debug_object (self, "add id:%d key:%s type:%s value:%s",
        subject, key, type, value);
  } else {
    type = NULL;
    pw_array_remove (&priv->metadata, item);
    wp_debug_object (self, "remove id:%d key:%s", subject, key);
  }

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0, subject, key, type, value);
  return 0;
}

static const struct pw_metadata_events metadata_events = {
  PW_VERSION_METADATA_EVENTS,
  .property = metadata_event_property,
};

static void
initial_sync_done (WpCore * core, GAsyncResult * res, WpMetadata * self)
{
  g_autoptr (GError) error = NULL;
  if (!wp_core_sync_finish (core, res, &error)) {
    wp_warning_object (self, "core sync error: %s", error->message);
    return;
  }

  wp_object_update_features (WP_OBJECT (self), WP_METADATA_FEATURE_DATA, 0);
}

static void
wp_metadata_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpMetadata *self = WP_METADATA (proxy);
  WpMetadataPrivate *priv = wp_metadata_get_instance_private (self);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  priv->iface = (struct pw_metadata *) pw_proxy;
  pw_metadata_add_listener (priv->iface, &priv->listener,
      &metadata_events, self);
  priv->remove_listener = TRUE;
  wp_core_sync_closure (core, NULL,
      g_cclosure_new_object ((GCallback) initial_sync_done, G_OBJECT (self)));
}

static void
wp_metadata_pw_proxy_destroyed (WpProxy * proxy)
{
  WpMetadata *self = WP_METADATA (proxy);
  WpMetadataPrivate *priv = wp_metadata_get_instance_private (self);

  if (priv->remove_listener) {
    spa_hook_remove (&priv->listener);
    priv->remove_listener = FALSE;
  }
  clear_items (&priv->metadata);
  wp_object_update_features (WP_OBJECT (self), 0, WP_METADATA_FEATURE_DATA);

  WP_PROXY_CLASS (wp_metadata_parent_class)->pw_proxy_destroyed (proxy);
}

static void
wp_metadata_class_init (WpMetadataClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_metadata_finalize;

  wpobject_class->get_supported_features = wp_metadata_get_supported_features;
  wpobject_class->activate_get_next_step = wp_metadata_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_metadata_activate_execute_step;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Metadata;
  proxy_class->pw_iface_version = PW_VERSION_METADATA;
  proxy_class->pw_proxy_created = wp_metadata_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_metadata_pw_proxy_destroyed;

  signals[SIGNAL_CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 4,
      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
}

/*!
 * \struct WpMetadataItem
 *
 * WpMetadataItem holds the subject, key, type and value of a metadata entry.
 */
struct _WpMetadataItem
{
  WpMetadata *metadata;
  guint32 subject;
  const gchar *key;
  const gchar *type;
  const gchar *value;
};

G_DEFINE_BOXED_TYPE (WpMetadataItem, wp_metadata_item,
    wp_metadata_item_ref, wp_metadata_item_unref)

static WpMetadataItem *
wp_metadata_item_new (WpMetadata *metadata, guint32 subject, const gchar *key,
    const gchar *type, const gchar *value)
{
  WpMetadataItem *self = g_rc_box_new0 (WpMetadataItem);
  self->metadata = g_object_ref (metadata);
  self->subject = subject;
  self->key = key;
  self->type = type;
  self->value = value;
  return self;
}

static void
wp_metadata_item_free (gpointer p)
{
  WpMetadataItem *self = p;
  g_clear_object (&self->metadata);
}

/*!
 * \brief Increases the reference count of a metadata item object
 * \ingroup wpmetadata
 * \param self a metadata item object
 * \returns (transfer full): \a self with an additional reference count on it
 * \since 0.5.0
 */
WpMetadataItem *
wp_metadata_item_ref (WpMetadataItem *self)
{
  return g_rc_box_acquire (self);
}

/*!
 * \brief Decreases the reference count on \a self and frees it when the ref
 * count reaches zero.
 * \ingroup wpmetadata
 * \param self (transfer full): a metadata item object
 * \since 0.5.0
 */
void
wp_metadata_item_unref (WpMetadataItem *self)
{
  g_rc_box_release_full (self, wp_metadata_item_free);
}

/*!
 * \brief Gets the subject from a metadata item
 *
 * \ingroup wpmetadata
 * \param self the item held by the GValue that was returned from the WpIterator
 *   of wp_metadata_new_iterator()
 * \returns the metadata subject of the \a item
 * \since 0.5.0
 */
guint32
wp_metadata_item_get_subject (WpMetadataItem * self)
{
  return self->subject;
}

/*!
 * \brief Gets the key from a metadata item
 *
 * \ingroup wpmetadata
 * \param self the item held by the GValue that was returned from the WpIterator
 *   of wp_metadata_new_iterator()
 * \returns (transfer none): the metadata key of the \a item
 * \since 0.5.0
 */
const gchar *
wp_metadata_item_get_key (WpMetadataItem * self)
{
  return self->key;
}

/*!
 * \brief Gets the value type from a metadata item
 *
 * \ingroup wpmetadata
 * \param self the item held by the GValue that was returned from the WpIterator
 *   of wp_metadata_new_iterator()
 * \returns (transfer none): the metadata value type of the \a item
 * \since 0.5.0
 */
const gchar *
wp_metadata_item_get_value_type (WpMetadataItem * self)
{
  return self->type;
}

/*!
 * \brief Gets the value from a metadata item
 *
 * \ingroup wpmetadata
 * \param self the item held by the GValue that was returned from the WpIterator
 *   of wp_metadata_new_iterator()
 * \returns (transfer none): the metadata value of the \a item
 * \since 0.5.0
 */
const gchar *
wp_metadata_item_get_value (WpMetadataItem * self)
{
  return self->value;
}

struct metadata_iterator_data
{
  WpMetadata *metadata;
  const struct item *item;
  guint32 subject;
};

static void
metadata_iterator_reset (WpIterator *it)
{
  struct metadata_iterator_data *it_data = wp_iterator_get_user_data (it);
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (it_data->metadata);

  it_data->item = pw_array_first (&priv->metadata);
}

static gboolean
metadata_iterator_next (WpIterator *it, GValue *item)
{
  struct metadata_iterator_data *it_data = wp_iterator_get_user_data (it);
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (it_data->metadata);

  while (pw_array_check (&priv->metadata, it_data->item)) {
    if ((it_data->subject == PW_ID_ANY ||
            it_data->subject == it_data->item->subject)) {
      g_autoptr (WpMetadataItem) mi = wp_metadata_item_new (it_data->metadata,
          it_data->item->subject, it_data->item->key, it_data->item->type,
          it_data->item->value);
      g_value_init (item, WP_TYPE_METADATA_ITEM);
      g_value_take_boxed (item, g_steal_pointer (&mi));
      it_data->item++;
      return TRUE;
    }
    it_data->item++;
  }
  return FALSE;
}

static gboolean
metadata_iterator_fold (WpIterator *it, WpIteratorFoldFunc func, GValue *ret,
    gpointer data)
{
  struct metadata_iterator_data *it_data = wp_iterator_get_user_data (it);
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (it_data->metadata);
  const struct item *i;

  pw_array_for_each (i, &priv->metadata) {
    if ((it_data->subject == PW_ID_ANY ||
            it_data->subject == it_data->item->subject)) {
      g_auto (GValue) item = G_VALUE_INIT;
      g_autoptr (WpMetadataItem) mi = wp_metadata_item_new (it_data->metadata,
          it_data->item->subject, it_data->item->key, it_data->item->type,
          it_data->item->value);
      g_value_init (&item, WP_TYPE_METADATA_ITEM);
      g_value_take_boxed (&item, g_steal_pointer (&mi));
      if (!func (&item, ret, data))
        return FALSE;
    }
  }
  return TRUE;
}

static void
metadata_iterator_finalize (WpIterator *it)
{
  struct metadata_iterator_data *it_data = wp_iterator_get_user_data (it);
  g_object_unref (it_data->metadata);
}

static const WpIteratorMethods metadata_iterator_methods = {
  .version = WP_ITERATOR_METHODS_VERSION,
  .reset = metadata_iterator_reset,
  .next = metadata_iterator_next,
  .fold = metadata_iterator_fold,
  .finalize = metadata_iterator_finalize,
};

/*!
 * \brief Iterates over metadata items that matches the given \a subject.
 *
 * If no constraints are specified, the returned iterator iterates over all the
 * stored metadata.
 *
 * Note that this method works on cached metadata. When you change metadata
 * with wp_metadata_set(), this cache will be updated on the next round-trip
 * with the pipewire server.
 *
 * \ingroup wpmetadata
 * \param self a metadata object
 * \param subject the metadata subject id, or -1 (PW_ID_ANY)
 * \returns (transfer full): an iterator that iterates over the found metadata.
 *   The type of the iterator item is WpMetadataItem.
 */
WpIterator *
wp_metadata_new_iterator (WpMetadata * self, guint32 subject)
{
  WpMetadataPrivate *priv;
  g_autoptr (WpIterator) it = NULL;
  struct metadata_iterator_data *it_data;

  g_return_val_if_fail (self != NULL, NULL);
  priv = wp_metadata_get_instance_private (self);

  it = wp_iterator_new (&metadata_iterator_methods,
      sizeof (struct metadata_iterator_data));
  it_data = wp_iterator_get_user_data (it);
  it_data->metadata = g_object_ref (self);
  it_data->item = pw_array_first (&priv->metadata);
  it_data->subject = subject;
  return g_steal_pointer (&it);
}

/*!
 * \brief Finds the metadata value given its \a subject and \a key.
 *
 * \ingroup wpmetadata
 * \param self a metadata object
 * \param subject the metadata subject id
 * \param key the metadata key name
 * \param type (out)(optional): the metadata type name
 * \returns the metadata string value, or NULL if not found.
 */
const gchar *
wp_metadata_find (WpMetadata * self, guint32 subject, const gchar * key,
  const gchar ** type)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  it = wp_metadata_new_iterator (self, subject);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpMetadataItem *mi = g_value_get_boxed (&val);
    const gchar *k = wp_metadata_item_get_key (mi);
    const gchar *t = wp_metadata_item_get_value_type (mi);
    const gchar *v = wp_metadata_item_get_value (mi);
    if (g_strcmp0 (k, key) == 0) {
      if (type)
        *type = t;
      g_value_unset (&val);
      return v;
    }
  }
  return NULL;
}

/*!
 * \brief Sets the metadata associated with the given \a subject and \a key.
 * Use NULL as a value to unset the given \a key and use NULL in both \a key
 * and \a value to remove all metadata associated with the given \a subject.
 *
 * \ingroup wpmetadata
 * \param self the metadata object
 * \param subject the subject id for which this metadata property is being set
 * \param key (nullable): the key to set, or NULL to remove all metadata for
 *   \a subject
 * \param type (nullable): the type of the value; NULL is synonymous to "string"
 * \param value (nullable): the value to set, or NULL to unset the given \a key
 */
void
wp_metadata_set (WpMetadata * self, guint32 subject,
    const gchar * key, const gchar * type, const gchar * value)
{
  WpMetadataPrivate *priv = wp_metadata_get_instance_private (self);
  pw_metadata_set_property (priv->iface, subject, key, type, value);
}

/*!
 * \brief Clears permanently all stored metadata.
 * \ingroup wpmetadata
 * \param self the metadata object
 */
void
wp_metadata_clear (WpMetadata * self)
{
  WpMetadataPrivate *priv = wp_metadata_get_instance_private (self);
  pw_metadata_clear (priv->iface);
}

/*!
 * \struct WpImplMetadata
 * Implementation of the metadata object.
 *
 * Activate this object with at least WP_PROXY_FEATURE_BOUND to export it to
 * PipeWire.
 */
struct _WpImplMetadata
{
  WpMetadata parent;

  gchar *name;
  WpProperties *properties;

  struct pw_impl_metadata *impl;
  struct spa_hook listener;
};

enum {
  PROP_0,
  PROP_NAME,
  PROP_PROPERTIES,
};

G_DEFINE_TYPE (WpImplMetadata, wp_impl_metadata, WP_TYPE_METADATA)

static void
wp_impl_metadata_init (WpImplMetadata * self)
{
}

static const struct pw_impl_metadata_events impl_metadata_events = {
  PW_VERSION_IMPL_METADATA_EVENTS,
  .property = metadata_event_property,
};

static void
wp_impl_metadata_constructed (GObject *object)
{
  WpImplMetadata *self = WP_IMPL_METADATA (object);
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));
  g_autoptr (WpCore) core = NULL;
  struct pw_context *pw_context;
  struct pw_properties *props = NULL;

  core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);
  pw_context = wp_core_get_pw_context (core);
  g_return_if_fail (pw_context);

  if (self->properties)
    props = wp_properties_to_pw_properties (self->properties);

  self->impl = pw_context_create_metadata (pw_context, self->name, props , 0);
  g_return_if_fail (self->impl);
  priv->iface = pw_impl_metadata_get_implementation (self->impl);
  g_return_if_fail (priv->iface);

  pw_impl_metadata_add_listener (self->impl, &self->listener,
      &impl_metadata_events, self);

  wp_object_update_features (WP_OBJECT (self), WP_METADATA_FEATURE_DATA, 0);
  G_OBJECT_CLASS (wp_impl_metadata_parent_class)->constructed (object);
}

static void
wp_impl_metadata_finalize (GObject * object)
{
  WpImplMetadata *self = WP_IMPL_METADATA (object);

  spa_hook_remove (&self->listener);
  g_clear_pointer (&self->impl, pw_impl_metadata_destroy);
  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_pointer (&self->name, g_free);

  G_OBJECT_CLASS (wp_impl_metadata_parent_class)->finalize (object);
}

static void
wp_impl_metadata_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpImplMetadata *self = WP_IMPL_METADATA (object);

  switch (property_id) {
  case PROP_NAME:
    g_clear_pointer (&self->name, g_free);
    self->name = g_value_dup_string (value);
    break;
  case PROP_PROPERTIES:
    g_clear_pointer (&self->properties, wp_properties_unref);
    self->properties = g_value_dup_boxed (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_impl_metadata_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpImplMetadata *self = WP_IMPL_METADATA (object);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, self->name);
    break;
  case PROP_PROPERTIES:
    g_value_set_boxed (value, self->properties);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_impl_metadata_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpImplMetadata *self = WP_IMPL_METADATA (object);
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));

  switch (step) {
  case STEP_BIND: {
    g_autoptr (WpCore) core = wp_object_get_core (object);
    struct pw_core *pw_core = wp_core_get_pw_core (core);
    const struct pw_properties *props = NULL;

    /* no pw_core -> we are not connected */
    if (!pw_core) {
      wp_transition_return_error (WP_TRANSITION (transition), g_error_new (
              WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
              "The WirePlumber core is not connected; "
              "object cannot be exported to PipeWire"));
      return;
    }

    props = pw_impl_metadata_get_properties (self->impl);
    wp_proxy_set_pw_proxy (WP_PROXY (self), pw_core_export (pw_core,
            PW_TYPE_INTERFACE_Metadata, &props->dict, priv->iface, 0)
    );
    break;
  }
  case STEP_CACHE:
    /* never reached because WP_METADATA_FEATURE_DATA is always enabled */
    g_assert_not_reached ();
    break;
  default:
    WP_OBJECT_CLASS (wp_impl_metadata_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static void
wp_impl_metadata_class_init (WpImplMetadataClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->constructed = wp_impl_metadata_constructed;
  object_class->finalize = wp_impl_metadata_finalize;
  object_class->set_property = wp_impl_metadata_set_property;
  object_class->get_property = wp_impl_metadata_get_property;

  wpobject_class->activate_execute_step =
      wp_impl_metadata_activate_execute_step;

  /* disable adding a listener for events */
  proxy_class->pw_proxy_created = NULL;

  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "The metadata name", "",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties",
          "The metadata properties", WP_TYPE_PROPERTIES,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Creates a new metadata implementation
 * \ingroup wpmetadata
 * \param core the core
 * \returns (transfer full): a new WpImplMetadata
 */
WpImplMetadata *
wp_impl_metadata_new (WpCore * core)
{
  return wp_impl_metadata_new_full (core, NULL, NULL);
}

/*!
 * \brief Creates a new metadata implementation with name and properties
 * \ingroup wpmetadata
 * \param core the core
 * \param name (nullable): the metadata name
 * \param properties (nullable) (transfer full): the metadata properties
 * \returns (transfer full): a new WpImplMetadata
 * \since 0.4.3
 */
WpImplMetadata *
wp_impl_metadata_new_full (WpCore * core, const gchar *name,
    WpProperties *properties)
{
  g_autoptr (WpProperties) props = properties;

  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_IMPL_METADATA,
      "core", core,
      "name", name,
      "properties", props,
      NULL);
}
