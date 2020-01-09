/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "proxy-node.h"
#include "error.h"
#include "private.h"

#include <pipewire/pipewire.h>
#include <spa/pod/builder.h>

struct _WpProxyNode
{
  WpProxy parent;
  struct pw_node_info *info;

  /* The node proxy listener */
  struct spa_hook listener;
};

enum {
  PROP_0,
  PROP_INFO,
  PROP_PROPERTIES,
};

enum {
  SIGNAL_PARAM,
  N_SIGNALS
};

static guint32 signals[N_SIGNALS];

G_DEFINE_TYPE (WpProxyNode, wp_proxy_node, WP_TYPE_PROXY)

static void
wp_proxy_node_init (WpProxyNode * self)
{
}

static void
wp_proxy_node_finalize (GObject * object)
{
  WpProxyNode *self = WP_PROXY_NODE (object);

  g_clear_pointer (&self->info, pw_node_info_free);

  G_OBJECT_CLASS (wp_proxy_node_parent_class)->finalize (object);
}

static void
wp_proxy_node_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpProxyNode *self = WP_PROXY_NODE (object);

  switch (property_id) {
  case PROP_INFO:
    g_value_set_pointer (value, self->info);
    break;
  case PROP_PROPERTIES:
    g_value_take_boxed (value, wp_proxy_node_get_properties (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
node_event_info(void *data, const struct pw_node_info *info)
{
  WpProxyNode *self = WP_PROXY_NODE (data);

  self->info = pw_node_info_update (self->info, info);
  g_object_notify (G_OBJECT (self), "info");

  if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS)
    g_object_notify (G_OBJECT (self), "properties");

  wp_proxy_set_feature_ready (WP_PROXY (self), WP_PROXY_FEATURE_INFO);
}

static void
node_event_param (void *data, int seq, uint32_t id, uint32_t index,
    uint32_t next, const struct spa_pod *param)
{
  WpProxyNode *self = WP_PROXY_NODE (data);
  GTask *task;

  g_signal_emit (self, signals[SIGNAL_PARAM], 0, seq, id, index, next, param);

  /* if this param event was emited because of enum_params_collect(),
   * copy the param in the result array of that API */
  task = wp_proxy_find_async_task (WP_PROXY (self), seq, FALSE);
  if (task) {
    GPtrArray *array = g_task_get_task_data (task);
    g_ptr_array_add (array, spa_pod_copy (param));
  }
}

static const struct pw_node_events node_events = {
  PW_VERSION_NODE_EVENTS,
  .info = node_event_info,
  .param = node_event_param,
};

static void
wp_proxy_node_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpProxyNode *self = WP_PROXY_NODE (proxy);
  pw_node_add_listener ((struct pw_node *) pw_proxy,
      &self->listener, &node_events, self);
}

static void
wp_proxy_node_class_init (WpProxyNodeClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->finalize = wp_proxy_node_finalize;
  object_class->get_property = wp_proxy_node_get_property;

  proxy_class->pw_proxy_created = wp_proxy_node_pw_proxy_created;

  g_object_class_install_property (object_class, PROP_INFO,
      g_param_spec_pointer ("info", "info", "The struct pw_node_info *",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties",
          "The pipewire properties of the proxy", WP_TYPE_PROPERTIES,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_PARAM] = g_signal_new ("param", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 5,
      G_TYPE_INT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_POINTER);
}

const struct pw_node_info *
wp_proxy_node_get_info (WpProxyNode * self)
{
  return self->info;
}

WpProperties *
wp_proxy_node_get_properties (WpProxyNode * self)
{
  return wp_properties_new_wrap_dict (self->info->props);
}

static void
enum_params_done (WpProxy * proxy, GAsyncResult * res, gpointer data)
{
  int seq = GPOINTER_TO_INT (data);
  g_autoptr (GError) error = NULL;
  g_autoptr (GTask) task = NULL;

  /* finish the sync task */
  wp_proxy_sync_finish (proxy, res, &error);

  /* find the enum params task and return from it */
  task = wp_proxy_find_async_task (proxy, seq, TRUE);
  g_return_if_fail (task != NULL);

  if (error)
    g_task_return_error (task, g_steal_pointer (&error));
  else {
    GPtrArray *params = g_task_get_task_data (task);
    g_task_return_pointer (task, g_ptr_array_ref (params),
        (GDestroyNotify) g_ptr_array_unref);
  }
}

void
wp_proxy_node_enum_params_collect (WpProxyNode * self,
    guint32 id, const struct spa_pod *filter,
    GCancellable * cancellable, GAsyncReadyCallback callback,
    gpointer user_data)
{
  g_autoptr (GTask) task = NULL;
  int seq;
  GPtrArray *params;

  g_return_if_fail (WP_IS_PROXY_NODE (self));

  /* create task for enum_params */
  task = g_task_new (self, cancellable, callback, user_data);
  params = g_ptr_array_new_with_free_func (free);
  g_task_set_task_data (task, params, (GDestroyNotify) g_ptr_array_unref);

  /* call enum_params */
  seq = wp_proxy_node_enum_params (self, id, filter);
  if (G_UNLIKELY (seq < 0)) {
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED, "enum_params failed: %s",
        g_strerror (-seq));
    return;
  }
  wp_proxy_register_async_task (WP_PROXY (self), seq, g_steal_pointer (&task));

  /* call sync */
  wp_proxy_sync (WP_PROXY (self), cancellable,
      (GAsyncReadyCallback) enum_params_done, GINT_TO_POINTER (seq));
}

/**
 * wp_proxy_node_enum_params_collect_finish:
 *
 * Returns: (transfer full) (element-type spa_pod*):
 *    the collected params
 */
GPtrArray *
wp_proxy_node_enum_params_collect_finish (WpProxyNode * self,
    GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_PROXY_NODE (self), NULL);
  g_return_val_if_fail (g_task_is_valid (res, self), NULL);

  return g_task_propagate_pointer (G_TASK (res), error);
}

