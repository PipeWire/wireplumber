/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include "context.h"
#include "endpoint-fake.h"
#include "endpoint-link-fake.h"
#include "../../../modules/module-config-policy/parser-endpoint-link.h"
#include "../../../modules/module-config-policy/config-policy.h"

struct _WpConfigPolicyContext
{
  GObject parent;

  /* Props */
  GWeakRef core;
  char *config_path;

  GMutex mutex;
  GCond cond;
  WpEndpoint *endpoint;
  WpEndpointLink *link;
};

enum {
  PROP_0,
  PROP_CORE,
  PROP_CONFIG_PATH,
};

G_DEFINE_TYPE (WpConfigPolicyContext, wp_config_policy_context, G_TYPE_OBJECT);

static WpEndpoint *
wait_for_endpoint (WpConfigPolicyContext *self, WpEndpointLink **link)
{
  g_mutex_lock (&self->mutex);

  /* Wait for endpoint to be set */
  while (!self->endpoint)
    g_cond_wait (&self->cond, &self->mutex);

  /* Set endpoint to a local value and clear global value */
  WpEndpoint *endpoint = g_object_ref (self->endpoint);
  g_clear_object (&self->endpoint);

  /* Set link to a local value and clear global value */
  if (link)
    *link = self->link ? g_object_ref (self->link) : NULL;
  g_clear_object (&self->link);

  g_mutex_unlock (&self->mutex);

  return endpoint;
}

static void
on_done (WpConfigPolicy *cp, WpEndpoint *ep, WpEndpointLink *link,
    WpConfigPolicyContext *self)
{
  if (!ep)
    return;

  g_mutex_lock (&self->mutex);

  self->endpoint = g_object_ref (ep);
  self->link = link ? g_object_ref (link) : NULL;
  g_cond_signal (&self->cond);

  g_mutex_unlock (&self->mutex);
}

static void
wp_config_policy_context_constructed (GObject *object)
{
  WpConfigPolicyContext *self = WP_CONFIG_POLICY_CONTEXT (object);
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_return_if_fail (core);

  /* Register the endpoint link fake factory */
  wp_factory_new (core, WP_ENDPOINT_LINK_FAKE_FACTORY_NAME,
      wp_endpoint_link_fake_factory);

  /* Set the configuration path */
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  wp_configuration_add_path (config, self->config_path);

  /* Register the config policy */
  g_autoptr (WpConfigPolicy) cp = wp_config_policy_new (config);
  wp_policy_register (WP_POLICY (cp), core);

  /* Handle done and link-created signals */
  g_signal_connect (cp, "done", (GCallback) on_done, self);

  G_OBJECT_CLASS (wp_config_policy_context_parent_class)->constructed (object);
}

static void
wp_config_policy_context_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpConfigPolicyContext *self = WP_CONFIG_POLICY_CONTEXT (object);

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&self->core, g_value_get_object (value));
    break;
  case PROP_CONFIG_PATH:
    self->config_path = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_config_policy_context_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpConfigPolicyContext *self = WP_CONFIG_POLICY_CONTEXT (object);

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&self->core));
    break;
  case PROP_CONFIG_PATH:
    g_value_set_string (value, self->config_path);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_config_policy_context_finalize (GObject * object)
{
  WpConfigPolicyContext *self = WP_CONFIG_POLICY_CONTEXT (object);

  g_mutex_clear (&self->mutex);
  g_cond_clear (&self->cond);

  g_weak_ref_clear (&self->core);

  G_OBJECT_CLASS (wp_config_policy_context_parent_class)->finalize (object);
}

static void
wp_config_policy_context_init (WpConfigPolicyContext * self)
{
  g_weak_ref_init (&self->core, NULL);

  g_mutex_init (&self->mutex);
  g_cond_init (&self->cond);
}

static void
wp_config_policy_context_class_init (WpConfigPolicyContextClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_config_policy_context_constructed;
  object_class->finalize = wp_config_policy_context_finalize;
  object_class->set_property = wp_config_policy_context_set_property;
  object_class->get_property = wp_config_policy_context_get_property;

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The wireplumber core", WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CONFIG_PATH,
      g_param_spec_string ("config-path", "config-path",
          "The config-path of the context", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpConfigPolicyContext *
wp_config_policy_context_new (WpCore *core, const char *config_path)
{
  return g_object_new (wp_config_policy_context_get_type (),
    "core", core,
    "config-path", config_path,
    NULL);
}

static void
on_endpoint_created (GObject *initable, GAsyncResult *res, gpointer d)
{
  g_autoptr (WpEndpoint) ep = NULL;
  GError *error = NULL;

  ep = wp_endpoint_new_finish (initable, res, &error);
  g_return_if_fail (!error);
  g_return_if_fail (ep);

  /* Register the endpoint */
  wp_endpoint_register (ep);
}

WpEndpoint *
wp_config_policy_context_add_endpoint (WpConfigPolicyContext *self,
    const char *name, const char *media_class, guint direction,
    WpProperties *props, const char *role, guint streams, WpEndpointLink **link)
{
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_return_val_if_fail (core, NULL);

  wp_endpoint_fake_new_async (core, name, media_class, direction, props, role,
      streams, on_endpoint_created, self);

  return wait_for_endpoint (self, link);
}

void
wp_config_policy_context_remove_endpoint (WpConfigPolicyContext *self,
    WpEndpoint *ep)
{
  g_return_if_fail (ep);

  wp_endpoint_unregister (ep);

  wait_for_endpoint (self, NULL);
}
