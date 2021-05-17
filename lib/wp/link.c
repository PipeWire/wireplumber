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
};

static void wp_link_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

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
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT,
        wp_pw_object_mixin_object_interface_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_link_pw_object_mixin_priv_interface_init))

static void
wp_link_init (WpLink * self)
{
}

static void
wp_link_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PW_OBJECT_MIXIN_STEP_BIND:
  case WP_TRANSITION_STEP_ERROR:
    /* base class can handle BIND and ERROR */
    WP_OBJECT_CLASS (wp_link_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  case WP_PW_OBJECT_MIXIN_STEP_WAIT_INFO:
    /* just wait, info will be emitted anyway after binding */
    break;
  default:
    g_assert_not_reached ();
  }
}

static const struct pw_link_events link_events = {
  PW_VERSION_LINK_EVENTS,
  .info = (HandleEventInfoFunc(link)) wp_pw_object_mixin_handle_event_info,
};

static void
wp_link_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_created (proxy, pw_proxy,
      link, &link_events);
}

static void
wp_link_pw_proxy_destroyed (WpProxy * proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_destroyed (proxy);

  WP_PROXY_CLASS (wp_link_parent_class)->pw_proxy_destroyed (proxy);
}

static void
wp_link_class_init (WpLinkClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_pw_object_mixin_get_property;

  wpobject_class->get_supported_features =
      wp_pw_object_mixin_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_pw_object_mixin_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_link_activate_execute_step;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Link;
  proxy_class->pw_iface_version = PW_VERSION_LINK;
  proxy_class->pw_proxy_created = wp_link_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_link_pw_proxy_destroyed;

  wp_pw_object_mixin_class_override_properties (object_class);
}

static void
wp_link_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  wp_pw_object_mixin_priv_interface_info_init_no_params (iface, link, LINK);
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
  return g_object_new (WP_TYPE_LINK,
      "core", core,
      "factory-name", factory_name,
      "global-properties", props,
      NULL);
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
 * Note: Using this method requires %WP_PIPEWIRE_OBJECT_FEATURE_INFO
 */
void
wp_link_get_linked_object_ids (WpLink * self,
    guint32 * output_node, guint32 * output_port,
    guint32 * input_node, guint32 * input_port)
{
  g_return_if_fail (WP_IS_LINK (self));

  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);
  struct pw_link_info *info = d->info;
  g_return_if_fail (info);

  if (output_node)
    *output_node = info->output_node_id;
  if (output_port)
    *output_port = info->output_port_id;
  if (input_node)
    *input_node = info->input_node_id;
  if (input_port)
    *input_port = info->input_port_id;
}
