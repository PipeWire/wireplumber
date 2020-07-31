/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author Raghavendra Rao <raghavendra.rao@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpMetadata
 *
 * The #WpMetadata class allows accessing the properties and methods of
 * Pipewire Jack metadata object (`struct pw_metadata`).
 *
 */

#define G_LOG_DOMAIN "wp-metadata"

#include "metadata.h"
#include "spa-type.h"
#include "spa-pod.h"
#include "debug.h"
#include "private.h"
#include "error.h"
#include "wpenums.h"

#include <pipewire/pipewire.h>
#include <pipewire/array.h>
#include <pipewire/extensions/metadata.h>

#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/pod/filter.h>

/* WpMetadata */

typedef struct _WpMetadataPrivate WpMetadataPrivate;
struct _WpMetadataPrivate
{
  struct pw_metadata *iface;
  struct spa_hook listener;

  struct spa_hook_list hooks;
  struct pw_properties *properties;
  struct pw_array metadata;
  struct pw_proxy *proxy;
};

G_DEFINE_TYPE_WITH_PRIVATE (WpMetadata, wp_metadata, WP_TYPE_PROXY)

static void
wp_metadata_init (WpMetadata * self)
{
}

static void
wp_metadata_finalize (GObject * object)
{
  G_OBJECT_CLASS (wp_metadata_parent_class)->finalize (object);
}

static void
wp_metadata_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpMetadata *self = WP_METADATA (proxy);
  WpMetadataPrivate *priv = wp_metadata_get_instance_private (self);
  priv->iface = (struct pw_metadata *) pw_proxy;
}

static void
wp_metadata_class_init (WpMetadataClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_metadata_finalize;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Metadata;
  proxy_class->pw_iface_version = PW_VERSION_METADATA;

  proxy_class->pw_proxy_created = wp_metadata_pw_proxy_created;
}

/* WpImplMetadata */

typedef struct _WpImplMetadata WpImplMetadata;
struct _WpImplMetadata
{
  WpMetadata parent;

  struct spa_interface iface;
  struct spa_hook_list hooks;
  gboolean subscribed;
};

G_DEFINE_TYPE (WpImplMetadata, wp_impl_metadata, WP_TYPE_METADATA)

#define pw_metadata_emit(hooks,method,version,...) \
  spa_hook_list_call_simple(hooks, struct pw_metadata_events, \
      method, version, ##__VA_ARGS__)

#define pw_metadata_emit_property(hooks,...) \
  pw_metadata_emit(hooks,property, 0, ##__VA_ARGS__)

struct item {
  uint32_t subject;
  char *key;
  char *type;
  char *value;
};

static void
clear_item (struct item *item)
{
  free (item->key);
  free (item->type);
  free (item->value);
  spa_zero (*item);
}

static void
set_item(struct item *item, uint32_t subject, const char *key,
	const char *type, const char *value)
{
  item->subject = subject;
  item->key = strdup(key);
  item->type = strdup(type);
  item->value = strdup(value);
}

static void
emit_properties(WpImplMetadata *self,
	const struct spa_dict *dict)
{
  struct item *item;
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));

  pw_array_for_each(item, &priv->metadata) {
    wp_info_object (self, "metadata : %d %s %s %s",
        item->subject, item->key, item->type, item->value);
    pw_metadata_emit_property (&priv->hooks,
        item->subject,
        item->key,
        item->type,
        item->value);
  }
}

static int
impl_add_listener(void *object,
    struct spa_hook *listener,
    const struct pw_metadata_events *events,
    void *data)
{
  WpImplMetadata *self = WP_IMPL_METADATA (object);
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));
  struct spa_hook_list save;

  spa_hook_list_isolate (&priv->hooks, &save, listener, events, data);
  emit_properties(self, &priv->properties->dict);
  spa_hook_list_join (&priv->hooks, &save);

  return 0;
}

