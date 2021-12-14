/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-pw-obj-mixin"

#include "private/pipewire-object-mixin.h"
#include "core.h"
#include "spa-type.h"
#include "spa-pod.h"
#include "log.h"
#include "error.h"

#include <spa/utils/result.h>

G_DEFINE_INTERFACE (WpPwObjectMixinPriv, wp_pw_object_mixin_priv, WP_TYPE_PROXY)

static void
wp_pw_object_mixin_priv_default_init (WpPwObjectMixinPrivInterface * iface)
{
}

static struct spa_param_info *
find_param_info (gpointer instance, guint32 id)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  WpPwObjectMixinPrivInterface *iface =
      WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (instance);

  /* offsets are 0 on objects that don't support params */
  if (d->info && iface->n_params_offset && iface->param_info_offset) {
    struct spa_param_info * param_info =
        G_STRUCT_MEMBER (struct spa_param_info *, d->info, iface->param_info_offset);
    guint32 n_params =
        G_STRUCT_MEMBER (guint32, d->info, iface->n_params_offset);

    for (guint i = 0; i < n_params; i++) {
      if (param_info[i].id == id)
        return &param_info[i];
    }
  }
  return NULL;
}

/*************/
/* INTERFACE */

static gconstpointer
wp_pw_object_mixin_get_native_info (WpPipewireObject * obj)
{
  return wp_pw_object_mixin_get_data (obj)->info;
}

static WpProperties *
wp_pw_object_mixin_get_properties (WpPipewireObject * obj)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (obj);
  return d->properties ? wp_properties_ref (d->properties) : NULL;
}

static GVariant *
wp_pw_object_mixin_get_param_info (WpPipewireObject * obj)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (obj);
  WpPwObjectMixinPrivInterface *iface = WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (obj);
  struct spa_param_info *info;
  guint32 n_params;
  g_auto (GVariantBuilder) b =
      G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_DICTIONARY);

  if (!d->info ||
      iface->param_info_offset == 0 ||
      iface->n_params_offset == 0)
    return NULL;

  info = G_STRUCT_MEMBER (struct spa_param_info *, d->info, iface->param_info_offset);
  n_params = G_STRUCT_MEMBER (guint32, d->info, iface->n_params_offset);

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a{ss}"));

  for (guint i = 0; i < n_params; i++) {
    WpSpaIdValue idval;
    const gchar *nick = NULL;
    gchar flags[3];
    guint flags_idx = 0;

    idval = wp_spa_id_value_from_number ("Spa:Enum:ParamId", info[i].id);
    nick = wp_spa_id_value_short_name (idval);
    g_return_val_if_fail (nick != NULL, NULL);

    if (info[i].flags & SPA_PARAM_INFO_READ)
      flags[flags_idx++] = 'r';
    if (info[i].flags & SPA_PARAM_INFO_WRITE)
      flags[flags_idx++] = 'w';
    flags[flags_idx] = '\0';

    g_variant_builder_add (&b, "{ss}", nick, flags);
  }

  return g_variant_builder_end (&b);
}

static void
enum_params_done (WpCore * core, GAsyncResult * res, gpointer data)
{
  g_autoptr (GTask) task = G_TASK (data);
  GList *taskl = NULL;
  g_autoptr (GError) error = NULL;
  gpointer instance = g_task_get_source_object (G_TASK (data));
  GPtrArray *params = g_task_get_task_data (task);
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);

  /* finish the sync task */
  wp_core_sync_finish (core, res, &error);

  /* return if task was previously removed from the list */
  taskl = g_list_find (d->enum_params_tasks, task);
  if (!taskl)
    return;

  /* remove the task from the stored list; ref is held by the g_autoptr */
  d->enum_params_tasks = g_list_delete_link (d->enum_params_tasks, taskl);

  wp_debug_object (instance, "got %u params, %s, task " WP_OBJECT_FORMAT,
      params->len, error ? "with error" : "ok", WP_OBJECT_ARGS (task));

  if (error)
    g_task_return_error (task, g_steal_pointer (&error));
  else {
    g_task_return_pointer (task, g_ptr_array_ref (params),
        (GDestroyNotify) g_ptr_array_unref);
  }
}

static void
enum_params_error (WpProxy * proxy, int seq, int res, const gchar *msg,
    GTask * task)
{
  gint t_seq = GPOINTER_TO_INT (g_task_get_source_tag (task));

  if (SPA_RESULT_ASYNC_SEQ (t_seq) == SPA_RESULT_ASYNC_SEQ (seq)) {
    gpointer instance = g_task_get_source_object (task);
    WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
    GList *taskl = NULL;

    taskl = g_list_find (d->enum_params_tasks, task);
    if (taskl) {
      d->enum_params_tasks = g_list_delete_link (d->enum_params_tasks, taskl);
      g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
          WP_LIBRARY_ERROR_OPERATION_FAILED, "%s", msg);
    }
  }
}

