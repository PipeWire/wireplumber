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
#include "error.h"

G_DEFINE_QUARK (WpPipewireObjectMixinEnumParamsTasks, enum_params_tasks)

void
wp_pipewire_object_mixin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
  case WP_PIPEWIRE_OBJECT_MIXIN_PROP_NATIVE_INFO:
    g_value_set_pointer (value, (gpointer)
        wp_pipewire_object_get_native_info (WP_PIPEWIRE_OBJECT (object)));
    break;
  case WP_PIPEWIRE_OBJECT_MIXIN_PROP_PROPERTIES:
    g_value_set_boxed (value,
        wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (object)));
    break;
  case WP_PIPEWIRE_OBJECT_MIXIN_PROP_PARAM_INFO:
    g_value_set_variant (value,
        wp_pipewire_object_get_param_info (WP_PIPEWIRE_OBJECT (object)));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

void
wp_pipewire_object_mixin_class_override_properties (GObjectClass * klass)
{
  g_object_class_override_property (klass,
      WP_PIPEWIRE_OBJECT_MIXIN_PROP_NATIVE_INFO, "native-info");
  g_object_class_override_property (klass,
      WP_PIPEWIRE_OBJECT_MIXIN_PROP_PROPERTIES, "properties");
  g_object_class_override_property (klass,
      WP_PIPEWIRE_OBJECT_MIXIN_PROP_PARAM_INFO, "param-info");
}

static const struct {
  gint param_id;
  WpObjectFeatures feature;
} feature_mappings[] = {
  { SPA_PARAM_Props, WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROPS },
  { SPA_PARAM_Format, WP_PIPEWIRE_OBJECT_FEATURE_PARAM_FORMAT },
  { SPA_PARAM_Profile, WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PROFILE },
  { SPA_PARAM_PortConfig, WP_PIPEWIRE_OBJECT_FEATURE_PARAM_PORT_CONFIG },
  { SPA_PARAM_Route, WP_PIPEWIRE_OBJECT_FEATURE_PARAM_ROUTE },
};

WpObjectFeatures
wp_pipewire_object_mixin_param_info_to_features (struct spa_param_info * info,
    guint n_params)
{
  WpObjectFeatures ft = 0;

  for (gint i = 0; i < n_params; i++) {
    for (gint j = 0; j < G_N_ELEMENTS (feature_mappings); j++) {
      if (info[i].id == feature_mappings[j].param_id &&
          info[i].flags & SPA_PARAM_INFO_READ)
        ft |= feature_mappings[j].feature;
    }
  }
  return ft;
}

guint
wp_pipewire_object_mixin_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  /* bind if not already bound */
  if (missing & WP_PROXY_FEATURE_BOUND)
    return WP_PIPEWIRE_OBJECT_MIXIN_STEP_BIND;
  /* then cache info */
  else
    return WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO;

  /* returning to STEP_NONE is handled by WpFeatureActivationTransition */
}

void
wp_pipewire_object_mixin_cache_info (WpObject * object,
    WpFeatureActivationTransition * transition)
{
  /* TODO */
}

void
wp_pipewire_object_mixin_deactivate (WpObject * object,
    WpObjectFeatures features)
{
  /* TODO */
}

static gint
task_has_seq (gconstpointer task, gconstpointer seq)
{
  gpointer t_seq = g_task_get_source_tag (G_TASK (task));
  return (GPOINTER_TO_INT (t_seq) == GPOINTER_TO_INT (seq)) ? 0 : 1;
}

void
wp_pipewire_object_mixin_handle_event_param (gpointer instance, int seq,
    uint32_t id, uint32_t index, uint32_t next, const struct spa_pod *param)
{
  g_autoptr (WpSpaPod) w_param = wp_spa_pod_new_wrap_const (param);
  GList *list;
  GTask *task;

  list = g_object_get_qdata (G_OBJECT (instance), enum_params_tasks_quark ());
  list = g_list_find_custom (list, GINT_TO_POINTER (seq), task_has_seq);
  task = list ? G_TASK (list->data) : NULL;

  if (task) {
    GPtrArray *array = g_task_get_task_data (task);
    g_ptr_array_add (array, wp_spa_pod_copy (w_param));
  }
}

