/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "proxy.h"
#include <pipewire/pipewire.h>
#include <spa/debug/types.h>

struct _WpProxy
{
  GObject parent;

  WpProxyRegistry *registry;

  struct pw_proxy *proxy;
  guint32 id;
  guint32 parent_id;
  guint32 type;
  const gchar *type_string;

  struct spa_hook proxy_listener;
  struct spa_hook proxy_proxy_listener;

  union {
    const struct spa_dict *initial_properties;
    GHashTable *properties;
  };
};

enum {
  PROP_0,
  PROP_ID,
  PROP_PARENT_ID,
  PROP_SPA_TYPE,
  PROP_SPA_TYPE_STRING,
  PROP_INITIAL_PROPERTIES,
  PROP_REGISTRY,
  PROP_PROXY,
};

enum {
  SIGNAL_DESTROYED,
  SIGNAL_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (WpProxy, wp_proxy, wp_object_get_type ());

static void
spa_dict_to_hashtable (const struct spa_dict * dict, GHashTable * htable)
{
  const struct spa_dict_item *item;
  spa_dict_for_each (item, dict) {
    g_hash_table_insert (htable,
        GUINT_TO_POINTER (g_quark_from_string (item->key)),
        g_strdup (item->value));
  }
}

#define UPDATE_PROP(name, lvalue) \
  { \
    static GQuark _quark = 0; \
    if (!_quark) \
      g_quark_from_static_string (name); \
    g_hash_table_insert (self->properties, GUINT_TO_POINTER (_quark), lvalue); \
  }

#define STATIC_PROP(name, lvalue) \
  { \
    static GQuark _quark = 0; \
    if (!_quark) \
      g_quark_from_static_string (name); \
    if (!g_hash_table_contains (self->properties, GUINT_TO_POINTER (_quark))) { \
      g_hash_table_insert (self->properties, GUINT_TO_POINTER (_quark), lvalue); \
    } \
  }

static void
node_event_info (void *object, const struct pw_node_info *info)
{
  WpProxy *self = WP_PROXY (object);

  if (info->change_mask & PW_NODE_CHANGE_MASK_NAME) {
    UPDATE_PROP ("node-info.name", g_strdup (info->name));
  }
  if (info->change_mask & PW_NODE_CHANGE_MASK_INPUT_PORTS) {
    UPDATE_PROP ("node-info.n-input-ports",
        g_strdup_printf ("%u", info->n_input_ports));
    UPDATE_PROP ("node-info.max-input-ports",
        g_strdup_printf ("%u", info->max_input_ports));
  }
  if (info->change_mask & PW_NODE_CHANGE_MASK_OUTPUT_PORTS) {
    UPDATE_PROP ("node-info.n-output-ports",
        g_strdup_printf ("%u", info->n_output_ports));
    UPDATE_PROP ("node-info.max-output-ports",
        g_strdup_printf ("%u", info->max_output_ports));
  }
  if (info->change_mask & PW_NODE_CHANGE_MASK_STATE) {
    UPDATE_PROP ("node-info.state",
        g_strdup (pw_node_state_as_string (info->state)));
    UPDATE_PROP ("node-info.error", g_strdup (info->error));
  }
  if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS) {
    spa_dict_to_hashtable (info->props, self->properties);
  }
  // TODO: PW_NODE_CHANGE_MASK_PARAMS

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static const struct pw_node_proxy_events node_events = {
  PW_VERSION_NODE_PROXY_EVENTS,
  .info = node_event_info
};

static void
port_event_info (void *object, const struct pw_port_info *info)
{
  WpProxy *self = WP_PROXY (object);

  STATIC_PROP ("port-info.direction",
      g_strdup (pw_direction_as_string (info->direction)));

  if (info->change_mask & PW_PORT_CHANGE_MASK_PROPS) {
    spa_dict_to_hashtable (info->props, self->properties);
  }
  // TODO: PW_PORT_CHANGE_MASK_PARAMS

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static const struct pw_port_proxy_events port_events = {
  PW_VERSION_PORT_PROXY_EVENTS,
  .info = port_event_info
};

static void
link_event_info (void *object, const struct pw_link_info *info)
{
  WpProxy *self = WP_PROXY (object);

  if (info->change_mask & PW_LINK_CHANGE_MASK_OUTPUT) {
    UPDATE_PROP ("link-info.output-node-id",
        g_strdup_printf ("%u", info->output_node_id));
    UPDATE_PROP ("link-info.output-port-id",
        g_strdup_printf ("%u", info->output_port_id));
  }
  if (info->change_mask & PW_LINK_CHANGE_MASK_INPUT) {
    UPDATE_PROP ("link-info.input-node-id",
        g_strdup_printf ("%u", info->input_node_id));
    UPDATE_PROP ("link-info.input-port-id",
        g_strdup_printf ("%u", info->input_port_id));
  }
  if (info->change_mask & PW_LINK_CHANGE_MASK_STATE) {
    UPDATE_PROP ("link-info.state",
        g_strdup (pw_link_state_as_string (info->state)));
    UPDATE_PROP ("link-info.error", g_strdup (info->error));
  }
  //TODO: PW_LINK_CHANGE_MASK_FORMAT

  if (info->change_mask & PW_LINK_CHANGE_MASK_PROPS) {
    spa_dict_to_hashtable (info->props, self->properties);
  }

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static const struct pw_link_proxy_events link_events = {
  PW_VERSION_LINK_PROXY_EVENTS,
  .info = link_event_info
};

static void
client_event_info (void *object, const struct pw_client_info *info)
{
  WpProxy *self = WP_PROXY (object);

  if (info->change_mask & PW_CLIENT_CHANGE_MASK_PROPS) {
    spa_dict_to_hashtable (info->props, self->properties);
  }

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static const struct pw_client_proxy_events client_events = {
  PW_VERSION_CLIENT_PROXY_EVENTS,
  .info = client_event_info
};

static void
device_event_info (void *object, const struct pw_device_info *info)
{
  WpProxy *self = WP_PROXY (object);

  STATIC_PROP ("device-info.name", g_strdup (info->name));

  if (info->change_mask & PW_DEVICE_CHANGE_MASK_PROPS) {
    spa_dict_to_hashtable (info->props, self->properties);
  }
  //TODO: PW_DEVICE_CHANGE_MASK_PARAMS

  g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static const struct pw_device_proxy_events device_events = {
  PW_VERSION_DEVICE_PROXY_EVENTS,
  .info = device_event_info
};

static void
proxy_event_destroy (void *object)
{
  WpProxy *self = WP_PROXY (object);

  g_debug ("proxy %u destroyed", self->id);

  self->proxy = NULL;
  g_signal_emit (self, signals[SIGNAL_DESTROYED], 0);
}

static const struct pw_proxy_events proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_event_destroy
};

static void
wp_proxy_init (WpProxy * self)
{
}

static void
wp_proxy_constructed (GObject * object)
{
  WpProxy *self = WP_PROXY (object);
  GHashTable *properties;
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
    case PW_TYPE_INTERFACE_Link:
      events = &link_events;
      ver = PW_VERSION_LINK;
      break;
    case PW_TYPE_INTERFACE_Client:
      events = &client_events;
      ver = PW_VERSION_CLIENT;
      break;
    case PW_TYPE_INTERFACE_Device:
      events = &device_events;
      ver = PW_VERSION_DEVICE;
      break;
    default:
      break;
  }

  reg_proxy = wp_proxy_registry_get_pw_registry_proxy (self->registry);
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
   * call, so we replace it with a GHashTable
   */
  properties = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_free);
  if (self->initial_properties)
    spa_dict_to_hashtable (self->initial_properties, properties);
  self->properties = properties;

  G_OBJECT_CLASS (wp_proxy_parent_class)->constructed (object);
}

static void
wp_proxy_finalize (GObject * object)
{
  WpProxy *self = WP_PROXY (object);

  g_hash_table_unref (self->properties);
  g_clear_object (&self->registry);

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
  case PROP_REGISTRY:
    self->registry = g_value_get_object (value);
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
  case PROP_REGISTRY:
    g_value_set_object (value, self->registry);
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

  g_object_class_install_property (object_class, PROP_REGISTRY,
      g_param_spec_object ("registry", "registry",
          "The WpProxyRegistry that owns this proxy",
          wp_proxy_registry_get_type (),
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
 * wp_proxy_get_registry: (method)
 * @self: the proxy
 *
 * Returns: (transfer full): the #WpProxyRegistry
 */
WpProxyRegistry *
wp_proxy_get_registry (WpProxy *self)
{
  g_return_val_if_fail (WP_IS_PROXY (self), NULL);
  return g_object_ref (self->registry);
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

/**
 * wp_proxy_get_pw_property: (method)
 * @self: the proxy
 * @property: (transfer none): the name of the property to lookup
 *
 * Returns: (transfer none): the value or %NULL
 */
const gchar *
wp_proxy_get_pw_property (WpProxy * self, const gchar * property)
{
  GQuark quark = 0;

  g_return_val_if_fail (WP_IS_PROXY (self), NULL);
  g_return_val_if_fail (property != NULL, NULL);

  quark = g_quark_try_string (property);
  return quark ?
    g_hash_table_lookup (self->properties, GUINT_TO_POINTER (quark)) : NULL;
}
