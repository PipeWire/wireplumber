/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "port.h"
#include "log.h"
#include "private/pipewire-object-mixin.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-port")

/*! \defgroup wpport WpPort */
/*!
 * \struct WpPort
 *
 * The WpPort class allows accessing the properties
 * and methods of a PipeWire port object (`struct pw_port`).
 *
 * A WpPort is constructed internally when a new port appears
 * on the PipeWire registry and it is made available through the
 * WpObjectManager API.
 */

struct _WpPort
{
  WpGlobalProxy parent;
};

static void wp_port_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpPort, wp_port, WP_TYPE_GLOBAL_PROXY,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT,
        wp_pw_object_mixin_object_interface_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_port_pw_object_mixin_priv_interface_init))

static void
wp_port_init (WpPort * self)
{
}

static void
wp_port_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PW_OBJECT_MIXIN_STEP_BIND:
  case WP_TRANSITION_STEP_ERROR:
    /* base class can handle BIND and ERROR */
    WP_OBJECT_CLASS (wp_port_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  case WP_PW_OBJECT_MIXIN_STEP_WAIT_INFO:
    /* just wait, info will be emitted anyway after binding */
    break;
  case WP_PW_OBJECT_MIXIN_STEP_CACHE_PARAMS:
    wp_pw_object_mixin_cache_params (object, missing);
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_port_deactivate (WpObject * object, WpObjectFeatures features)
{
  wp_pw_object_mixin_deactivate (object, features);
  WP_OBJECT_CLASS (wp_port_parent_class)->deactivate (object, features);
}

static const struct pw_port_events port_events = {
  PW_VERSION_PORT_EVENTS,
  .info = (HandleEventInfoFunc(port)) wp_pw_object_mixin_handle_event_info,
  .param = wp_pw_object_mixin_handle_event_param,
};

static void
wp_port_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_created (proxy, pw_proxy,
      port, &port_events);
}

static void
wp_port_pw_proxy_destroyed (WpProxy * proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_destroyed (proxy);

  WP_PROXY_CLASS (wp_port_parent_class)->pw_proxy_destroyed (proxy);
}

static void
wp_port_class_init (WpPortClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_pw_object_mixin_get_property;

  wpobject_class->get_supported_features =
      wp_pw_object_mixin_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_pw_object_mixin_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_port_activate_execute_step;
  wpobject_class->deactivate = wp_port_deactivate;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Port;
  proxy_class->pw_iface_version = PW_VERSION_PORT;
  proxy_class->pw_proxy_created = wp_port_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_port_pw_proxy_destroyed;

  wp_pw_object_mixin_class_override_properties (object_class);
}

static gint
wp_port_enum_params (gpointer instance, guint32 id,
    guint32 start, guint32 num, WpSpaPod *filter)
{
  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (instance);
  return pw_port_enum_params (d->iface, 0, id, start, num,
      filter ? wp_spa_pod_get_spa_pod (filter) : NULL);
}

static void
wp_port_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  wp_pw_object_mixin_priv_interface_info_init (iface, port, PORT);
  iface->enum_params = wp_port_enum_params;
}

/*!
 * \brief Gets the current direction of the port
 * \remarks Requires WP_PIPEWIRE_OBJECT_FEATURE_INFO
 * \ingroup wpport
 * \param self the port
 * \returns the current direction of the port
 */
WpDirection
wp_port_get_direction (WpPort * self)
{
  g_return_val_if_fail (WP_IS_PORT (self), 0);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  WpPwObjectMixinData *d = wp_pw_object_mixin_get_data (self);
  const struct pw_port_info *info = d->info;

  return (WpDirection) info->direction;
}