static void
wp_pw_object_mixin_enum_params_unchecked (gpointer obj,
    guint32 id, WpSpaPod *filter, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (obj);
  WpPwObjectMixinPrivInterface *iface = WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (obj);
  g_autoptr (GTask) task = NULL;
  gint seq = 0;
  GPtrArray *params = NULL;

  g_return_if_fail (iface->enum_params_sync || iface->enum_params);

  if (iface->enum_params_sync) {
    params = iface->enum_params_sync (obj, id, 0, -1, filter);
  } else {
    seq = iface->enum_params (obj, id, 0, -1, filter);

    /* return early if seq contains an error */
    if (G_UNLIKELY (SPA_RESULT_IS_ERROR (seq))) {
      wp_message_object (obj, "enum_params failed: %s", spa_strerror (seq));
      g_task_report_new_error (obj, callback, user_data, NULL,
          WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
          "enum_params failed: %s", spa_strerror (seq));
      return;
    }
  }

  if (!params)
    params = g_ptr_array_new_with_free_func ((GDestroyNotify) wp_spa_pod_unref);

  /* create task */
  task = g_task_new (obj, cancellable, callback, user_data);

  /* debug */
  if (wp_log_level_is_enabled (G_LOG_LEVEL_DEBUG)) {
    const gchar *name = NULL;
    name = wp_spa_id_value_short_name (
        wp_spa_id_value_from_number ("Spa:Enum:ParamId", id));
    wp_debug_object (obj, "enum id %u (%s), seq 0x%x (%u), task "
        WP_OBJECT_FORMAT "%s", id, name, seq, seq, WP_OBJECT_ARGS (task),
        iface->enum_params_sync ? ", sync" : "");
  }

  if (iface->enum_params_sync) {
    g_task_return_pointer (task, params, (GDestroyNotify) g_ptr_array_unref);
  } else {
    g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (obj));

    /* watch for errors */
    g_signal_connect_object (obj, "error", G_CALLBACK (enum_params_error),
        task, 0);

    /* store */
    g_task_set_task_data (task, params, (GDestroyNotify) g_ptr_array_unref);
    g_task_set_source_tag (task, GINT_TO_POINTER (seq));
    d->enum_params_tasks = g_list_append (d->enum_params_tasks, task);

    /* call sync */
    wp_core_sync (core, cancellable, (GAsyncReadyCallback) enum_params_done,
        g_object_ref (task));
  }
}

static void
wp_pw_object_mixin_enum_params (WpPipewireObject * obj, const gchar * id,
    WpSpaPod *filter, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  WpPwObjectMixinPrivInterface *iface = WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (obj);
  WpSpaIdValue param_id;

  if (!(iface->enum_params || iface->enum_params_sync)) {
    g_task_report_new_error (obj, callback, user_data, NULL,
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "enum_params is not supported on this object");
    return;
  }

  /* translate the id */
  param_id = wp_spa_id_value_from_short_name ("Spa:Enum:ParamId", id);
  if (!param_id) {
    wp_critical_object (obj, "invalid param id: %s", id);
    return;
  }

  wp_pw_object_mixin_enum_params_unchecked (obj,
      wp_spa_id_value_number (param_id), filter,
      cancellable, callback, user_data);
}

static WpIterator *
wp_pw_object_mixin_enum_params_finish (WpPipewireObject * obj,
    GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (g_task_is_valid (res, obj), NULL);

  GPtrArray *array = g_task_propagate_pointer (G_TASK (res), error);
  if (!array)
    return NULL;

  return wp_iterator_new_ptr_array (array, WP_TYPE_SPA_POD);
}

static WpIterator *
wp_pw_object_mixin_enum_params_sync (WpPipewireObject * obj, const gchar * id,
    WpSpaPod * filter)
{
  WpPwObjectMixinPrivInterface *iface = WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (obj);
  GPtrArray *params = NULL;
  WpSpaIdValue param_id;

  /* translate the id */
  param_id = wp_spa_id_value_from_short_name ("Spa:Enum:ParamId", id);
  if (!param_id) {
    wp_critical_object (obj, "invalid param id: %s", id);
    return NULL;
  }

  if (iface->enum_params_sync) {
    /* use enum_params_sync if supported */
    params = iface->enum_params_sync (obj, wp_spa_id_value_number (param_id),
        0, -1, filter);
  } else {
    /* otherwise, find and return the cached params */
    WpPwObjectMixinData *data = wp_pw_object_mixin_get_data (obj);
    params = wp_pw_object_mixin_get_stored_params (data,
        wp_spa_id_value_number (param_id));
    /* TODO filter */
  }

  return params ? wp_iterator_new_ptr_array (params, WP_TYPE_SPA_POD) : NULL;
}

