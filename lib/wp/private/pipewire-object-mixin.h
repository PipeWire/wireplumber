/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PIPEWIRE_OBJECT_MIXIN_H__
#define __WIREPLUMBER_PIPEWIRE_OBJECT_MIXIN_H__

#include "proxy-interfaces.h"

#include <pipewire/pipewire.h>

G_BEGIN_DECLS

enum {
  /* this is the same STEP_BIND as in WpGlobalProxy */
  WP_PW_OBJECT_MIXIN_STEP_BIND = WP_TRANSITION_STEP_CUSTOM_START,
  WP_PW_OBJECT_MIXIN_STEP_WAIT_INFO,
  WP_PW_OBJECT_MIXIN_STEP_CACHE_PARAMS,

  WP_PW_OBJECT_MIXIN_STEP_CUSTOM_START,
};

enum {
  WP_PW_OBJECT_MIXIN_PROP_0,

  // WpPipewireObject
  WP_PW_OBJECT_MIXIN_PROP_NATIVE_INFO,
  WP_PW_OBJECT_MIXIN_PROP_PROPERTIES,
  WP_PW_OBJECT_MIXIN_PROP_PARAM_INFO,

  WP_PW_OBJECT_MIXIN_PROP_CUSTOM_START,
};

#define WP_TYPE_PW_OBJECT_MIXIN_PRIV (wp_pw_object_mixin_priv_get_type ())
G_DECLARE_INTERFACE (WpPwObjectMixinPriv, wp_pw_object_mixin_priv,
                     WP, PW_OBJECT_MIXIN_PRIV, WpProxy)

struct _WpPwObjectMixinPrivInterface
{
  GTypeInterface parent;

  /* WpPwObjectMixinPriv-specific flags */
#define WP_PW_OBJECT_MIXIN_PRIV_NO_PARAM_CACHE (1 << 0)
  guint32 flags;

  /* pipewire info struct abstraction layer */
  gsize info_size;
  gsize change_mask_offset;
  gsize props_offset;
  gsize param_info_offset;
  gsize n_params_offset;

  guint64 CHANGE_MASK_ALL;
  guint64 CHANGE_MASK_PROPS;
  guint64 CHANGE_MASK_PARAMS;

  gpointer (*update_info) (gpointer info, gconstpointer update);
  void (*free_info) (gpointer info);

  /* to further process info struct updates - for proxy objects only */
  void (*process_info) (gpointer instance, gpointer old_info, gpointer info);

  /* pipewire interface methods - proxy & impl */
  gint (*enum_params) (gpointer instance, guint32 id,
      guint32 start, guint32 num, WpSpaPod *filter);
  GPtrArray * (*enum_params_sync) (gpointer instance, guint32 id,
      guint32 start, guint32 num, WpSpaPod *filter);
  gint (*set_param) (gpointer instance, guint32 id, guint32 flags,
      WpSpaPod *param /* transfer full */);

  /* pipewire interface events - for impl objects only */
  void (*emit_info) (struct spa_hook_list * hooks, gconstpointer info);
  void (*emit_param) (struct spa_hook_list * hooks, int seq,
      guint32 id, guint32 index, guint32 next, const struct spa_pod *param);
};

