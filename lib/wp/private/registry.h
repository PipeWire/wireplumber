/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_REGISTRY_H__
#define __WIREPLUMBER_REGISTRY_H__

#include "core.h"

#include <pipewire/pipewire.h>

G_BEGIN_DECLS

typedef struct _WpRegistry WpRegistry;
typedef struct _WpGlobal WpGlobal;

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

WpCore * wp_registry_get_core (WpRegistry * self) G_GNUC_CONST;

/* core */

WpRegistry * wp_core_get_registry (WpCore * self) G_GNUC_CONST;

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

G_END_DECLS

#endif