static gboolean
wp_pw_object_mixin_set_param (WpPipewireObject * obj, const gchar * id,
    guint32 flags, WpSpaPod * param)
{
  WpPwObjectMixinPrivInterface *iface = WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (obj);
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (obj);
  WpSpaIdValue param_id;
  gint ret;

  if (!d->iface) {
    wp_message_object (obj, "ignoring set_param on already destroyed objects");
    return FALSE;
  }

  if (!iface->set_param) {
    wp_warning_object (obj, "set_param is not supported on this object");
    return FALSE;
  }

  param_id = wp_spa_id_value_from_short_name ("Spa:Enum:ParamId", id);
  if (!param_id) {
    wp_critical_object (obj, "invalid param id: %s", id);
    wp_spa_pod_unref (param);
    return FALSE;
  }

  ret = iface->set_param (obj, wp_spa_id_value_number (param_id), flags, param);

  if (G_UNLIKELY (SPA_RESULT_IS_ERROR (ret))) {
    wp_message_object (obj, "set_param failed: %s", spa_strerror (ret));
    return FALSE;
  }
  return TRUE;
}

void
wp_pw_object_mixin_object_interface_init (WpPipewireObjectInterface * iface)
{
  iface->get_native_info = wp_pw_object_mixin_get_native_info;
  iface->get_properties = wp_pw_object_mixin_get_properties;
  iface->get_param_info = wp_pw_object_mixin_get_param_info;
  iface->enum_params = wp_pw_object_mixin_enum_params;
  iface->enum_params_finish = wp_pw_object_mixin_enum_params_finish;
  iface->enum_params_sync = wp_pw_object_mixin_enum_params_sync;
  iface->set_param = wp_pw_object_mixin_set_param;
}

/********/
/* DATA */

G_DEFINE_QUARK (WpPwObjectMixinData, wp_pw_object_mixin_data)

static void wp_pw_object_mixin_param_store_free (gpointer data);

static WpPwObjectMixinData *
wp_pw_object_mixin_data_new (void)
{
  WpPwObjectMixinData *d = g_slice_new0 (WpPwObjectMixinData);
  spa_hook_list_init (&d->hooks);
  return d;
}

static void
wp_pw_object_mixin_data_free (gpointer data)
{
  WpPwObjectMixinData *d = data;
  g_clear_pointer (&d->properties, wp_properties_unref);
  g_list_free_full (d->params, wp_pw_object_mixin_param_store_free);
  g_clear_pointer (&d->subscribed_ids, g_array_unref);
  g_warn_if_fail (d->enum_params_tasks == NULL);
  g_slice_free (WpPwObjectMixinData, d);
}

WpPwObjectMixinData *
wp_pw_object_mixin_get_data (gpointer instance)
{
  WpPwObjectMixinData *d = g_object_get_qdata (G_OBJECT (instance),
      wp_pw_object_mixin_data_quark ());
  if (G_UNLIKELY (!d)) {
    d = wp_pw_object_mixin_data_new ();
    g_object_set_qdata_full (G_OBJECT (instance),
        wp_pw_object_mixin_data_quark (), d, wp_pw_object_mixin_data_free);
  }
  return d;
}

/****************/
/* PARAMS STORE */

typedef struct _WpPwObjectMixinParamStore WpPwObjectMixinParamStore;
struct _WpPwObjectMixinParamStore
{
  guint32 param_id;
  GPtrArray *params;
};

static WpPwObjectMixinParamStore *
wp_pw_object_mixin_param_store_new (void)
{
  WpPwObjectMixinParamStore *d = g_slice_new0 (WpPwObjectMixinParamStore);
  return d;
}

static void
wp_pw_object_mixin_param_store_free (gpointer data)
{
  WpPwObjectMixinParamStore * p = data;
  g_clear_pointer (&p->params, g_ptr_array_unref);
  g_slice_free (WpPwObjectMixinParamStore, p);
}

static gint
param_store_has_id (gconstpointer param, gconstpointer id)
{
  guint32 param_id = ((const WpPwObjectMixinParamStore *) param)->param_id;
  return (param_id == GPOINTER_TO_UINT (id)) ? 0 : 1;
}