/* fills in info struct abstraction layer in WpPwObjectMixinPrivInterface */
#define wp_pw_object_mixin_priv_interface_info_init(iface, type, TYPE) \
({ \
  iface->info_size = sizeof (struct pw_ ## type ## _info); \
  iface->change_mask_offset = G_STRUCT_OFFSET (struct pw_ ## type ## _info, change_mask); \
  iface->props_offset = G_STRUCT_OFFSET (struct pw_ ## type ## _info, props); \
  iface->param_info_offset = G_STRUCT_OFFSET (struct pw_ ## type ## _info, params); \
  iface->n_params_offset = G_STRUCT_OFFSET (struct pw_ ## type ## _info, n_params); \
  iface->CHANGE_MASK_ALL = PW_ ## TYPE ## _CHANGE_MASK_ALL; \
  iface->CHANGE_MASK_PROPS = PW_ ## TYPE ## _CHANGE_MASK_PROPS; \
  iface->CHANGE_MASK_PARAMS = PW_ ## TYPE ## _CHANGE_MASK_PARAMS; \
  iface->update_info = (gpointer (*)(gpointer, gconstpointer)) pw_ ## type ## _info_update; \
  iface->free_info = (void (*)(gpointer)) pw_ ## type ## _info_free; \
})

/* same as above, for types that don't have params */
#define wp_pw_object_mixin_priv_interface_info_init_no_params(iface, type, TYPE) \
({ \
  iface->flags = WP_PW_OBJECT_MIXIN_PRIV_NO_PARAM_CACHE; \
  iface->info_size = sizeof (struct pw_ ## type ## _info); \
  iface->change_mask_offset = G_STRUCT_OFFSET (struct pw_ ## type ## _info, change_mask); \
  iface->props_offset = G_STRUCT_OFFSET (struct pw_ ## type ## _info, props); \
  iface->param_info_offset = 0; \
  iface->n_params_offset = 0; \
  iface->CHANGE_MASK_ALL = PW_ ## TYPE ## _CHANGE_MASK_ALL; \
  iface->CHANGE_MASK_PROPS = PW_ ## TYPE ## _CHANGE_MASK_PROPS; \
  iface->CHANGE_MASK_PARAMS = 0; \
  iface->update_info = (gpointer (*)(gpointer, gconstpointer)) pw_ ## type ## _info_update; \
  iface->free_info = (void (*)(gpointer)) pw_ ## type ## _info_free; \
})

/*************/
/* INTERFACE */

/* implements WpPipewireObject for an object that implements WpPwObjectMixinPriv */
void wp_pw_object_mixin_object_interface_init (WpPipewireObjectInterface * iface);

/********/
/* DATA */

typedef struct _WpPwObjectMixinData WpPwObjectMixinData;
struct _WpPwObjectMixinData
{
  gpointer info;            /* pointer to the info struct */
  gpointer iface;           /* pointer to the interface (ex. pw_endpoint) */
  struct spa_hook listener;
  struct spa_hook_list hooks;
  WpProperties *properties;
  GList *enum_params_tasks;  /* element-type: GTask* */
  GList *params;             /* element-type: WpPwObjectMixinParamStore* */
  GArray *subscribed_ids;    /* element-type: guint32 */
};

/* get mixin data (stored as qdata on the @em instance) */
WpPwObjectMixinData * wp_pw_object_mixin_get_data (gpointer instance);

/****************/
/* PARAMS STORE */

/* param store access; (transfer container) */
GPtrArray * wp_pw_object_mixin_get_stored_params (WpPwObjectMixinData * data,
    guint32 id);

/* param store manipulation
 * @em flags: see below
 * @em param: (transfer full): WpSpaPod* or GPtrArray* */
void wp_pw_object_mixin_store_param (WpPwObjectMixinData * data, guint32 id,
    guint32 flags, gpointer param);

/* set the index at which to store the new param */
#define WP_PW_OBJECT_MIXIN_STORE_PARAM_SET(x)  ((x) & 0x7fff)
#define WP_PW_OBJECT_MIXIN_STORE_PARAM_APPEND  (0xffff)
#define WP_PW_OBJECT_MIXIN_STORE_PARAM_PREPEND (0)
/* @em param is a GPtrArray* */
#define WP_PW_OBJECT_MIXIN_STORE_PARAM_ARRAY   (1 << 16)
/* clear the existing array of params before storing */
#define WP_PW_OBJECT_MIXIN_STORE_PARAM_CLEAR   (1 << 17)
/* completely remove stored params for @id */
#define WP_PW_OBJECT_MIXIN_STORE_PARAM_REMOVE  (1 << 18)

/******************/
/* PROPERTIES API */

/* assign to get_property or chain it from there */
void wp_pw_object_mixin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec);

/* call from class_init */
void wp_pw_object_mixin_class_override_properties (GObjectClass * klass);

/****************/
/* FEATURES API */

/* call from get_supported_features */
WpObjectFeatures wp_pw_object_mixin_get_supported_features (WpObject * object);

/* assign directly to activate_get_next_step */
guint wp_pw_object_mixin_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing);

/* call from activate_execute_step when
   step == WP_PW_OBJECT_MIXIN_STEP_CACHE_PARAMS */
void wp_pw_object_mixin_cache_params (WpObject * object,
    WpObjectFeatures missing);

/* handle deactivation of PARAM_* caching features */
void wp_pw_object_mixin_deactivate (WpObject * object,
    WpObjectFeatures features);

/************************/
/* PROXY EVENT HANDLERS */
/*  (for proxy objects) */

#define wp_pw_object_mixin_handle_pw_proxy_created(instance, pw_proxy, type, events) \
({ \
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance); \
  d->iface = pw_proxy; \
  pw_ ## type ## _add_listener ((struct pw_ ## type *) pw_proxy, \
      &d->listener, events, instance); \
})

void wp_pw_object_mixin_handle_pw_proxy_destroyed (WpProxy * proxy);

/***************************/
/* PIPEWIRE EVENT HANDLERS */
/*   (for proxy objects)   */

#define HandleEventInfoFunc(type) \
  void (*)(void *, const struct pw_ ## type ## _info *)

void wp_pw_object_mixin_handle_event_info (gpointer instance, gconstpointer info);

/* assign as the param event callback */
void wp_pw_object_mixin_handle_event_param (gpointer instance, int seq,
    guint32 id, guint32 index, guint32 next, const struct spa_pod *param);

/***********************************/
/* PIPEWIRE METHOD IMPLEMENTATIONS */
/*       (for impl objects)        */

#define ImplAddListenerFunc(type) \
  int (*)(void *, struct spa_hook *, const struct pw_ ## type ## _events *, void *)

int wp_pw_object_mixin_impl_add_listener (gpointer instance,
    struct spa_hook *listener, gconstpointer events, gpointer data);

int wp_pw_object_mixin_impl_enum_params (gpointer instance, int seq,
    guint32 id, guint32 start, guint32 num, const struct spa_pod *filter);

int wp_pw_object_mixin_impl_subscribe_params (gpointer instance,
    guint32 *ids, guint32 n_ids);

int wp_pw_object_mixin_impl_set_param (gpointer instance, guint32 id,
    guint32 flags, const struct spa_pod *param);

/**********************/
/*      NOTIFIERS     */
/* (for impl objects) */

void wp_pw_object_mixin_notify_info (gpointer instance, guint32 change_mask);

void wp_pw_object_mixin_notify_params_changed (gpointer instance, guint32 id);

G_END_DECLS

#endif
