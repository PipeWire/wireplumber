/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "proxy.h"
#include "error.h"
#include <pipewire/pipewire.h>
#include <spa/debug/types.h>
#include <spa/pod/builder.h>

struct _WpProxy
{
  GObject parent;

  WpObject *core;

  struct pw_proxy *proxy;
  guint32 id;
  guint32 parent_id;
  guint32 type;
  const gchar *type_string;

  struct spa_hook proxy_listener;
  struct spa_hook proxy_proxy_listener;

  union {
    const struct spa_dict *initial_properties;
    struct pw_properties *properties;
  };

  union {
    gpointer info;
    struct pw_node_info *node_info;
    struct pw_port_info *port_info;
    struct pw_factory_info *factory_info;
    struct pw_link_info *link_info;
    struct pw_client_info *client_info;
    struct pw_module_info *module_info;
    struct pw_device_info *device_info;
  };

  GList *tasks;
};

enum {
  PROP_0,
  PROP_ID,
  PROP_PARENT_ID,
  PROP_SPA_TYPE,
  PROP_SPA_TYPE_STRING,
  PROP_INITIAL_PROPERTIES,
  PROP_CORE,
  PROP_PROXY,
};

enum {
  SIGNAL_DESTROYED,
  SIGNAL_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];
static int global_seq = 0;

static void wp_proxy_pw_properties_init (WpPipewirePropertiesInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpProxy, wp_proxy, WP_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_PROPERTIES, wp_proxy_pw_properties_init);
)

struct task_data {
  guint32 seq;
  GPtrArray *result;
};

static gint
find_task (gconstpointer t, gconstpointer s)
{
  GTask *task = (GTask *) t;
  int seq = GPOINTER_TO_INT (s);
  struct task_data *data = g_task_get_task_data (task);

  return data->seq - seq;
}

/* Updates the info structure while copying the properties into self->properties
 * and avoiding making a second copy of the properties dict in info->props */
