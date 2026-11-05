/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_REGISTRY_H__
#define __WIREPLUMBER_PRIVATE_REGISTRY_H__

#include "core.h"
#include "global-proxy.h"

#include <pipewire/pipewire.h>

G_BEGIN_DECLS

typedef struct _WpGlobal WpGlobal;

/* registry */

WpRegistry * wp_registry_new (WpCore * core);

void wp_registry_clear (WpRegistry *self);

void wp_registry_attach (WpRegistry *self, struct pw_core *pw_core);

void wp_registry_detach (WpRegistry *self);

void wp_registry_prepare_new_global (WpRegistry * self, guint32 id,
    guint32 permissions, guint32 flag, GType type,
    WpGlobalProxy *proxy, const struct spa_dict *props,
    WpGlobal ** new_global);

void wp_registry_notify_add_object (WpRegistry * self, gpointer object);
void wp_registry_notify_rm_object (WpRegistry * self, gpointer object);

void wp_registry_add_object_manager (WpRegistry * self,
    WpObjectManager * om);

void wp_registry_mark_feature_provided (WpRegistry * self,
    const gchar * feature);

gboolean wp_registry_has_pending_globals (WpRegistry * self);
void wp_registry_destroy_global (WpRegistry * self, guint32 id);


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
  WpGlobalProxy *proxy;
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

G_END_DECLS

#endif
