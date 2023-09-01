/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "factory.h"
#include "log.h"
#include "private/pipewire-object-mixin.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-factory")

/*! \defgroup wpfactory WpFactory */
/*!
 * \struct WpFactory
 *
 * The WpFactory class allows accessing the properties and methods of
 * PipeWire Factory objects (`struct pw_factory`).
 *
 * A WpFactory is constructed internally by wireplumber, when the pipewire 
 * constructed factory objects are reported in by PipeWire registry 
 * and it is made available for wireplumber clients through the 
 * WpObjectManager API.
 *
 * \since 0.4.5
 */

struct _WpFactory
{
  WpGlobalProxy parent;
};

static void wp_factory_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);


G_DEFINE_TYPE_WITH_CODE (WpFactory, wp_factory, WP_TYPE_GLOBAL_PROXY,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT,
        wp_pw_object_mixin_object_interface_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_factory_pw_object_mixin_priv_interface_init))

static void wp_factory_init (WpFactory * self)
{
}

static void
wp_factory_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PW_OBJECT_MIXIN_STEP_BIND:
  case WP_TRANSITION_STEP_ERROR:
    /* base class can handle BIND and ERROR */
    WP_OBJECT_CLASS (wp_factory_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  case WP_PW_OBJECT_MIXIN_STEP_WAIT_INFO:
    /* just wait, info will be emitted anyway after binding */
    break;
  default:
    g_assert_not_reached ();
  }
}

static const struct pw_factory_events factory_events = {
  PW_VERSION_FACTORY_EVENTS,
  .info = (HandleEventInfoFunc(factory)) wp_pw_object_mixin_handle_event_info,
};

static void
wp_factory_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_created (proxy, pw_proxy,
      factory, &factory_events);
}

static void
wp_factory_pw_proxy_destroyed (WpProxy * proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_destroyed (proxy);

  WP_PROXY_CLASS (wp_factory_parent_class)->pw_proxy_destroyed (proxy);
}

static void
wp_factory_class_init (WpFactoryClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_pw_object_mixin_get_property;

  wpobject_class->get_supported_features =
      wp_pw_object_mixin_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_pw_object_mixin_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_factory_activate_execute_step;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Factory;
  proxy_class->pw_iface_version = PW_VERSION_FACTORY;
  proxy_class->pw_proxy_created = wp_factory_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_factory_pw_proxy_destroyed;

  wp_pw_object_mixin_class_override_properties (object_class);
}

static void
wp_factory_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  wp_pw_object_mixin_priv_interface_info_init_no_params (iface, factory, FACTORY);
}
