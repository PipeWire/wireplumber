/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_H__
#define __WIREPLUMBER_PRIVATE_H__

#include "core.h"
#include "object-manager.h"
#include "proxy.h"
#include "iterator.h"
#include "spa-type.h"

#include <stdint.h>
#include <pipewire/pipewire.h>

G_BEGIN_DECLS

struct spa_pod;
struct spa_pod_builder;

typedef struct _WpRegistry WpRegistry;
typedef struct _WpGlobal WpGlobal;
typedef struct _WpSpaProps WpSpaProps;

/* registry */

struct _WpRegistry
{
  struct pw_registry *pw_registry;
  struct spa_hook listener;

  GPtrArray *globals; // elementy-type: WpGlobal*
  GPtrArray *tmp_globals; // elementy-type: WpGlobal*
  GPtrArray *objects; // element-type: GObject*
  GPtrArray *object_managers; // element-type: WpObjectManager*
};

void wp_registry_init (WpRegistry *self);
void wp_registry_clear (WpRegistry *self);
void wp_registry_attach (WpRegistry *self, struct pw_core *pw_core);
void wp_registry_detach (WpRegistry *self);

void wp_registry_prepare_new_global (WpRegistry * self, guint32 id,
    guint32 permissions, guint32 flag, GType type,
    WpProxy *proxy, const struct spa_dict *props,
    WpGlobal ** new_global);

gpointer wp_registry_find_object (WpRegistry *reg, GEqualFunc func,
    gconstpointer data);
void wp_registry_register_object (WpRegistry *reg, gpointer obj);
void wp_registry_remove_object (WpRegistry *reg, gpointer obj);

WpCore * wp_registry_get_core (WpRegistry * self);

/* core */

WpRegistry * wp_core_get_registry (WpCore * self);

/* global */

typedef enum {
  WP_GLOBAL_FLAG_APPEARS_ON_REGISTRY = 0x1,
  WP_GLOBAL_FLAG_OWNED_BY_PROXY = 0x2,
} WpGlobalFlags;

struct _WpGlobal
{
  guint32 flags;
  guint32 id;
  GType type;
  guint32 permissions;
  WpProperties *properties;
  WpProxy *proxy;
  WpRegistry *registry;
};

#define WP_TYPE_GLOBAL (wp_global_get_type ())
GType wp_global_get_type (void);

static inline void
wp_global_clear (WpGlobal * self)
{
  g_clear_pointer (&self->properties, wp_properties_unref);
}

static inline WpGlobal *
wp_global_ref (WpGlobal * self)
{
  return g_rc_box_acquire (self);
}

static inline void
wp_global_unref (WpGlobal * self)
{
  g_rc_box_release_full (self, (GDestroyNotify) wp_global_clear);
}

void wp_global_rm_flag (WpGlobal *global, guint rm_flag);
struct pw_proxy * wp_global_bind (WpGlobal * global);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (WpGlobal, wp_global_unref)

/* proxy */

void wp_proxy_destroy (WpProxy *self);
void wp_proxy_set_pw_proxy (WpProxy * self, struct pw_proxy * proxy);

void wp_proxy_set_feature_ready (WpProxy * self, WpProxyFeatures feature);
void wp_proxy_augment_error (WpProxy * self, GError * error);

void wp_proxy_handle_event_param (void * proxy, int seq, uint32_t id,
    uint32_t index, uint32_t next, const struct spa_pod *param);

/* iterator */

struct _WpIteratorMethods {
  void (*reset) (WpIterator *self);
  gboolean (*next) (WpIterator *self, GValue *item);
  gboolean (*fold) (WpIterator *self, WpIteratorFoldFunc func,
      GValue *ret, gpointer data);
  gboolean (*foreach) (WpIterator *self, WpIteratorForeachFunc func,
      gpointer data);
  void (*finalize) (WpIterator *self);
};
typedef struct _WpIteratorMethods WpIteratorMethods;

WpIterator * wp_iterator_new (const WpIteratorMethods *methods,
    size_t user_size);
gpointer wp_iterator_get_user_data (WpIterator *self);

/* spa pod */

typedef struct _WpSpaPod WpSpaPod;
WpSpaPod * wp_spa_pod_new_regular_wrap (struct spa_pod *pod);
WpSpaPod * wp_spa_pod_new_property_wrap (WpSpaTypeTable table, guint32 key,
    guint32 flags, struct spa_pod *pod);
WpSpaPod * wp_spa_pod_new_control_wrap (guint32 offset, guint32 type,
    struct spa_pod *pod);
WpSpaPod * wp_spa_pod_new_regular_wrap_copy (const struct spa_pod *pod);
WpSpaPod * wp_spa_pod_new_property_wrap_copy (WpSpaTypeTable table, guint32 key,
    guint32 flags, const struct spa_pod *pod);
WpSpaPod * wp_spa_pod_new_control_wrap_copy (guint32 offset, guint32 type,
    const struct spa_pod *pod);
struct spa_pod *wp_spa_pod_get_spa_pod (WpSpaPod *self);

/* spa props */

struct _WpSpaProps
{
  GList *entries;
};

void wp_spa_props_clear (WpSpaProps * self);

void wp_spa_props_register_pod (WpSpaProps * self,
    guint32 id, const gchar *name, const struct spa_pod *type);
gint wp_spa_props_register_from_prop_info (WpSpaProps * self,
    const struct spa_pod * prop_info);

const struct spa_pod * wp_spa_props_get_stored (WpSpaProps * self, guint32 id);

gint wp_spa_props_store_pod (WpSpaProps * self, guint32 id,
    const struct spa_pod * value);
gint wp_spa_props_store_from_props (WpSpaProps * self,
    const struct spa_pod * props, GArray * changed_ids);

GPtrArray * wp_spa_props_build_all_pods (WpSpaProps * self,
    struct spa_pod_builder * b);
struct spa_pod * wp_spa_props_build_update (WpSpaProps * self, guint32 id,
    const struct spa_pod * value, struct spa_pod_builder * b);

const struct spa_pod * wp_spa_props_build_pod_valist (gchar * buffer,
    gsize size, va_list args);

static inline const struct spa_pod *
wp_spa_props_build_pod (gchar * buffer, gsize size, ...)
{
  const struct spa_pod *ret;
  va_list args;
  va_start (args, size);
  ret = wp_spa_props_build_pod_valist (buffer, size, args);
  va_end (args);
  return ret;
}

#define wp_spa_props_register(self, id, name, ...) \
({ \
  gchar b[512]; \
  wp_spa_props_register_pod (self, id, name, \
      wp_spa_props_build_pod (b, sizeof (b), ##__VA_ARGS__, NULL)); \
})

#define wp_spa_props_store(self, id, ...) \
({ \
  gchar b[512]; \
  wp_spa_props_store_pod (self, id, \
      wp_spa_props_build_pod (b, sizeof (b), ##__VA_ARGS__, NULL)); \
})

G_END_DECLS

#endif