#define PROXY_INFO_FUNC(TYPE, type) \
  static void \
  type##_event_info (void *object, const struct pw_##type##_info *info) \
  { \
    WpProxy *self = WP_PROXY (object); \
    \
    if (info->change_mask & PW_##TYPE##_CHANGE_MASK_PROPS) { \
      g_clear_pointer (&self->properties, pw_properties_free); \
      self->properties = pw_properties_new_dict (info->props); \
    } \
    \
    { \
      uint64_t change_mask = info->change_mask; \
      ((struct pw_##type##_info *)info)->change_mask &= ~PW_##TYPE##_CHANGE_MASK_PROPS; \
      self->type##_info = pw_##type##_info_update (self->type##_info, info); \
      self->type##_info->props = &self->properties->dict; \
      ((struct pw_##type##_info *)info)->change_mask = change_mask; \
    } \
    \
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0); \
  }

#define PROXY_PARAM_FUNC(type) \
  static void \
  type##_event_param(void *object, int seq, uint32_t id, uint32_t index, \
      uint32_t next, const struct spa_pod *param) \
  { \
    WpProxy *self = WP_PROXY (object); \
    GList *l; \
    struct task_data *data; \
    \
    l = g_list_find_custom (self->tasks, GINT_TO_POINTER (seq), find_task); \
    g_return_if_fail (l != NULL); \
    \
    data = g_task_get_task_data (G_TASK (l->data)); \
    g_ptr_array_add (data->result, spa_pod_copy (param)); \
  }

PROXY_INFO_FUNC (NODE, node)
PROXY_PARAM_FUNC(node)

static const struct pw_node_proxy_events node_events = {
  PW_VERSION_NODE_PROXY_EVENTS,
  .info = node_event_info,
  .param = node_event_param,
};

PROXY_INFO_FUNC (PORT, port)
PROXY_PARAM_FUNC(port)

static const struct pw_port_proxy_events port_events = {
  PW_VERSION_PORT_PROXY_EVENTS,
  .info = port_event_info,
  .param = port_event_param,
};

PROXY_INFO_FUNC (FACTORY, factory)

static const struct pw_factory_proxy_events factory_events = {
  PW_VERSION_FACTORY_PROXY_EVENTS,
  .info = factory_event_info
};

PROXY_INFO_FUNC (LINK, link)

static const struct pw_link_proxy_events link_events = {
  PW_VERSION_LINK_PROXY_EVENTS,
  .info = link_event_info
};

PROXY_INFO_FUNC (CLIENT, client)

static const struct pw_client_proxy_events client_events = {
  PW_VERSION_CLIENT_PROXY_EVENTS,
  .info = client_event_info
};

PROXY_INFO_FUNC (MODULE, module)

static const struct pw_module_proxy_events module_events = {
  PW_VERSION_MODULE_PROXY_EVENTS,
  .info = module_event_info
};

PROXY_INFO_FUNC (DEVICE, device)
PROXY_PARAM_FUNC (device)

static const struct pw_device_proxy_events device_events = {
  PW_VERSION_DEVICE_PROXY_EVENTS,
  .info = device_event_info,
  .param = device_event_param,
};

static void
proxy_event_destroy (void *object)
{
  WpProxy *self = WP_PROXY (object);

  g_debug ("proxy %u destroyed", self->id);

  self->proxy = NULL;
  g_signal_emit (self, signals[SIGNAL_DESTROYED], 0);
}

static void
proxy_event_done (void *object, int seq)
{
  WpProxy *self = WP_PROXY (object);
  GList *l;
  struct task_data *data;

  l = g_list_find_custom (self->tasks, GINT_TO_POINTER (seq), find_task);
  g_return_if_fail (l != NULL);

  data = g_task_get_task_data (G_TASK (l->data));
  g_task_return_pointer (G_TASK (l->data), g_ptr_array_ref (data->result),
      (GDestroyNotify) g_ptr_array_unref);
}

static const struct pw_proxy_events proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_event_destroy,
  .done = proxy_event_done,
};

static void
wp_proxy_init (WpProxy * self)
{
}

static void
wp_proxy_constructed (GObject * object)
{
  WpProxy *self = WP_PROXY (object);
  WpProxyRegistry *pr = NULL;
  struct pw_registry_proxy *reg_proxy;
  const void *events = NULL;
  uint32_t ver = 0;

  self->type_string = spa_debug_type_find_name (pw_type_info (), self->type);

  g_debug ("added id %u, parent %u, type %s", self->id, self->parent_id,
      self->type_string);

  switch (self->type) {
    case PW_TYPE_INTERFACE_Node:
      events = &node_events;
      ver = PW_VERSION_NODE;
      break;
    case PW_TYPE_INTERFACE_Port:
      events = &port_events;
      ver = PW_VERSION_PORT;
      break;
    case PW_TYPE_INTERFACE_Factory:
      events = &factory_events;
      ver = PW_VERSION_FACTORY;
      break;
    case PW_TYPE_INTERFACE_Link:
      events = &link_events;
      ver = PW_VERSION_LINK;
      break;
    case PW_TYPE_INTERFACE_Client:
      events = &client_events;
      ver = PW_VERSION_CLIENT;
      break;
    case PW_TYPE_INTERFACE_Module:
      events = &module_events;
      ver = PW_VERSION_MODULE;
      break;
    case PW_TYPE_INTERFACE_Device:
      events = &device_events;
      ver = PW_VERSION_DEVICE;
      break;
    default:
      break;
  }

  pr = wp_object_get_interface (self->core, WP_TYPE_PROXY_REGISTRY);
  reg_proxy = wp_proxy_registry_get_pw_registry_proxy (pr);
  g_warn_if_fail (reg_proxy != NULL);

  self->proxy = pw_registry_proxy_bind (reg_proxy, self->id, self->type, ver, 0);
  pw_proxy_add_listener (self->proxy, &self->proxy_listener, &proxy_events,
      self);

  if (events)
    pw_proxy_add_proxy_listener(self->proxy, &self->proxy_proxy_listener,
        events, self);

  /*
   * initial_properties is a stack-allocated const spa_dict *
   * that is not safe to access beyond the scope of the g_object_new()
   * call, so we replace it with a pw_properties
   */
  if (self->initial_properties)
    self->properties = pw_properties_new_dict (self->initial_properties);
  else
    self->properties = pw_properties_new (NULL);

  G_OBJECT_CLASS (wp_proxy_parent_class)->constructed (object);
}

static void
wp_proxy_finalize (GObject * object)
{
  WpProxy *self = WP_PROXY (object);

  g_clear_pointer (&self->properties, pw_properties_free);
  g_clear_object (&self->core);

  G_OBJECT_CLASS (wp_proxy_parent_class)->finalize (object);
}

static void
wp_proxy_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpProxy *self = WP_PROXY (object);

  switch (property_id) {
  case PROP_ID:
    self->id = g_value_get_uint (value);
    break;
  case PROP_PARENT_ID:
    self->parent_id = g_value_get_uint (value);
    break;
  case PROP_SPA_TYPE:
    self->type = g_value_get_uint (value);
    break;
  case PROP_INITIAL_PROPERTIES:
    self->initial_properties = g_value_get_pointer (value);
    break;
  case PROP_CORE:
    self->core = g_value_get_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpProxy *self = WP_PROXY (object);

  switch (property_id) {
  case PROP_ID:
    g_value_set_uint (value, self->id);
    break;
  case PROP_PARENT_ID:
    g_value_set_uint (value, self->parent_id);
    break;
  case PROP_SPA_TYPE:
    g_value_set_uint (value, self->type);
    break;
  case PROP_SPA_TYPE_STRING:
    g_value_set_string (value, self->type_string);
    break;
  case PROP_CORE:
    g_value_set_object (value, self->core);
    break;
  case PROP_PROXY:
    g_value_set_pointer (value, self->proxy);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_proxy_class_init (WpProxyClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_proxy_constructed;
  object_class->finalize = wp_proxy_finalize;
  object_class->get_property = wp_proxy_get_property;
  object_class->set_property = wp_proxy_set_property;

  g_object_class_install_property (object_class, PROP_ID,
      g_param_spec_uint ("id", "id",
          "The global ID of the object", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PARENT_ID,
      g_param_spec_uint ("parent-id", "parent-id",
          "The global ID of the parent object", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SPA_TYPE,
      g_param_spec_uint ("spa-type", "spa-type",
          "The SPA type of the object", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SPA_TYPE_STRING,
      g_param_spec_string ("spa-type-string", "spa-type-string",
          "The string representation of the SPA type of the object", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_INITIAL_PROPERTIES,
      g_param_spec_pointer ("initial-properties", "initial-properties",
          "The initial set of properties of the proxy",
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The core that owns this proxy",
          WP_TYPE_OBJECT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROXY,
      g_param_spec_pointer ("proxy", "proxy",
          "The underlying struct pw_proxy *",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_DESTROYED] = g_signal_new ("destroyed",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
  signals[SIGNAL_CHANGED] = g_signal_new ("changed",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
}

static const gchar *
wp_proxy_pw_properties_get (WpPipewireProperties * p, const gchar * property)
{
  WpProxy * self = WP_PROXY (p);
  return pw_properties_get (self->properties, property);
}

static const struct spa_dict *
wp_proxy_pw_properties_get_as_spa_dict (WpPipewireProperties * p)
{
  WpProxy * self = WP_PROXY (p);
  return &self->properties->dict;
}

static void
wp_proxy_pw_properties_init (WpPipewirePropertiesInterface * iface)
{
  iface->get = wp_proxy_pw_properties_get;
  iface->get_as_spa_dict = wp_proxy_pw_properties_get_as_spa_dict;
}

/**
 * wp_proxy_get_id: (method)
 * @self: the proxy
 *
 * Returns: the global ID of the remote object
 */
guint32
wp_proxy_get_id (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), -1);
  return self->id;
}

/**
 * wp_proxy_get_parent_id: (method)
 * @self: the proxy
 *
 * Returns: the global ID of the parent remote object
 */
guint32
wp_proxy_get_parent_id (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), -1);
  return self->parent_id;
}

/**
 * wp_proxy_get_spa_type: (method)
 * @self: the proxy
 *
 * Returns: the SPA type of the remote object
 */
guint32
wp_proxy_get_spa_type (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), -1);
  return self->type;
}

/**
 * wp_proxy_get_spa_type_string: (method)
 * @self: the proxy
 *
 * Returns: (transfer none): the string that describes the SPA type of
 *    the remote object
 */
const gchar *
wp_proxy_get_spa_type_string (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);
  return self->type_string;
}

/**
 * wp_proxy_get_core: (method)
 * @self: the proxy
 *
 * Returns: (transfer full): the core #WpObject
 */
WpObject *
wp_proxy_get_core (WpProxy *self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);
  return g_object_ref (self->core);
}

/**
 * wp_proxy_is_destroyed: (method)
 * @self: the proxy
 *
 * Returns: TRUE if the proxy has been destroyed
 */
gboolean
wp_proxy_is_destroyed (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), TRUE);
  return (self->proxy == NULL);
}

/**
 * wp_proxy_get_pw_proxy: (skip)
 * @self: the proxy
 *
 * Returns: the pw_proxy pointer or %NULL if the proxy is destroyed
 */
struct pw_proxy *
wp_proxy_get_pw_proxy (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);
  return self->proxy;
}