GPtrArray *
wp_pw_object_mixin_get_stored_params (WpPwObjectMixinData * data, guint32 id)
{
  GList *link = g_list_find_custom (data->params, GUINT_TO_POINTER (id),
      param_store_has_id);
  WpPwObjectMixinParamStore *s = link ? link->data : NULL;
  return (s && s->params) ? g_ptr_array_ref (s->params) : NULL;
}

void
wp_pw_object_mixin_store_param (WpPwObjectMixinData * data, guint32 id,
    guint32 flags, gpointer param)
{
  GList *link = g_list_find_custom (data->params, GUINT_TO_POINTER (id),
      param_store_has_id);
  WpPwObjectMixinParamStore *s = link ? link->data : NULL;
  gint16 index = (gint16) (flags & 0xffff);

  /* if the link exists, data must also exist */
  g_warn_if_fail (!link || link->data);

  if (!s) {
    if (flags & WP_PW_OBJECT_MIXIN_STORE_PARAM_REMOVE)
      return;
    s = wp_pw_object_mixin_param_store_new ();
    s->param_id = id;
    data->params = g_list_append (data->params, s);
  }
  else if (s && (flags & WP_PW_OBJECT_MIXIN_STORE_PARAM_REMOVE)) {
    wp_pw_object_mixin_param_store_free (s);
    data->params = g_list_delete_link (data->params, link);
    return;
  }

  if (flags & WP_PW_OBJECT_MIXIN_STORE_PARAM_CLEAR)
    g_clear_pointer (&s->params, g_ptr_array_unref);

  if (!param)
    return;

  if (flags & WP_PW_OBJECT_MIXIN_STORE_PARAM_ARRAY) {
    if (!s->params)
      s->params = (GPtrArray *) param;
    else
      g_ptr_array_extend_and_steal (s->params, (GPtrArray *) param);
  }
  else {
    WpSpaPod *param_pod = param;

    if (!s->params)
      s->params =
          g_ptr_array_new_with_free_func ((GDestroyNotify) wp_spa_pod_unref);

    /* copy if necessary to make sure we don't reference
       `const struct spa_pod *` data allocated on the stack */
    param_pod = wp_spa_pod_ensure_unique_owner (param_pod);
    g_ptr_array_insert (s->params, index, param_pod);
  }
}

/******************/
/* PROPERTIES API */

void
wp_pw_object_mixin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
  case WP_PW_OBJECT_MIXIN_PROP_NATIVE_INFO:
    g_value_set_pointer (value, (gpointer)
        wp_pipewire_object_get_native_info (WP_PIPEWIRE_OBJECT (object)));
    break;
  case WP_PW_OBJECT_MIXIN_PROP_PROPERTIES:
    g_value_take_boxed (value,
        wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (object)));
    break;
  case WP_PW_OBJECT_MIXIN_PROP_PARAM_INFO:
    g_value_set_variant (value,
        wp_pipewire_object_get_param_info (WP_PIPEWIRE_OBJECT (object)));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

void
wp_pw_object_mixin_class_override_properties (GObjectClass * klass)
{
  g_object_class_override_property (klass,
      WP_PW_OBJECT_MIXIN_PROP_NATIVE_INFO, "native-info");
  g_object_class_override_property (klass,
      WP_PW_OBJECT_MIXIN_PROP_PROPERTIES, "properties");
  g_object_class_override_property (klass,
      WP_PW_OBJECT_MIXIN_PROP_PARAM_INFO, "param-info");
}

/****************/
/* FEATURES API */

static const struct {
  WpObjectFeatures feature;
  guint32 param_ids[2];
} params_features[] = {
  { WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS, { SPA_PARAM_PropInfo, SPA_PARAM_Props } },
  { WP_PIPEWIRE_OBJECT_FEATURE_PARAM_FORMAT, { SPA_PARAM_EnumFormat, SPA_PARAM_Format } },
  { WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROFILE, { SPA_PARAM_EnumProfile, SPA_PARAM_Profile } },
  { WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PORT_CONFIG, { SPA_PARAM_EnumPortConfig, SPA_PARAM_PortConfig } },
  { WP_PIPEWIRE_OBJECT_FEATURE_PARAM_ROUTE, { SPA_PARAM_EnumRoute, SPA_PARAM_Route } },
};

static WpObjectFeatures
get_feature_for_param_id (guint32 param_id)
{
  for (guint i = 0; i < G_N_ELEMENTS (params_features); i++) {
    if (params_features[i].param_ids[0] == param_id ||
        params_features[i].param_ids[1] == param_id)
      return params_features[i].feature;
  }
  return 0;
}

