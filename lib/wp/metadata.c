/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Raghavendra Rao <raghavendra.rao@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: metadata
 * @title: PipeWire Metadata
 */

#define G_LOG_DOMAIN "wp-metadata"

#include "metadata.h"
#include "core.h"
#include "log.h"
#include "error.h"
#include "wpenums.h"

#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>

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

/* WpMetadata */

typedef struct _WpMetadataPrivate WpMetadataPrivate;
struct _WpMetadataPrivate
{
  struct pw_metadata *iface;
  struct spa_hook listener;
  struct pw_array metadata;
};

/**
 * WpMetadata:
 *
 * The #WpMetadata class allows accessing the properties and methods of
 * PipeWire metadata object (`struct pw_metadata`).
 */
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
  wp_core_sync (core, NULL, (GAsyncReadyCallback) initial_sync_done, self);
}

static void
wp_metadata_pw_proxy_destroyed (WpProxy * proxy)
{
  WpMetadata *self = WP_METADATA (proxy);
  WpMetadataPrivate *priv = wp_metadata_get_instance_private (self);

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
      g_value_init (item, G_TYPE_POINTER);
      g_value_set_pointer (item, (gpointer) it_data->item);
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
      g_value_init (&item, G_TYPE_POINTER);
      g_value_set_pointer (&item, (gpointer) i);
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

/**
 * wp_metadata_new_iterator:
 * @self: a metadata object
 * @subject: the metadata subject id, or -1 (PW_ID_ANY)
 *
 * Iterates over metadata items that matches the given @subject. If no
 * constraints are specified, the returned iterator iterates over all the
 * stored metadata.
 *
 * Note that this method works on cached metadata. When you change metadata
 * with wp_metadata_set(), this cache will be updated on the next round-trip
 * with the pipewire server.
 *
 * Returns: (transfer full): an iterator that iterates over the found metadata.
 *   Use wp_metadata_iterator_item_extract() to parse the items returned by
 *   this iterator.
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

/**
 * wp_metadata_iterator_item_extract:
 * @item: a #GValue that was returned from the #WpIterator of wp_metadata_find()
 * @subject: (out)(optional): the subject id of the current item
 * @key: (out)(optional)(transfer none): the key of the current item
 * @type: (out)(optional)(transfer none): the type of the current item
 * @value: (out)(optional)(transfer none): the value of the current item
 *
 * Extracts the metadata subject, key, type and value out of a #GValue that was
 * returned from the #WpIterator of wp_metadata_find()
 */
void
wp_metadata_iterator_item_extract (const GValue * item, guint32 * subject,
    const gchar ** key, const gchar ** type, const gchar ** value)
{
  const struct item *i = g_value_get_pointer (item);
  g_return_if_fail (i != NULL);
  if (subject)
    *subject = i->subject;
  if (key)
    *key = i->key;
  if (type)
    *type = i->type;
  if (value)
    *value = i->value;
}

/**
 * wp_metadata_find:
 * @self: a metadata object
 * @subject: the metadata subject id
 * @key: the metadata key name
 * @type: (out)(optional): the metadata type name
 *
 * Finds the metadata value given its @subject and &key.
 *
 * Returns: the metadata string value, or NULL if not found.
 */
const gchar *
wp_metadata_find (WpMetadata * self, guint32 subject, const gchar * key,
  const gchar ** type)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  it = wp_metadata_new_iterator (self, subject);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    const gchar *k = NULL, *t = NULL, *v = NULL;
    wp_metadata_iterator_item_extract (&val, NULL, &k, &t, &v);
    if (g_strcmp0 (k, key) == 0) {
      if (type)
        *type = t;
      return v;
    }
  }
  return NULL;
}

/**
 * wp_metadata_set:
 * @self: the metadata object
 * @subject: the subject id for which this metadata property is being set
 * @key: (nullable): the key to set, or %NULL to remove all metadata for
 *   @subject
 * @type: (nullable): the type of the value; %NULL is synonymous to "string"
 * @value: (nullable): the value to set, or %NULL to unset the given @key
 *
 * Sets the metadata associated with the given @subject and @key. Use %NULL as
 * a value to unset the given @key and use %NULL in both @key and @value to
 * remove all metadata associated with the given @subject.
 */
void
wp_metadata_set (WpMetadata * self, guint32 subject,
    const gchar * key, const gchar * type, const gchar * value)
{
  WpMetadataPrivate *priv = wp_metadata_get_instance_private (self);
  pw_metadata_set_property (priv->iface, subject, key, type, value);
}

/**
 * wp_metadata_clear:
 * @self: the metadata object
 *
 * Clears permanently all stored metadata.
 */
void
wp_metadata_clear (WpMetadata * self)
{
  WpMetadataPrivate *priv = wp_metadata_get_instance_private (self);
  pw_metadata_clear (priv->iface);
}