gconstpointer
wp_proxy_get_info_native (WpProxy * self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);
  return self->info;
}

static void
task_data_free (struct task_data * data)
{
  g_ptr_array_unref (data->result);
  g_slice_free (struct task_data, data);
}

void
wp_proxy_enum_params (WpProxy * self, guint32 id,
    GAsyncReadyCallback callback, gpointer user_data)
{
  g_autoptr (GTask) task = NULL;
  int seq;
  struct task_data *data;

  g_return_if_fail (WP_IS_PROXY (self));
  g_return_if_fail (callback != NULL);

  task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (task, wp_proxy_enum_params);

  data = g_slice_new0 (struct task_data);
  data->seq = seq = global_seq++;
  data->result = g_ptr_array_new_with_free_func (free);
  g_task_set_task_data (task, data, (GDestroyNotify) task_data_free);

  self->tasks = g_list_append (self->tasks, task);

  switch (self->type) {
    case PW_TYPE_INTERFACE_Node:
      pw_node_proxy_enum_params ((struct pw_node_proxy *) self->proxy,
          seq, id, 0, -1, NULL);
      pw_proxy_sync (self->proxy, seq);
      break;
    case PW_TYPE_INTERFACE_Port:
      pw_port_proxy_enum_params ((struct pw_port_proxy *) self->proxy,
          seq, id, 0, -1, NULL);
      pw_proxy_sync (self->proxy, seq);
      break;
    case PW_TYPE_INTERFACE_Device:
      pw_device_proxy_enum_params ((struct pw_device_proxy *) self->proxy,
          seq, id, 0, -1, NULL);
      pw_proxy_sync (self->proxy, seq);
      break;
    default:
      g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
          WP_LIBRARY_ERROR_INVARIANT,
          "Proxy interface does not have an enum_params method");
      break;
  }
}

/**
 * wp_proxy_enum_params_finish:
 *
 * Returns: (transfer full) (element-type spa_pod*): the params
 */
GPtrArray *
wp_proxy_enum_params_finish (WpProxy * self,
    GAsyncResult * res, GError ** err)
{
  g_return_val_if_fail (g_task_is_valid (res, self), NULL);
  g_return_val_if_fail (g_async_result_is_tagged (res, wp_proxy_enum_params),
      NULL);

  self->tasks = g_list_remove (self->tasks, res);

  return g_task_propagate_pointer (G_TASK (res), err);
}