WpObjectFeatures
wp_pw_object_mixin_get_supported_features (WpObject * object)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (object);
  WpPwObjectMixinPrivInterface *iface =
      WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (object);
  WpObjectFeatures ft =
      WP_PROXY_FEATURE_BOUND | WP_PIPEWIRE_OBJECT_FEATURE_INFO;

  if (d->info && iface->n_params_offset && iface->param_info_offset) {
    struct spa_param_info * param_info =
        G_STRUCT_MEMBER (struct spa_param_info *, d->info, iface->param_info_offset);
    guint32 n_params =
        G_STRUCT_MEMBER (guint32, d->info, iface->n_params_offset);

    for (guint i = 0; i < n_params; i++)
      ft |= get_feature_for_param_id (param_info[i].id);
  }
  return ft;
}

guint
wp_pw_object_mixin_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (object);

  /* bind if not already bound */
  if (missing & WP_PROXY_FEATURE_BOUND || !d->iface)
    return WP_PW_OBJECT_MIXIN_STEP_BIND;
  /* wait for info before proceeding, if necessary */
  else if ((missing & WP_PIPEWIRE_OBJECT_FEATURES_ALL) && !d->info)
    return WP_PW_OBJECT_MIXIN_STEP_WAIT_INFO;
  /* then cache params */
  else if (missing & WP_PIPEWIRE_OBJECT_FEATURES_ALL)
    return WP_PW_OBJECT_MIXIN_STEP_CACHE_PARAMS;
  else
    return WP_PW_OBJECT_MIXIN_STEP_CUSTOM_START;

  /* returning to STEP_NONE is handled by WpFeatureActivationTransition */
}

static void
enum_params_for_cache_done (GObject * object, GAsyncResult * res, gpointer data)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (object);
  guint32 param_id = GPOINTER_TO_UINT (data);
  g_autoptr (GError) error = NULL;
  g_autoptr (GPtrArray) params = NULL;
  const gchar *name = NULL;

  params = g_task_propagate_pointer (G_TASK (res), &error);
  if (error) {
    wp_debug_object (object, "enum params failed: %s", error->message);
    return;
  }

  name = wp_spa_id_value_short_name (wp_spa_id_value_from_number (
        "Spa:Enum:ParamId", param_id));

  wp_debug_object (object, "cached params id:%u (%s), n_params:%u", param_id,
      name, params->len);

  wp_pw_object_mixin_store_param (d, param_id,
      WP_PW_OBJECT_MIXIN_STORE_PARAM_ARRAY |
      WP_PW_OBJECT_MIXIN_STORE_PARAM_CLEAR |
      WP_PW_OBJECT_MIXIN_STORE_PARAM_APPEND,
      g_steal_pointer (&params));

  g_signal_emit_by_name (object, "params-changed", name);
}

G_DEFINE_QUARK (WpPwObjectMixinParamCacheActivatedFeatures, activated_features)

static void
param_cache_features_enabled (WpCore * core, GAsyncResult * res, gpointer data)
{
  WpObject *object = WP_OBJECT (data);
  WpObjectFeatures activated = GPOINTER_TO_UINT (
      g_object_get_qdata (G_OBJECT (object), activated_features_quark ()));
  wp_object_update_features (object, activated, 0);
}

void
wp_pw_object_mixin_cache_params (WpObject * object, WpObjectFeatures missing)
{
  WpPwObjectMixinPrivInterface *iface =
      WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (object);
  g_autoptr (WpCore) core = wp_object_get_core (object);
  struct spa_param_info * param_info;
  WpObjectFeatures activated = 0;

  g_return_if_fail (!(iface->flags & WP_PW_OBJECT_MIXIN_PRIV_NO_PARAM_CACHE));

  for (guint i = 0; i < G_N_ELEMENTS (params_features); i++) {
    if (missing & params_features[i].feature) {
      param_info = find_param_info (object, params_features[i].param_ids[0]);
      if (param_info && param_info->flags & SPA_PARAM_INFO_READ) {
        wp_pw_object_mixin_enum_params_unchecked (object,
            param_info->id, NULL, NULL, enum_params_for_cache_done,
            GUINT_TO_POINTER (param_info->id));
      }

      param_info = find_param_info (object, params_features[i].param_ids[1]);
      if (param_info && param_info->flags & SPA_PARAM_INFO_READ) {
        wp_pw_object_mixin_enum_params_unchecked (object,
            param_info->id, NULL, NULL, enum_params_for_cache_done,
            GUINT_TO_POINTER (param_info->id));
      }

      activated |= params_features[i].feature;
    }
  }

  g_object_set_qdata (G_OBJECT (object),
      activated_features_quark (), GUINT_TO_POINTER (activated));
  wp_core_sync (core, NULL,
      (GAsyncReadyCallback) param_cache_features_enabled, object);
}

