/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: client
 * @title: PipeWire Client
 */

#define G_LOG_DOMAIN "wp-client"

#include "client.h"
#include "private/pipewire-object-mixin.h"

struct _WpClient
{
  WpGlobalProxy parent;
};

static void wp_client_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface);

/**
 * WpClient:
 *
 * The #WpClient class allows accessing the properties and methods of a PipeWire
 * client object (`struct pw_client`). A #WpClient is constructed internally
 * when a new client connects to PipeWire and it is made available through the
 * #WpObjectManager API.
 */
G_DEFINE_TYPE_WITH_CODE (WpClient, wp_client, WP_TYPE_GLOBAL_PROXY,
    G_IMPLEMENT_INTERFACE (WP_TYPE_PIPEWIRE_OBJECT,
        wp_pw_object_mixin_object_interface_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_PW_OBJECT_MIXIN_PRIV,
        wp_client_pw_object_mixin_priv_interface_init))

static void
wp_client_init (WpClient * self)
{
}

static void
wp_client_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
  case WP_PW_OBJECT_MIXIN_STEP_BIND:
  case WP_TRANSITION_STEP_ERROR:
    /* base class can handle BIND and ERROR */
    WP_OBJECT_CLASS (wp_client_parent_class)->
        activate_execute_step (object, transition, step, missing);
    break;
  case WP_PW_OBJECT_MIXIN_STEP_WAIT_INFO:
    /* just wait, info will be emitted anyway after binding */
    break;
  default:
    g_assert_not_reached ();
  }
}

static const struct pw_client_events client_events = {
  PW_VERSION_CLIENT_EVENTS,
  .info = (HandleEventInfoFunc(client)) wp_pw_object_mixin_handle_event_info,
};

static void
wp_client_pw_proxy_created (WpProxy * proxy, struct pw_proxy * pw_proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_created (proxy, pw_proxy,
      client, &client_events);
}

static void
wp_client_pw_proxy_destroyed (WpProxy * proxy)
{
  wp_pw_object_mixin_handle_pw_proxy_destroyed (proxy);

  WP_PROXY_CLASS (wp_client_parent_class)->pw_proxy_destroyed (proxy);
}

static void
wp_client_class_init (WpClientClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;
  WpProxyClass *proxy_class = (WpProxyClass *) klass;

  object_class->get_property = wp_pw_object_mixin_get_property;

  wpobject_class->get_supported_features =
      wp_pw_object_mixin_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_pw_object_mixin_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_client_activate_execute_step;

  proxy_class->pw_iface_type = PW_TYPE_INTERFACE_Client;
  proxy_class->pw_iface_version = PW_VERSION_CLIENT;
  proxy_class->pw_proxy_created = wp_client_pw_proxy_created;
  proxy_class->pw_proxy_destroyed = wp_client_pw_proxy_destroyed;

  wp_pw_object_mixin_class_override_properties (object_class);
}

static void
wp_client_pw_object_mixin_priv_interface_init (
    WpPwObjectMixinPrivInterface * iface)
{
  wp_pw_object_mixin_priv_interface_info_init_no_params (iface, client, CLIENT);
}

/**
 * wp_client_update_permissions:
 * @self: the client
 * @n_perm: the number of permissions specified in the variable arguments
 * @...: @n_perm pairs of #guint32 numbers; the first number is the object id
 *   and the second is the permissions that this client should have
 *   on this object
 *
 * Update client's permissions on a list of objects. An object id of `-1`
 * can be used to set the default object permissions for this client
 */
void
wp_client_update_permissions (WpClient * self, guint n_perm, ...)
{
  va_list args;
  struct pw_permission *perm =
      g_alloca (n_perm * sizeof (struct pw_permission));

  va_start (args, n_perm);
  for (guint i = 0; i < n_perm; i++) {
    perm[i].id = va_arg (args, guint32);
    perm[i].permissions = va_arg (args, guint32);
  }
  va_end (args);

  wp_client_update_permissions_array (self, n_perm, perm);
}

/**
 * wp_client_update_permissions_array:
 * @self: the client
 * @n_perm: the number of permissions specified in the @permissions array
 * @permissions: (array length=n_perm) (element-type pw_permission): an array
 *    of permissions per object id
 *
 * Update client's permissions on a list of objects. An object id of `-1`
 * can be used to set the default object permissions for this client
 */
void
wp_client_update_permissions_array (WpClient * self,
    guint n_perm, const struct pw_permission *permissions)
{
  struct pw_client *pwp;
  int client_update_permissions_result;

  g_return_if_fail (WP_IS_CLIENT (self));

  pwp = (struct pw_client *) wp_proxy_get_pw_proxy (WP_PROXY (self));
  g_return_if_fail (pwp != NULL);

  client_update_permissions_result = pw_client_update_permissions (
      pwp, n_perm, permissions);
  g_warn_if_fail (client_update_permissions_result >= 0);
}
