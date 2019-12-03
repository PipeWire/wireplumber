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
#include "exported.h"
#include "object-manager.h"
#include "proxy.h"

G_BEGIN_DECLS

/* core */

struct pw_core_proxy;
struct pw_registry_proxy;

struct pw_core_proxy * wp_core_get_pw_core_proxy (WpCore * self);
struct pw_registry_proxy * wp_core_get_pw_registry_proxy (WpCore * self);

gpointer wp_core_find_object (WpCore * self, GEqualFunc func,
    gconstpointer data);
void wp_core_register_object (WpCore * self, gpointer obj);
void wp_core_remove_object (WpCore * self, gpointer obj);

/* exported */

void wp_exported_notify_export_done (WpExported * self, GError * error);

/* global */

typedef struct _WpGlobal WpGlobal;
struct _WpGlobal
{
  guint32 id;
  guint32 type;
  guint32 version;
  guint32 permissions;
  WpProperties *properties;
  GWeakRef proxy;
};

static inline WpGlobal *
wp_global_new (void)
{
  WpGlobal *self = g_rc_box_new0 (WpGlobal);
  g_weak_ref_init (&self->proxy, NULL);
  return self;
}

static inline void
wp_global_clear (WpGlobal * self)
{
  g_clear_pointer (&self->properties, wp_properties_unref);
  g_weak_ref_clear (&self->proxy);
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

G_DEFINE_AUTOPTR_CLEANUP_FUNC (WpGlobal, wp_global_unref)

/* object manager */

void wp_object_manager_add_global (WpObjectManager * self, WpGlobal * global);
void wp_object_manager_rm_global (WpObjectManager * self, guint32 id);

void wp_object_manager_add_object (WpObjectManager * self, GObject * object);
void wp_object_manager_rm_object (WpObjectManager * self, GObject * object);

/* proxy */

WpProxy * wp_proxy_new_global (WpCore * core, WpGlobal * global);

void wp_proxy_set_feature_ready (WpProxy * self, WpProxyFeatures feature);
void wp_proxy_augment_error (WpProxy * self, GError * error);

void wp_proxy_register_async_task (WpProxy * self, int seq, GTask * task);
GTask * wp_proxy_find_async_task (WpProxy * self, int seq, gboolean steal);

/* spa props */

struct spa_pod;
struct spa_pod_builder;

typedef struct _WpSpaProps WpSpaProps;
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