void
wp_pw_object_mixin_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (object);
  WpPwObjectMixinPrivInterface *iface =
      WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (object);

  /* deactivate param caching */
  if (!(iface->flags & WP_PW_OBJECT_MIXIN_PRIV_NO_PARAM_CACHE)) {
    for (guint i = 0; i < G_N_ELEMENTS (params_features); i++) {
      if (features & params_features[i].feature) {
        wp_pw_object_mixin_store_param (d, params_features[i].param_ids[0],
            WP_PW_OBJECT_MIXIN_STORE_PARAM_REMOVE, NULL);
        wp_pw_object_mixin_store_param (d, params_features[i].param_ids[1],
            WP_PW_OBJECT_MIXIN_STORE_PARAM_REMOVE, NULL);
        wp_object_update_features (object, 0, params_features[i].feature);
      }
    }
  }
}

/************************/
/* PROXY EVENT HANDLERS */

void
wp_pw_object_mixin_handle_pw_proxy_destroyed (WpProxy * proxy)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (proxy);
  WpPwObjectMixinPrivInterface *iface =
      WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (proxy);

  g_clear_pointer (&d->properties, wp_properties_unref);
  g_clear_pointer (&d->info, iface->free_info);
  d->iface = NULL;

  /* deactivate param caching */
  if (!(iface->flags & WP_PW_OBJECT_MIXIN_PRIV_NO_PARAM_CACHE)) {
    for (guint i = 0; i < G_N_ELEMENTS (params_features); i++) {
      wp_pw_object_mixin_store_param (d, params_features[i].param_ids[0],
          WP_PW_OBJECT_MIXIN_STORE_PARAM_REMOVE, NULL);
      wp_pw_object_mixin_store_param (d, params_features[i].param_ids[1],
          WP_PW_OBJECT_MIXIN_STORE_PARAM_REMOVE, NULL);
    }
  }

  /* cancel enum_params tasks */
  {
    GList *link;
    for (link = g_list_first (d->enum_params_tasks);
         link; link = g_list_first (d->enum_params_tasks)) {
      GTask *task = G_TASK (link->data);
      d->enum_params_tasks = g_list_delete_link (d->enum_params_tasks, link);
      g_task_return_new_error (task,
          WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
          "pipewire proxy destroyed before finishing");
    }
  }

  wp_object_update_features (WP_OBJECT (proxy), 0,
      WP_PIPEWIRE_OBJECT_FEATURES_ALL);
}

/***************************/
/* PIPEWIRE EVENT HANDLERS */

