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
#include "spa-type.h"
#include "spa-pod.h"
#include "debug.h"

#include <pipewire/pipewire.h>
#include <spa/utils/result.h>

G_BEGIN_DECLS

enum {
  /* this is the same STEP_BIND as in WpGlobalProxy */
  WP_PIPEWIRE_OBJECT_MIXIN_STEP_BIND = WP_TRANSITION_STEP_CUSTOM_START,
  WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO,

  WP_PIPEWIRE_OBJECT_MIXIN_STEP_CUSTOM_START,
};

enum {
  WP_PIPEWIRE_OBJECT_MIXIN_PROP_0,

  // WpPipewireObject
  WP_PIPEWIRE_OBJECT_MIXIN_PROP_NATIVE_INFO,
  WP_PIPEWIRE_OBJECT_MIXIN_PROP_PROPERTIES,
  WP_PIPEWIRE_OBJECT_MIXIN_PROP_PARAM_INFO,

  WP_PIPEWIRE_OBJECT_MIXIN_PROP_CUSTOM_START,
};

/******************/
/* PROPERTIES API */

/* assign to get_property or chain it from there */
void wp_pipewire_object_mixin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec);

/* call from class_init */
void wp_pipewire_object_mixin_class_override_properties (GObjectClass * klass);

/****************/
/* FEATURES API */

/* call from get_supported_features */
WpObjectFeatures wp_pipewire_object_mixin_param_info_to_features (
    struct spa_param_info * info, guint n_params);

/* assign directly to activate_get_next_step */
guint wp_pipewire_object_mixin_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing);

/* call from activate_execute_step when
   step == WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO */
void wp_pipewire_object_mixin_cache_info (WpObject * object,
    WpFeatureActivationTransition * transition);

/* handle deactivation of PARAM_* caching features */
void wp_pipewire_object_mixin_deactivate (WpObject * object,
    WpObjectFeatures features);

/***************************/
/* PIPEWIRE EVENT HANDLERS */

/* call at the end of the info event callback */
#define wp_pipewire_object_mixin_handle_event_info(instance, info, CM_PROPS, CM_PARAMS) \
({ \
  if (info->change_mask & CM_PROPS) \
    g_object_notify (G_OBJECT (instance), "properties"); \
  \
  if (info->change_mask & CM_PARAMS) \
    g_object_notify (G_OBJECT (instance), "param-info"); \
})

/* assign as the param event callback */
void wp_pipewire_object_mixin_handle_event_param (gpointer instance, int seq,
    uint32_t id, uint32_t index, uint32_t next, const struct spa_pod *param);

/***********************/
/* PIPEWIRE OBJECT API */

/* call from get_param_info */
GVariant * wp_pipewire_object_mixin_param_info_to_gvariant (
    struct spa_param_info * info, guint n_params);

/* assign directly to enum_params_finish */
WpIterator * wp_pipewire_object_mixin_enum_params_finish (WpPipewireObject * obj,
    GAsyncResult * res, GError ** error);

/* assign directly to enum_cached_params */
WpIterator * wp_pipewire_object_mixin_enum_cached_params (WpPipewireObject * obj,
    const gchar * id);

/* assign to enum_params on objects that don't support params (like pw_link) */
void wp_pipewire_object_mixin_enum_params_unimplemented (WpPipewireObject * obj,
    const gchar * id, WpSpaPod *filter, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

/* assign to set_param on objects that don't support params (like pw_link) */
void wp_pipewire_object_mixin_set_param_unimplemented (WpPipewireObject * obj,
    const gchar * id, WpSpaPod * param);

/* call from enum_params */
#define wp_pipewire_object_mixin_enum_params(type, instance, id, filter, cancellable, callback, user_data) \
({ \
  struct type *pwp; \
  gint seq; \
  guint32 id_num = 0; \
  \
  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_PARAM, id, &id_num, \
          NULL, NULL)) { \
    wp_critical_object (instance, "invalid param id: %s", id); \
  } else { \
    pwp = (struct type *) wp_proxy_get_pw_proxy (WP_PROXY (instance)); \
    seq = type ## _enum_params (pwp, 0, id_num, 0, -1, \
        filter ? wp_spa_pod_get_spa_pod (filter) : NULL); \
    \
    wp_pipewire_object_mixin_create_enum_params_task (instance, seq, cancellable, \
        callback, user_data); \
  } \
})

/* call from set_param */
#define wp_pipewire_object_mixin_set_param(type, instance, id, param) \
({ \
  struct type *pwp; \
  gint ret; \
  guint32 id_num = 0; \
  \
  if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_PARAM, id, &id_num, \
          NULL, NULL)) { \
    wp_critical_object (instance, "invalid param id: %s", id); \
  } else { \
    pwp = (struct type *) wp_proxy_get_pw_proxy (WP_PROXY (instance)); \
    ret = type ## _set_param (pwp, id_num, 0, wp_spa_pod_get_spa_pod (param)); \
    if (G_UNLIKELY (SPA_RESULT_IS_ERROR (ret))) { \
      wp_message_object (instance, "set_param failed: %s", spa_strerror (ret)); \
    } \
  } \
})

/************/
/* INTERNAL */

void wp_pipewire_object_mixin_create_enum_params_task (gpointer instance,
    gint seq, GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data);

G_END_DECLS

#endif
