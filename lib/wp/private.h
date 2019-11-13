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

G_END_DECLS

#endif
