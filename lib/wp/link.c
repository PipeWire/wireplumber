/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: link
 * @title: PipeWire Link
 */

#define G_LOG_DOMAIN "wp-link"

#include "link.h"
#include "private/pipewire-object-mixin.h"

struct _WpLink
{
  WpGlobalProxy parent;
  struct pw_link_info *info;
  struct spa_hook listener;
};

static void wp_link_pipewire_object_interface_init (WpPipewireObjectInterface * iface);

/**
 * WpLink:
 *
 * The #WpLink class allows accessing the properties and methods of a
 * PipeWire link object (`struct pw_link`).
 *
 * A #WpLink is constructed internally when a new link appears on the
 * PipeWire registry and it is made available through the #WpObjectManager API.
 * Alternatively, a #WpLink can also be constructed using
 * wp_link_new_from_factory(), which creates a new link object
 * on the remote PipeWire server by calling into a factory.
 */
G_DEFINE_TYPE_WITH_CODE (WpLink, wp_link, WP_TYPE_GLOBAL_PROXY,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT, wp_link_pipewire_object_interface_init));

static void
wp_link_init (WpLink * self)
{
}

static WpObjectFeatures
wp_link_get_supported_features (WpObject * object)
{
  return WP_PROXY_FEATURE_BOUND | WP_PIPEWIRE_OBJECT_FEATURE_INFO;
}

static void
wp_link_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO:
    /* just wait, info will be emitted anyway after binding */
    break;
  default:
    WP_OBJECT_CLASS (wp_link_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static void
link_event_info(void *data, const struct pw_link_info *info)
{
  WpLink *self = WP_LINK (data);

  self->info = pw_link_info_update (self->info, info);
  wp_object_update_features (WP_OBJECT (self),
      WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  wp_pipewire_object_mixin_handle_event_info (self, info,
      PW_LINK_CHANGE_MASK_PROPS, 0);
}

static const struct pw_link_events link_events = {
  PW_VERSION_LINK_EVENTS,
  .info = link_event_info,
};

static void
wp_link_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpLink *self = WP_LINK (proxy);
  pw_link_add_listener ((struct pw_link *) pw_proxy,
      &self->listener, &link_events, self);
}

static void
wp_link_pw_proxy_destroyed (WpProxy * proxy)
{
  g_clear_pointer (&WP_LINK (proxy)->info, pw_link_info_free);
  wp_object_update_features (WP_OBJECT (proxy), 0,
      WP_PIPEWIRE_OBJECT_FEATURE_INFO);
}

static void
wp_link_class_init (WpLinkClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_pipewire_object_mixin_get_property;

  wpobject_class->get_supported_features = wp_link_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_pipewire_object_mixin_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_link_activate_execute_step;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Link;
  proxy_class->pw_iface_version = PW_VERSION_LINK;
  proxy_class->pw_proxy_created = wp_link_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_link_pw_proxy_destroyed;

  wp_pipewire_object_mixin_class_override_properties (object_class);
}

static gconstpointer
wp_link_get_native_info (WpPipewireObject * obj)
{
  return WP_LINK (obj)->info;
}

static WpProperties *
wp_link_get_properties (WpPipewireObject * obj)
{
  return wp_properties_new_wrap_dict (WP_LINK (obj)->info->props);
}

static GVariant *
wp_link_get_param_info (WpPipewireObject * obj)
{
  return NULL;
}

static void
wp_link_pipewire_object_interface_init (WpPipewireObjectInterface * iface)
{
  iface->get_native_info = wp_link_get_native_info;
  iface->get_properties = wp_link_get_properties;
  iface->get_param_info = wp_link_get_param_info;
  iface->enum_params = wp_pipewire_object_mixin_enum_params_unimplemented;
  iface->enum_params_finish = wp_pipewire_object_mixin_enum_params_finish;
  iface->enum_cached_params = wp_pipewire_object_mixin_enum_cached_params;
  iface->set_param = wp_pipewire_object_mixin_set_param_unimplemented;
}

/**
 * wp_link_new_from_factory:
 * @core: the wireplumber core
 * @factory_name: the pipewire factory name to construct the link
 * @properties: (nullable) (transfer full): the properties to pass to the factory
 *
 * Constructs a link on the PipeWire server by asking the remote factory
 * @factory_name to create it.
 *
 * Because of the nature of the PipeWire protocol, this operation completes
 * asynchronously at some point in the future. In order to find out when
 * this is done, you should call wp_object_activate(), requesting at least
 * %WP_PROXY_FEATURE_BOUND. When this feature is ready, the link is ready for
 * use on the server. If the link cannot be created, this activation operation
 * will fail.
 *
 * Returns: (nullable) (transfer full): the new link or %NULL if the core
 *   is not connected and therefore the link cannot be created
 */
WpLink *
wp_link_new_from_factory (WpCore * core,
    const gchar * factory_name, WpProperties * properties)
{
  g_autoptr (WpProperties) props = properties;
  WpLink *self = NULL;
  struct pw_core *pw_core = wp_core_get_pw_core (core);

  if (G_UNLIKELY (!pw_core)) {
    g_critical ("The WirePlumber core is not connected; link cannot be created");
    return NULL;
  }

  self = g_object_new (WP_TYPE_LINK, "core", core, NULL);
  wp_proxy_set_pw_proxy (WP_PROXY (self), pw_core_create_object (pw_core,
          factory_name, PW_TYPE_INTERFACE_Link, PW_VERSION_LINK,
          props ? wp_properties_peek_dict (props) : NULL, 0));
  return self;
}

/**
 * wp_link_get_linked_object_ids:
 * @self: the link
 * @output_node: (out) (optional): the bound id of the output (source) node
 * @output_port: (out) (optional): the bound id of the output (source) port
 * @input_node: (out) (optional): the bound id of the input (sink) node
 * @input_port: (out) (optional): the bound id of the input (sink) port
 *
 * Retrieves the ids of the objects that are linked by this link
 *
 * Note: Using this method requires %WP_PROXY_FEATURE_INFO
 */
void
wp_link_get_linked_object_ids (WpLink * self,
    guint32 * output_node, guint32 * output_port,
    guint32 * input_node, guint32 * input_port)
{
  g_return_if_fail (WP_IS_LINK (self));

  if (output_node)
    *output_node = self->info->output_node_id;
  if (output_port)
    *output_port = self->info->output_port_id;
  if (input_node)
    *input_node = self->info->input_node_id;
  if (input_port)
    *input_port = self->info->input_port_id;
}