/* WpImplMetadata */

struct _WpImplMetadata
{
  WpMetadata parent;

  struct spa_interface iface;
  struct spa_hook_list hooks;
};

/**
 * WpImplMetadata:
 *
 * The #WpImplMetadata class implements a PipeWire metadata object. It can
 * be exported and made available by requesting the %WP_PROXY_FEATURE_BOUND
 * feature.
 */
G_DEFINE_TYPE (WpImplMetadata, wp_impl_metadata, WP_TYPE_METADATA)

#define pw_metadata_emit(hooks,method,version,...) \
  spa_hook_list_call_simple(hooks, struct pw_metadata_events, \
      method, version, ##__VA_ARGS__)

#define pw_metadata_emit_property(hooks,...) \
  pw_metadata_emit(hooks,property, 0, ##__VA_ARGS__)

static void
emit_properties (WpImplMetadata *self)
{
  struct item *item;
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));

  pw_array_for_each(item, &priv->metadata) {
    wp_debug_object (self, "emit property: %d %s %s %s",
        item->subject, item->key, item->type, item->value);
    pw_metadata_emit_property (&self->hooks,
        item->subject,
        item->key,
        item->type,
        item->value);
  }
}

static int
impl_add_listener (void * object, struct spa_hook * listener,
    const struct pw_metadata_events * events, void * data)
{
  WpImplMetadata *self = WP_IMPL_METADATA (object);
  struct spa_hook_list save;

  spa_hook_list_isolate (&self->hooks, &save, listener, events, data);
  emit_properties (self);
  spa_hook_list_join (&self->hooks, &save);
  return 0;
}

static int
impl_set_property (void * object, uint32_t subject, const char * key,
    const char * type, const char * value)
{
  return metadata_event_property (object, subject, key, type, value);
}

static int
impl_clear (void *object)
{
  WpImplMetadata *self = WP_IMPL_METADATA (object);
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));

  wp_debug_object (self, "clearing all metadata");
  clear_items (&priv->metadata);
  return 0;
}

static const struct pw_metadata_methods impl_metadata = {
  PW_VERSION_METADATA_METHODS,
  .add_listener = impl_add_listener,
  .set_property = impl_set_property,
  .clear = impl_clear,
};

static void
wp_impl_metadata_init (WpImplMetadata * self)
{
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));

  self->iface = SPA_INTERFACE_INIT (
      PW_TYPE_INTERFACE_Metadata,
      PW_VERSION_METADATA,
      &impl_metadata, self);
  spa_hook_list_init (&self->hooks);

  priv->iface = (struct pw_metadata *) &self->iface;
  wp_object_update_features (WP_OBJECT (self), WP_METADATA_FEATURE_DATA, 0);
}

static void
wp_impl_metadata_dispose (GObject * object)
{
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (object));

  clear_items (&priv->metadata);
  wp_object_update_features (WP_OBJECT (object), 0, WP_METADATA_FEATURE_DATA);

  G_OBJECT_CLASS (wp_impl_metadata_parent_class)->dispose (object);
}

static void
wp_impl_metadata_on_changed (WpImplMetadata * self, guint32 subject,
    const gchar * key, const gchar * type, const gchar * value, gpointer data)
{
  wp_debug_object (self, "emit property: %d %s %s %s",
        subject, key, type, value);
  pw_metadata_emit_property (&self->hooks, subject, key, type, value);
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

    /* no pw_core -> we are not connected */
    if (!pw_core) {
      wp_transition_return_error (WP_TRANSITION (transition), g_error_new (
              WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
              "The WirePlumber core is not connected; "
              "object cannot be exported to PipeWire"));
      return;
    }

    wp_proxy_set_pw_proxy (WP_PROXY (self), pw_core_export (pw_core,
            PW_TYPE_INTERFACE_Metadata,
            NULL, priv->iface, 0));
    g_signal_connect (self, "changed",
        (GCallback) wp_impl_metadata_on_changed, NULL);
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
wp_impl_metadata_pw_proxy_destroyed (WpProxy * proxy)
{
  g_signal_handlers_disconnect_by_func (proxy,
      (GCallback) wp_impl_metadata_on_changed, NULL);
}

static void
wp_impl_metadata_class_init (WpImplMetadataClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->dispose = wp_impl_metadata_dispose;

  wpobject_class->activate_execute_step =
      wp_impl_metadata_activate_execute_step;

  /* disable adding a listener for events */
  proxy_class->pw_proxy_created = NULL;
  proxy_class->pw_proxy_destroyed = wp_impl_metadata_pw_proxy_destroyed;
}

WpImplMetadata *
wp_impl_metadata_new (WpCore * core)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_IMPL_METADATA,
      "core", core,
      NULL);
}