GVariant *
wp_pipewire_object_mixin_param_info_to_gvariant (struct spa_param_info * info,
    guint n_params)
{
  g_auto (GVariantBuilder) b =
      G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_DICTIONARY);

  if (!info || n_params == 0)
    return NULL;

  for (guint i = 0; i < n_params; i++) {
    const gchar *nick = NULL;
    gchar flags[3];
    guint flags_idx = 0;

    wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_PARAM, info[i].id, NULL, &nick,
        NULL);
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

WpIterator *
wp_pipewire_object_mixin_enum_params_finish (WpPipewireObject * obj,
    GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (g_task_is_valid (res, obj), NULL);

  GPtrArray *array = g_task_propagate_pointer (G_TASK (res), error);
  if (!array)
    return NULL;

  return wp_iterator_new_ptr_array (array, WP_TYPE_SPA_POD);
}

WpIterator *
wp_pipewire_object_mixin_enum_cached_params (WpPipewireObject * obj,
    const gchar * id)
{
  return NULL; //TODO
}

void
wp_pipewire_object_mixin_enum_params_unimplemented (WpPipewireObject * obj,
    const gchar * id, WpSpaPod *filter, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  wp_pipewire_object_mixin_create_enum_params_task (obj, 0, cancellable,
      callback, user_data);
}

void
wp_pipewire_object_mixin_set_param_unimplemented (WpPipewireObject * obj,
    const gchar * id, WpSpaPod * param)
{
  wp_warning_object (obj,
      "setting params is not implemented on this object type");
}

static void
enum_params_done (WpCore * core, GAsyncResult * res, gpointer data)
{
  g_autoptr (GTask) task = G_TASK (data);
  g_autoptr (GError) error = NULL;
  gpointer instance = g_task_get_source_object (G_TASK (data));
  GList *list;

  /* finish the sync task */
  wp_core_sync_finish (core, res, &error);

  /* remove the task from the stored list; ref is held by the g_autoptr */
  list = g_object_get_qdata (G_OBJECT (instance), enum_params_tasks_quark ());
  list = g_list_remove (list, instance);
  g_object_set_qdata (G_OBJECT (instance), enum_params_tasks_quark (), list);

  if (error)
    g_task_return_error (task, g_steal_pointer (&error));
  else {
    GPtrArray *params = g_task_get_task_data (task);
    g_task_return_pointer (task, g_ptr_array_ref (params),
        (GDestroyNotify) g_ptr_array_unref);
  }
}

void
wp_pipewire_object_mixin_create_enum_params_task (gpointer instance,
    gint seq, GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data)
{
  g_autoptr (GTask) task = NULL;
  GPtrArray *params;
  GList *list;

  /* create task */
  task = g_task_new (instance, cancellable, callback, user_data);
  params = g_ptr_array_new_with_free_func ((GDestroyNotify) wp_spa_pod_unref);
  g_task_set_task_data (task, params, (GDestroyNotify) g_ptr_array_unref);
  g_task_set_source_tag (task, GINT_TO_POINTER (seq));

  /* return early if seq contains an error */
  if (G_UNLIKELY (SPA_RESULT_IS_ERROR (seq))) {
    wp_message_object (instance, "enum_params failed: %s", spa_strerror (seq));
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED, "enum_params failed: %s",
        spa_strerror (seq));
    return;
  }

  /* store */
  list = g_object_get_qdata (G_OBJECT (instance), enum_params_tasks_quark ());
  list = g_list_append (list, g_object_ref (task));
  g_object_set_qdata (G_OBJECT (instance), enum_params_tasks_quark (), list);

  /* call sync */
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (instance));
  wp_core_sync (core, cancellable, (GAsyncReadyCallback) enum_params_done,
      task);
}