gint
wp_proxy_node_enum_params (WpProxyNode * self,
    guint32 id, const struct spa_pod *filter)
{
  struct pw_node *pwp;
  int enum_params_result;

  g_return_val_if_fail (WP_IS_PROXY_NODE (self), -EINVAL);

  pwp = (struct pw_node *) wp_proxy_get_pw_proxy (WP_PROXY (self));
  g_return_val_if_fail (pwp != NULL, -EINVAL);

  enum_params_result = pw_node_enum_params (pwp, 0, id, 0, -1, filter);
  g_warn_if_fail (enum_params_result >= 0);

  return enum_params_result;
}

void
wp_proxy_node_subscribe_params (WpProxyNode * self, guint32 n_ids, ...)
{
  va_list args;
  guint32 *ids = g_alloca (n_ids * sizeof (guint32));

  va_start (args, n_ids);
  for (gint i = 0; i < n_ids; i++)
    ids[i] = va_arg (args, guint32);
  va_end (args);

  wp_proxy_node_subscribe_params_array (self, n_ids, ids);
}

void
wp_proxy_node_subscribe_params_array (WpProxyNode * self, guint32 n_ids,
    guint32 *ids)
{
  struct pw_node *pwp;
  int node_subscribe_params_result;

  g_return_if_fail (WP_IS_PROXY_NODE (self));

  pwp = (struct pw_node *) wp_proxy_get_pw_proxy (WP_PROXY (self));
  g_return_if_fail (pwp != NULL);

  node_subscribe_params_result = pw_node_subscribe_params (
      pwp, ids, n_ids);
  g_warn_if_fail (node_subscribe_params_result >= 0);
}

void
wp_proxy_node_set_param (WpProxyNode * self, guint32 id,
    guint32 flags, const struct spa_pod *param)
{
  struct pw_node *pwp;
  int node_set_param_result;

  g_return_if_fail (WP_IS_PROXY_NODE (self));

  pwp = (struct pw_node *) wp_proxy_get_pw_proxy (WP_PROXY (self));
  g_return_if_fail (pwp != NULL);

  node_set_param_result = pw_node_set_param (pwp, id, flags, param);
  g_warn_if_fail (node_set_param_result >= 0);
}
