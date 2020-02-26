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
  GMainLoop *loop;
  char *config_path;

  WpConfigPolicy *policy;
  GWeakRef last_endpoint;
};

enum {
  PROP_0,
  PROP_CORE,
  PROP_LOOP,
  PROP_CONFIG_PATH,
};

G_DEFINE_TYPE (WpConfigPolicyContext, wp_config_policy_context, G_TYPE_OBJECT);

static void
on_done (WpConfigPolicyContext *self)
{
  g_return_if_fail (self->loop);
  g_main_loop_quit (self->loop);
}

static void
wp_config_policy_context_constructed (GObject *object)
{
  WpConfigPolicyContext *self = WP_CONFIG_POLICY_CONTEXT (object);
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_return_if_fail (core);

  /* Register the endpoint link fake factory */
  wp_factory_new (core, WP_FAKE_ENDPOINT_LINK_FACTORY_NAME,
      wp_fake_endpoint_link_factory);

  /* Set the configuration path */
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  wp_configuration_add_path (config, self->config_path);

  /* Register the config policy */
  self->policy = wp_config_policy_new (config);
  wp_policy_register (WP_POLICY (self->policy), core);

  /* Handle the done signal */
  g_signal_connect_object (self->policy, "done", (GCallback) on_done, self,
      G_CONNECT_SWAPPED);

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
  case PROP_LOOP:
    self->loop = g_value_get_pointer (value);
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
  case PROP_LOOP:
    g_value_set_pointer (value, self->loop);
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

  g_weak_ref_clear (&self->last_endpoint);

  if (self->policy)
    wp_policy_unregister (WP_POLICY (self->policy));
  g_clear_object (&self->policy);

  self->loop = NULL;
  g_weak_ref_clear (&self->core);

  G_OBJECT_CLASS (wp_config_policy_context_parent_class)->finalize (object);
}

static void
wp_config_policy_context_init (WpConfigPolicyContext * self)
{
  g_weak_ref_init (&self->core, NULL);
  self->loop = NULL;

  g_weak_ref_init (&self->last_endpoint, NULL);
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

  g_object_class_install_property (object_class, PROP_LOOP,
      g_param_spec_pointer ("loop", "loop", "The main loop where pipewire runs",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CONFIG_PATH,
      g_param_spec_string ("config-path", "config-path",
          "The config-path of the context", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

WpConfigPolicyContext *
wp_config_policy_context_new (WpCore *core, GMainLoop *loop,
    const char *config_path)
{
  return g_object_new (wp_config_policy_context_get_type (),
    "core", core,
    "loop", loop,
    "config-path", config_path,
    NULL);
}

static void
on_endpoint_created (GObject *initable, GAsyncResult *res, gpointer data)
{
  WpConfigPolicyContext *self = data;
  GError *error = NULL;
  g_autoptr (WpBaseEndpoint) ep = NULL;

  ep = wp_base_endpoint_new_finish (initable, res, &error);
  g_return_if_fail (!error);
  g_return_if_fail (ep);

  /* Update last endpoint weak ref */
  g_weak_ref_set (&self->last_endpoint, ep);

  /* Register the endpoint */
  wp_base_endpoint_register (ep);
}

WpBaseEndpoint *
wp_config_policy_context_add_endpoint (WpConfigPolicyContext *self,
    const char *name, const char *media_class, guint direction,
    WpProperties *props, const char *role, guint streams)
{
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  g_return_val_if_fail (core, NULL);

  wp_fake_endpoint_new_async (core, name, media_class, direction, props, role,
      streams, on_endpoint_created, self);

  g_main_loop_run (self->loop);
  return g_weak_ref_get(&self->last_endpoint);
}

void
wp_config_policy_context_remove_endpoint (WpConfigPolicyContext *self,
    WpBaseEndpoint *ep)
{
  g_return_if_fail (ep);

  wp_base_endpoint_unregister (ep);
  g_main_loop_run (self->loop);
}
