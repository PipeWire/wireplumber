/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "policy.h"
#include "private.h"

/* WpPolicyManager */

struct _WpPolicyManager
{
  GObject parent;
  GList *policies;
};

enum {
  SIGNAL_CHANGED,
  N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE (WpPolicyManager, wp_policy_manager, G_TYPE_OBJECT)

static void
wp_policy_manager_init (WpPolicyManager *self)
{
}

static void
wp_policy_manager_finalize (GObject *object)
{
  WpPolicyManager *self = WP_POLICY_MANAGER (object);

  g_debug ("WpPolicyManager destroyed");

  g_list_free_full (self->policies, g_object_unref);

  G_OBJECT_CLASS (wp_policy_manager_parent_class)->finalize (object);
}

static void
wp_policy_manager_class_init (WpPolicyManagerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_policy_manager_finalize;

  signals[SIGNAL_CHANGED] = g_signal_new ("policy-changed",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
}

static void
policy_mgr_endpoint_added (WpCore *core, GQuark key, WpEndpoint *ep,
    WpPolicyManager *self)
{
  GList *l;
  WpPolicy *p;
  gboolean handled = FALSE;

  for (l = g_list_first (self->policies); l; l = g_list_next (l)) {
    p = WP_POLICY (l->data);

    if (WP_POLICY_GET_CLASS (p)->endpoint_added)
      WP_POLICY_GET_CLASS (p)->endpoint_added (p, ep);

    if (!handled && WP_POLICY_GET_CLASS (p)->handle_endpoint)
      handled = WP_POLICY_GET_CLASS (p)->handle_endpoint (p, ep);
  }
}

static void
policy_mgr_endpoint_removed (WpCore *core, GQuark key, WpEndpoint *ep,
    WpPolicyManager *self)
{
  GList *l;
  WpPolicy *p;

  for (l = g_list_first (self->policies); l; l = g_list_next (l)) {
    p = WP_POLICY (l->data);

    if (WP_POLICY_GET_CLASS (p)->endpoint_removed)
      WP_POLICY_GET_CLASS (p)->endpoint_removed (p, ep);
  }
}

/**
 * wp_policy_manager_get_instance:
 * @core: the #WpCore
 *
 * Returns: (transfer full): the instance of #WpPolicyManager that is
 * registered on the @core
 */
WpPolicyManager *
wp_policy_manager_get_instance (WpCore *core)
{
  WpPolicyManager *mgr;

  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  mgr = wp_core_get_global (core, WP_GLOBAL_POLICY_MANAGER);
  if (G_UNLIKELY (!mgr)) {
    mgr = g_object_new (WP_TYPE_POLICY_MANAGER, NULL);

    g_signal_connect_object (core, "global-added::endpoint",
        (GCallback) policy_mgr_endpoint_added, mgr, 0);
    g_signal_connect_object (core, "global-removed::endpoint",
        (GCallback) policy_mgr_endpoint_removed, mgr, 0);

    wp_core_register_global (core, WP_GLOBAL_POLICY_MANAGER, mgr,
        g_object_unref);
  }

  return g_object_ref (mgr);
}

/* WpPolicy */

/**
 * WpPolicyClass::endpoint_added:
 * @self: the policy
 * @ep: the endpoint
 *
 * Called when a new endpoint has been added.
 * This is only informative, to be used for internal bookeeping purposes.
 * No action should be taken to do something with this endpoint.
 */

/**
 * WpPolicyClass::endpoint_removed:
 * @self: the policy
 * @ep: the endpoint
 *
 * Called when an endpoint has been removed.
 * This is only informative, to be used for internal bookeeping purposes.
 */

/**
 * WpPolicyClass::handle_endpoint:
 * @self: the policy
 * @ep: the endpoint
 *
 * Called when a new endpoint has been added.
 * The policy is meant to decide if this endpoint needs to be linked
 * somewhere and if so, create the link.
 * This will only be called if no other higher-ranked policy has already
 * handled this endpoint.
 *
 * Returns: TRUE if this policy did handle the endpoint, FALSE to let some
 *   lower-ranked policy to try
 */

/**
 * WpPolicyClass::find_endpoint:
 * @self: the policy
 * @props: properties of the lookup
 * @stream_id: (out): the relevant stream id of the returned endpoint
 *
 * Called to locate an endpoint with a specific set of properties,
 * which may be used to implement decision making when multiple endpoints
 * can match.
 *
 * The most notorious use case of this function is to locate a target
 * device endpoint in order to link a client one.
 *
 * @props is expected to be a dictionary (a{sv}) GVariant with keys that
 * describe the situation. Some of these keys can be:
 *  * "action" (s): Currently the value can be "link" or "mixer". "link" is
 *          to find a target for linking a client. "mixer" is to find a target
 *          to modify mixer controls.
 *  * "media.role" (s): the role of the media stream, as defined in pipewire
 *  * "media.class" (s): the media class that the returned endpoint is supposed
 *          to have (policy is free to ignore this)
 *  * "target.properties" (a{sv}): the properties of the other endpoint in case
 *          the action is "link"
 *
 * @stream_id is to be set to the stream id of the returned endpoint that
 * the policy wants to be used for this action.
 *
 * Returns: (transfer full) (nullable): the found endpoint, or NULL
 */

typedef struct _WpPolicyPrivate WpPolicyPrivate;
struct _WpPolicyPrivate
{
  guint32 rank;
  WpCore *core;
};

enum {
  PROP_0,
  PROP_RANK
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpPolicy, wp_policy, G_TYPE_OBJECT)

static void
wp_policy_init (WpPolicy *self)
{
}

static void
wp_policy_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpPolicyPrivate *priv = wp_policy_get_instance_private (WP_POLICY (object));

  switch (property_id) {
  case PROP_RANK:
    priv->rank = g_value_get_uint (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_policy_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpPolicyPrivate *priv = wp_policy_get_instance_private (WP_POLICY (object));

  switch (property_id) {
  case PROP_RANK:
    g_value_set_uint (value, priv->rank);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
wp_policy_class_init (WpPolicyClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->set_property = wp_policy_set_property;
  object_class->get_property = wp_policy_get_property;

  g_object_class_install_property (object_class, PROP_RANK,
      g_param_spec_uint ("rank", "rank", "The rank of the policy",
          0, G_MAXINT32, WP_POLICY_RANK_UPSTREAM,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

guint32
wp_policy_get_rank (WpPolicy *self)
{
  WpPolicyPrivate *priv;

  g_return_val_if_fail (WP_IS_POLICY (self), 0);

  priv = wp_policy_get_instance_private (self);
  return priv->rank;
}

/**
 * wp_policy_get_core:
 * @self: the policy
 *
 * Returns: (transfer full): the core of the policy
 */

WpCore *
wp_policy_get_core (WpPolicy *self)
{
  WpPolicyPrivate *priv;

  g_return_val_if_fail (WP_IS_POLICY (self), NULL);

  priv = wp_policy_get_instance_private (self);
  return priv->core ? g_object_ref (priv->core) : NULL;
}

static gint
compare_ranks (const WpPolicy * a, const WpPolicy * b)
{
  WpPolicyPrivate *a_priv = wp_policy_get_instance_private ((WpPolicy *) a);
  WpPolicyPrivate *b_priv = wp_policy_get_instance_private ((WpPolicy *) b);
  return (gint) b_priv->rank - (gint) a_priv->rank;
}

void
wp_policy_register (WpPolicy *self, WpCore *core)
{
  g_autoptr (WpPolicyManager) mgr = NULL;
  WpPolicyPrivate *priv;

  g_return_if_fail (WP_IS_POLICY (self));
  g_return_if_fail (WP_IS_CORE (core));

  priv = wp_policy_get_instance_private (self);
  priv->core = core;

  mgr = wp_policy_manager_get_instance (core);
  mgr->policies = g_list_insert_sorted (mgr->policies, g_object_ref (self),
      (GCompareFunc) compare_ranks);
  g_signal_emit (mgr, signals[SIGNAL_CHANGED], 0);
}

void
wp_policy_unregister (WpPolicy *self)
{
  WpPolicyManager *mgr;
  WpPolicyPrivate *priv;

  g_return_if_fail (WP_IS_POLICY (self));

  priv = wp_policy_get_instance_private (self);

  if (priv->core) {
    mgr = wp_core_get_global (priv->core, WP_GLOBAL_POLICY_MANAGER);
    if (G_UNLIKELY (!mgr)) {
      g_critical ("WpPolicy:%p seems registered, but the policy manager "
          "is absent", self);
      return;
    }

    mgr->policies = g_list_remove (mgr->policies, self);
    g_signal_emit (mgr, signals[SIGNAL_CHANGED], 0);
    g_object_unref (self);
  }
}

void
wp_policy_notify_changed (WpPolicy *self)
{
  WpPolicyManager *mgr;
  WpPolicyPrivate *priv;

  g_return_if_fail (WP_IS_POLICY (self));

  priv = wp_policy_get_instance_private (self);
  if (priv->core) {
    mgr = wp_core_get_global (priv->core, WP_GLOBAL_POLICY_MANAGER);
    if (G_UNLIKELY (!mgr)) {
      g_critical ("WpPolicy:%p seems registered, but the policy manager "
          "is absent", self);
      return;
    }

    g_signal_emit (mgr, signals[SIGNAL_CHANGED], 0);
  }
}

/**
 * wp_policy_find_endpoint:
 * @core: the #WpCore
 * @props: (transfer floating): properties of the lookup
 * @stream_id: (out): the relevant stream id of the returned endpoint
 *
 * Calls #WpPolicyClass::find_endpoint on all policies, in order, until
 * it finds a suitable endpoint.
 *
 * Returns: (transfer full) (nullable): the found endpoint, or NULL
 */
WpEndpoint *
wp_policy_find_endpoint (WpCore *core, GVariant *props,
    guint32 *stream_id)
{
  WpPolicyManager *mgr;
  GList *l;
  WpPolicy *p;
  WpEndpoint * ret;

  g_return_val_if_fail (WP_IS_CORE (core), NULL);
  g_return_val_if_fail (g_variant_is_of_type (props, G_VARIANT_TYPE_VARDICT), NULL);
  g_return_val_if_fail (stream_id != NULL, NULL);

  mgr = wp_core_get_global (core, WP_GLOBAL_POLICY_MANAGER);
  if (mgr) {
    for (l = g_list_first (mgr->policies); l; l = g_list_next (l)) {
      p = WP_POLICY (l->data);

      if (WP_POLICY_GET_CLASS (p)->find_endpoint &&
          (ret = WP_POLICY_GET_CLASS (p)->find_endpoint (p, props, stream_id)))
        return ret;
    }
  }

  if (g_variant_is_floating (props))
    g_variant_unref (props);

  return NULL;
}
