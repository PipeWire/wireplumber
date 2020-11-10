/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: port
 * @title: PipeWire Port
 */

#define G_LOG_DOMAIN "wp-port"

#include "port.h"
#include "private/pipewire-object-mixin.h"

struct _WpPort
{
  WpGlobalProxy parent;
  struct pw_port_info *info;
  struct spa_hook listener;
};

static void wp_port_pipewire_object_interface_init (WpPipewireObjectInterface * iface);

/**
 * WpPort:
 *
 * The #WpPort class allows accessing the properties and methods of a
 * PipeWire port object (`struct pw_port`).
 *
 * A #WpPort is constructed internally when a new port appears on the
 * PipeWire registry and it is made available through the #WpObjectManager API.
 */
G_DEFINE_TYPE_WITH_CODE (WpPort, wp_port, WP_TYPE_GLOBAL_PROXY,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT, wp_port_pipewire_object_interface_init));

static void
wp_port_init (WpPort * self)
{
}

static WpObjectFeatures
wp_port_get_supported_features (WpObject * object)
{
  WpPort *self = WP_PORT (object);

  return WP_PROXY_FEATURE_BOUND | WP_PIPEWIRE_OBJECT_FEATURE_INFO |
      wp_pipewire_object_mixin_param_info_to_features (
          self->info ? self->info->params : NULL,
          self->info ? self->info->n_params : 0);
}

static void
wp_port_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PIPEWIRE_OBJECT_MIXIN_STEP_CACHE_INFO:
    wp_pipewire_object_mixin_cache_info (object, transition);
    break;
  default:
    WP_OBJECT_CLASS (wp_port_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  }
}

static void
wp_port_deactivate (WpObject * object, WpObjectFeatures features)
{
  wp_pipewire_object_mixin_deactivate (object, features);

  WP_OBJECT_CLASS (wp_port_parent_class)->deactivate (object, features);
}

static void
port_event_info(void *data, const struct pw_port_info *info)
{
  WpPort *self = WP_PORT (data);

  self->info = pw_port_info_update (self->info, info);
  wp_object_update_features (WP_OBJECT (self),
      WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  wp_pipewire_object_mixin_handle_event_info (self, info,
      PW_PORT_CHANGE_MASK_PROPS, PW_PORT_CHANGE_MASK_PARAMS);
}

static const struct pw_port_events port_events = {
  PW_VERSION_PORT_EVENTS,
  .info = port_event_info,
  .param = wp_pipewire_object_mixin_handle_event_param,
};

static void
wp_port_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  WpPort *self = WP_PORT (proxy);
  pw_port_add_listener ((struct pw_port *) pw_proxy,
      &self->listener, &port_events, self);
}

static void
wp_port_pw_proxy_destroyed (WpProxy * proxy)
{
  g_clear_pointer (&WP_PORT (proxy)->info, pw_port_info_free);
  wp_object_update_features (WP_OBJECT (proxy), 0,
      WP_PIPEWIRE_OBJECT_FEATURE_INFO);

  wp_pipewire_object_mixin_deactivate (WP_OBJECT (proxy),
      WP_OBJECT_FEATURES_ALL);
}

static void
wp_port_class_init (WpPortClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_pipewire_object_mixin_get_property;

  wpobject_class->get_supported_features = wp_port_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_pipewire_object_mixin_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_port_activate_execute_step;
  wpobject_class->deactivate = wp_port_deactivate;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Port;
  proxy_class->pw_iface_version = PW_VERSION_PORT;
  proxy_class->pw_proxy_created = wp_port_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_port_pw_proxy_destroyed;

  wp_pipewire_object_mixin_class_override_properties (object_class);
}

static gconstpointer
wp_port_get_native_info (WpPipewireObject * obj)
{
  return WP_PORT (obj)->info;
}

static WpProperties *
wp_port_get_properties (WpPipewireObject * obj)
{
  return wp_properties_new_wrap_dict (WP_PORT (obj)->info->props);
}

static GVariant *
wp_port_get_param_info (WpPipewireObject * obj)
{
  WpPort *self = WP_PORT (obj);
  return wp_pipewire_object_mixin_param_info_to_gvariant (self->info->params,
      self->info->n_params);
}

static void
wp_port_enum_params (WpPipewireObject * obj, const gchar * id,
    WpSpaPod *filter, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  wp_pipewire_object_mixin_enum_params (pw_port, obj, id, filter, cancellable,
      callback, user_data);
}

static void
wp_port_pipewire_object_interface_init (WpPipewireObjectInterface * iface)
{
  iface->get_native_info = wp_port_get_native_info;
  iface->get_properties = wp_port_get_properties;
  iface->get_param_info = wp_port_get_param_info;
  iface->enum_params = wp_port_enum_params;
  iface->enum_params_finish = wp_pipewire_object_mixin_enum_params_finish;
  iface->enum_cached_params = wp_pipewire_object_mixin_enum_cached_params;
  iface->set_param = wp_pipewire_object_mixin_set_param_unimplemented;
}

WpDirection
wp_port_get_direction (WpPort * self)
{
  g_return_val_if_fail (WP_IS_PORT (self), 0);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_PIPEWIRE_OBJECT_FEATURE_INFO, 0);

  return (WpDirection) self->info->direction;
}