void
wp_pw_object_mixin_handle_event_info (gpointer instance, gconstpointer update)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  WpPwObjectMixinPrivInterface *iface =
      WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (instance);
  guint64 change_mask =
      G_STRUCT_MEMBER (guint64, update, iface->change_mask_offset);
  guint64 process_info_change_mask =
      change_mask & ~(iface->CHANGE_MASK_PROPS | iface->CHANGE_MASK_PARAMS);
  gpointer old_info = NULL;

  wp_debug_object (instance, "info, change_mask:0x%"G_GINT64_MODIFIER"x [%s%s]",
      change_mask,
      (change_mask & iface->CHANGE_MASK_PROPS) ? "props," : "",
      (change_mask & iface->CHANGE_MASK_PARAMS) ? "params," : "");

  /* make a copy of d->info for process_info() */
  if (iface->process_info && d->info && process_info_change_mask) {
    /* copy everything that changed except props and params, for efficiency;
       process_info() is only interested in variables that are not PROPS & PARAMS */
    G_STRUCT_MEMBER (guint64, d->info, iface->change_mask_offset) =
        process_info_change_mask;
    old_info = iface->update_info (NULL, d->info);
  }

  /* update params */
  if (!(iface->flags & WP_PW_OBJECT_MIXIN_PRIV_NO_PARAM_CACHE) &&
       (change_mask & iface->CHANGE_MASK_PARAMS) && d->info) {
    struct spa_param_info * old_param_info =
        G_STRUCT_MEMBER (struct spa_param_info *, d->info, iface->param_info_offset);
    struct spa_param_info * param_info =
        G_STRUCT_MEMBER (struct spa_param_info *, update, iface->param_info_offset);
    guint32 old_n_params =
        G_STRUCT_MEMBER (guint32, d->info, iface->n_params_offset);
    guint32 n_params =
        G_STRUCT_MEMBER (guint32, update, iface->n_params_offset);
    WpObjectFeatures active_ft =
        wp_object_get_active_features (WP_OBJECT (instance));

    for (guint i = 0; i < n_params; i++) {
      /* param changes when flags change */
      if (i >= old_n_params || old_param_info[i].flags != param_info[i].flags) {
        /* update cached params if the relevant feature is active */
        if (active_ft & get_feature_for_param_id (param_info[i].id) &&
            param_info[i].flags & SPA_PARAM_INFO_READ)
        {
          wp_pw_object_mixin_enum_params_unchecked (instance,
              param_info[i].id, NULL, NULL, enum_params_for_cache_done,
              GUINT_TO_POINTER (param_info[i].id));
        }
      }
    }
  }

  /* update our info struct */
  d->info = iface->update_info (d->info, update);
  wp_object_update_features (WP_OBJECT (instance),
      WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  /* update properties */
  if (change_mask & iface->CHANGE_MASK_PROPS) {
    const struct spa_dict * props =
        G_STRUCT_MEMBER (const struct spa_dict *, d->info, iface->props_offset);

    g_clear_pointer (&d->properties, wp_properties_unref);
    d->properties = wp_properties_new_wrap_dict (props);

    g_object_notify (G_OBJECT (instance), "properties");
  }

  if (change_mask & iface->CHANGE_MASK_PARAMS)
    g_object_notify (G_OBJECT (instance), "param-info");

  /* custom handling, if required */
  if (iface->process_info && process_info_change_mask) {
    iface->process_info (instance, old_info, d->info);
    g_clear_pointer (&old_info, iface->free_info);
  }
}

static gint
task_has_seq (gconstpointer task, gconstpointer seq)
{
  gpointer t_seq = g_task_get_source_tag (G_TASK (task));
  return (GPOINTER_TO_INT (t_seq) == GPOINTER_TO_INT (seq)) ? 0 : 1;
}

void
wp_pw_object_mixin_handle_event_param (gpointer instance, int seq,
    uint32_t id, uint32_t index, uint32_t next, const struct spa_pod *param)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  g_autoptr (WpSpaPod) w_param = wp_spa_pod_new_wrap_const (param);
  GList *list;
  GTask *task;

  list = g_list_find_custom (d->enum_params_tasks, GINT_TO_POINTER (seq),
      task_has_seq);
  task = list ? G_TASK (list->data) : NULL;

  wp_trace_boxed (WP_TYPE_SPA_POD, w_param,
      WP_OBJECT_FORMAT " param id:%u, index:%u",
      WP_OBJECT_ARGS (instance), id, index);

  if (task) {
    GPtrArray *array = g_task_get_task_data (task);
    g_ptr_array_add (array, wp_spa_pod_copy (w_param));
  } else {
    /* this should never happen */
    wp_warning_object (instance,
        "param event was received without calling enum_params");
  }
}

/***********************************/
/* PIPEWIRE METHOD IMPLEMENTATIONS */

int
wp_pw_object_mixin_impl_add_listener (gpointer instance,
    struct spa_hook *listener, gconstpointer events, gpointer data)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  WpPwObjectMixinPrivInterface *iface =
      WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (instance);
  struct spa_hook_list save;

  spa_hook_list_isolate (&d->hooks, &save, listener, events, data);

  G_STRUCT_MEMBER (guint64, d->info, iface->change_mask_offset) = iface->CHANGE_MASK_ALL;
  iface->emit_info (&d->hooks, d->info);
  G_STRUCT_MEMBER (guint64, d->info, iface->change_mask_offset) = 0;

  spa_hook_list_join (&d->hooks, &save);
  return 0;
}

int
wp_pw_object_mixin_impl_enum_params (gpointer instance, int seq,
    guint32 id, guint32 start, guint32 num, const struct spa_pod *filter)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  WpPwObjectMixinPrivInterface *iface =
      WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (instance);
  g_autoptr (GPtrArray) params = NULL;
  g_autoptr (WpSpaPod) filter_pod = NULL;

  if (!iface->enum_params_sync)
    return -ENOTSUP;

  struct spa_param_info * info = find_param_info (instance, id);
  if (!info || !(info->flags & SPA_PARAM_INFO_READ))
    return -EINVAL;

  filter_pod = filter ? wp_spa_pod_new_wrap_const (filter) : NULL;
  params = iface->enum_params_sync (instance, id, start, num, filter_pod);

  if (params) {
    for (guint i = 0; i < params->len; i++) {
      WpSpaPod *pod = g_ptr_array_index (params, i);

      wp_trace_boxed (WP_TYPE_SPA_POD, pod,
          WP_OBJECT_FORMAT " emit param id:%u, index:%u",
          WP_OBJECT_ARGS (instance), id, start+i);

      iface->emit_param (&d->hooks, seq, id, start+i, start+i+1,
          wp_spa_pod_get_spa_pod (pod));
    }
  }
  return 0;
}