static struct item *
find_item (WpImplMetadata *self, uint32_t subject, const char *key)
{
  struct item *item;
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));

  pw_array_for_each(item, &priv->metadata) {
    if (item->subject == subject && (key == NULL || !strcmp (item->key, key))) {
      return item;
    }
  }
  return NULL;
}

static int
clear_subjects (WpImplMetadata *self, uint32_t subject)
{
  struct item *item;
  uint32_t removed = 0;
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));

  while (true) {
    item = find_item(self, subject, NULL);
    if (item == NULL)
      break;

    wp_debug_object (self, "remove id:%d key:%s", subject, item->key);

    clear_item (item);
    pw_array_remove (&priv->metadata, item);
    removed++;
  }

  if (removed > 0)
    pw_metadata_emit_property (&priv->hooks, subject, NULL, NULL, NULL);

  return 0;
}

static void
clear_items (WpImplMetadata *self)
{
  struct item *item;
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));

  pw_array_consume (item, &priv->metadata) {
    clear_subjects (self, item->subject);
  }
  pw_array_reset (&priv->metadata);
}

static int
impl_set_property (void *object, uint32_t subject, const char *key,
		const char *type, const char *value)
{
  WpImplMetadata *self = WP_IMPL_METADATA (object);
  WpMetadataPrivate *priv;
  struct item *item = NULL;

  g_return_val_if_fail (WP_IS_IMPL_METADATA (self), -1);
  priv = wp_metadata_get_instance_private (WP_METADATA (self));

  if (key == NULL)
    return clear_subjects (self, subject);

  item = find_item (self, subject, key);
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

  pw_metadata_emit_property (&priv->hooks, subject, key, type, value);

  return 0;
}

static int
impl_clear (void *object)
{
  WpImplMetadata *self = WP_IMPL_METADATA (object);
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));
  clear_items (self);
  pw_array_clear (&priv->metadata);
  pw_properties_free (priv->properties);
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
  spa_hook_list_init (&priv->hooks);

  priv->iface = (struct pw_metadata *) &self->iface;

  priv->properties = pw_properties_new (NULL, NULL);
  pw_array_init (&priv->metadata, 4096);
}

static void
wp_impl_metadata_finalize (GObject * object)
{
  G_OBJECT_CLASS (wp_impl_metadata_parent_class)->finalize (object);
}

static void
wp_impl_metadata_augment (WpProxy * proxy, WpProxyFeatures features)
{
  WpImplMetadata *self = WP_IMPL_METADATA (proxy);
  WpMetadataPrivate *priv =
      wp_metadata_get_instance_private (WP_METADATA (self));

  /* PW_PROXY depends on BOUND */
  if (features & WP_PROXY_FEATURE_PW_PROXY)
    features |= WP_PROXY_FEATURE_BOUND;

  if (features & WP_PROXY_FEATURE_BOUND) {
    g_autoptr (WpCore) core = wp_proxy_get_core (proxy);
    struct pw_core *pw_core = wp_core_get_pw_core (core);

    /* no pw_core -> we are not connected */
    if (!pw_core) {
      wp_proxy_augment_error (proxy, g_error_new (WP_DOMAIN_LIBRARY,
              WP_LIBRARY_ERROR_OPERATION_FAILED,
              "The WirePlumber core is not connected; "
              "object cannot be exported to PipeWire"));
      return;
    }

    wp_proxy_set_pw_proxy (proxy, pw_core_export (pw_core,
            PW_TYPE_INTERFACE_Metadata,
            &priv->properties->dict,
            priv->iface, 0));
  }
}

static void
wp_impl_metadata_class_init (WpImplMetadataClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_impl_metadata_finalize;

  proxy_class->augment = wp_impl_metadata_augment;
  proxy_class->enum_params = NULL;
  proxy_class->subscribe_params = NULL;
  proxy_class->pw_proxy_created = NULL;
}

WpImplMetadata *
wp_impl_metadata_new (WpCore * core)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_IMPL_METADATA,
      "core", core,
      NULL);
}