int
wp_pw_object_mixin_impl_subscribe_params (gpointer instance,
    guint32 *ids, guint32 n_ids)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  WpPwObjectMixinPrivInterface *iface =
      WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (instance);

  if (!iface->enum_params_sync)
    return -ENOTSUP;

  for (guint i = 0; i < n_ids; i++)
    wp_pw_object_mixin_impl_enum_params (instance, 1, ids[i], 0, -1, NULL);

  if (!d->subscribed_ids)
    d->subscribed_ids = g_array_new (FALSE, FALSE, sizeof (guint32));

  /* FIXME: deduplicate stored ids */
  g_array_append_vals (d->subscribed_ids, ids, n_ids);
  return 0;
}

int
wp_pw_object_mixin_impl_set_param (gpointer instance, guint32 id,
    guint32 flags, const struct spa_pod *param)
{
  WpPwObjectMixinPrivInterface *iface =
      WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (instance);

  if (!iface->set_param)
    return -ENOTSUP;

  struct spa_param_info * info = find_param_info (instance, id);
  if (!info || !(info->flags & SPA_PARAM_INFO_WRITE))
    return -EINVAL;

  WpSpaPod *param_pod = wp_spa_pod_new_wrap_const (param);

  wp_trace_boxed (WP_TYPE_SPA_POD, param_pod,
          WP_OBJECT_FORMAT " set_param id:%u flags:0x%x",
          WP_OBJECT_ARGS (instance), id, flags);

  return iface->set_param (instance, id, flags, param_pod);
}

/**********************/
/*      NOTIFIERS     */

void
wp_pw_object_mixin_notify_info (gpointer instance, guint32 change_mask)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  WpPwObjectMixinPrivInterface *iface =
      WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (instance);

  wp_debug_object (instance, "notify info, change_mask:0x%x [%s%s]",
      change_mask,
      (change_mask & iface->CHANGE_MASK_PROPS) ? "props," : "",
      (change_mask & iface->CHANGE_MASK_PARAMS) ? "params," : "");

  G_STRUCT_MEMBER (guint64, d->info, iface->change_mask_offset) =
      (change_mask & iface->CHANGE_MASK_ALL);
  iface->emit_info (&d->hooks, d->info);
  G_STRUCT_MEMBER (guint64, d->info, iface->change_mask_offset) = 0;

  if (change_mask & iface->CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (instance), "properties");

  if (change_mask & iface->CHANGE_MASK_PARAMS)
    g_object_notify (G_OBJECT (instance), "param-info");
}

void
wp_pw_object_mixin_notify_params_changed (gpointer instance, guint32 id)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  WpPwObjectMixinPrivInterface *iface =
      WP_PW_OBJECT_MIXIN_PRIV_GET_IFACE (instance);
  gboolean subscribed = FALSE;
  const gchar *name = NULL;

  struct spa_param_info * info = find_param_info (instance, id);
  g_return_if_fail (info);

  if (d->subscribed_ids) {
    for (guint i = 0; i < d->subscribed_ids->len; i++) {
      if (g_array_index (d->subscribed_ids, guint32, i) == id) {
        subscribed = TRUE;
        break;
      }
    }
  }

  name = wp_spa_id_value_short_name (wp_spa_id_value_from_number (
        "Spa:Enum:ParamId", id));

  wp_debug_object (instance, "notify param id:%u (%s)", id, name);

  /* toggle the serial flag; this notifies that there is a data change */
  info->flags ^= SPA_PARAM_INFO_SERIAL;

  G_STRUCT_MEMBER (guint64, d->info, iface->change_mask_offset) =
      iface->CHANGE_MASK_PARAMS;
  iface->emit_info (&d->hooks, d->info);
  G_STRUCT_MEMBER (guint64, d->info, iface->change_mask_offset) = 0;

  if (subscribed)
    wp_pw_object_mixin_impl_enum_params (instance, 1, id, 0, -1, NULL);

  g_signal_emit_by_name (instance, "params-changed", name);
}
