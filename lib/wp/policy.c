/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpPolicy
 */

#define G_LOG_DOMAIN "wp-policy"

#include <pipewire/pipewire.h>

#include "policy.h"
#include "debug.h"
#include "private.h"

/* WpPolicyManager */

struct _WpPolicyManager
{
  GObject parent;
  GList *policies;
  WpObjectManager *endpoints_om;
  WpObjectManager *sessions_om;
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
  self->endpoints_om = wp_object_manager_new ();
  self->sessions_om = wp_object_manager_new ();
}

static void
wp_policy_manager_finalize (GObject *object)
{
  WpPolicyManager *self = WP_POLICY_MANAGER (object);

  wp_trace_object (self, "destroyed");

  g_clear_object (&self->sessions_om);
  g_clear_object (&self->endpoints_om);
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
policy_mgr_endpoint_added (WpObjectManager *om, WpBaseEndpoint *ep,
    WpPolicyManager *self)
{
  GList *l;
  WpPolicy *p;

  for (l = g_list_first (self->policies); l; l = g_list_next (l)) {
    p = WP_POLICY (l->data);

    if (WP_POLICY_GET_CLASS (p)->endpoint_added)
      WP_POLICY_GET_CLASS (p)->endpoint_added (p, ep);
  }
}

static void
policy_mgr_endpoint_removed (WpObjectManager *om, WpBaseEndpoint *ep,
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

  mgr = wp_registry_find_object (wp_core_get_registry (core),
      (GEqualFunc) WP_IS_POLICY_MANAGER, NULL);
  if (G_UNLIKELY (!mgr)) {
    mgr = g_object_new (WP_TYPE_POLICY_MANAGER, NULL);

    /* install the object manager to listen to added/removed endpoints */
    wp_object_manager_add_interest (mgr->endpoints_om,
        WP_TYPE_BASE_ENDPOINT, NULL, 0);
    g_signal_connect_object (mgr->endpoints_om, "object-added",
        (GCallback) policy_mgr_endpoint_added, mgr, 0);
    g_signal_connect_object (mgr->endpoints_om, "object-removed",
        (GCallback) policy_mgr_endpoint_removed, mgr, 0);
    wp_core_install_object_manager (core, mgr->endpoints_om);

    /* install the object manager to listen to changed sessions */
    wp_object_manager_add_interest (mgr->sessions_om,
        WP_TYPE_IMPL_SESSION, NULL,
        WP_PROXY_FEATURES_STANDARD | WP_SESSION_FEATURE_DEFAULT_ENDPOINT);
    wp_core_install_object_manager (core, mgr->sessions_om);

    wp_registry_register_object (wp_core_get_registry (core),
        g_object_ref (mgr));
  }

  return mgr;
}

/**
 * wp_policy_manager_get_session:
 * @self: the policy manager
 *
 * Returns: (transfer full) (nullable): the active session
 */
WpSession *
wp_policy_manager_get_session (WpPolicyManager *self)
{
  g_autoptr (GPtrArray) arr = NULL;

  g_return_val_if_fail (WP_IS_POLICY_MANAGER (self), NULL);

  arr = wp_object_manager_get_objects (self->sessions_om, 0);
  return (arr->len > 0) ? g_object_ref (g_ptr_array_index (arr, 0)) : NULL;
}

static inline gboolean
media_class_matches (const gchar * media_class, const gchar * lookup)
{
  const gchar *c1 = media_class, *c2 = lookup;

  /* empty lookup matches all classes */
  if (!lookup)
    return TRUE;

  /* compare until we reach the end of the lookup string */
  for (; *c2 != '\0'; c1++, c2++) {
    if (*c1 != *c2)
      return FALSE;
  }

  /* the lookup may not end in a slash, however it must match up
   * to the end of a submedia_class. i.e.:
   * match: media_class: Audio/Source/Virtual
   *        lookup: Audio/Source
   *
   * NO match: media_class: Audio/Source/Virtual
   *           lookup: Audio/Sou
   *
   * if *c1 is not /, also check the previous char, because the lookup
   * may actually end in a slash:
   *
   * match: media_class: Audio/Source/Virtual
   *        lookup: Audio/Source/
   */
  if (!(*c1 == '/' || *c1 == '\0' || *(c1 - 1) == '/'))
    return FALSE;

  return TRUE;
}

/**
 * wp_policy_manager_list_endpoints:
 * @self: the policy manager
 * @media_class: the media class lookup string
 *
 * Returns: (transfer full) (element-type WpBaseEndpoint*): an array with all the
 *   endpoints matching the media class lookup string
 */
GPtrArray *
wp_policy_manager_list_endpoints (WpPolicyManager * self,
    const gchar * media_class)
{
  GPtrArray * ret;
  guint i;

  g_return_val_if_fail (WP_IS_POLICY_MANAGER (self), NULL);

  ret = wp_object_manager_get_objects (self->endpoints_om, 0);
  for (i = ret->len; i > 0; i--) {
    WpBaseEndpoint *ep = g_ptr_array_index (ret, i-1);
    if (!media_class_matches (wp_base_endpoint_get_media_class (ep), media_class))
      g_ptr_array_remove_index_fast (ret, i-1);
  }
  return ret;
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
  GWeakRef core;
};

enum {
  PROP_0,
  PROP_RANK
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpPolicy, wp_policy, G_TYPE_OBJECT)

static void
wp_policy_init (WpPolicy *self)
{
  WpPolicyPrivate *priv = wp_policy_get_instance_private (self);

  g_weak_ref_init (&priv->core, NULL);
}

static void
wp_policy_finalize (GObject * object)
{
  WpPolicyPrivate *priv = wp_policy_get_instance_private (WP_POLICY (object));

  g_weak_ref_clear (&priv->core);

  G_OBJECT_CLASS (wp_policy_parent_class)->finalize (object);
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

  object_class->finalize = wp_policy_finalize;
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
  return g_weak_ref_get (&priv->core);
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
  g_weak_ref_set (&priv->core, core);

  mgr = wp_policy_manager_get_instance (core);
  mgr->policies = g_list_insert_sorted (mgr->policies, g_object_ref (self),
      (GCompareFunc) compare_ranks);
  g_signal_emit (mgr, signals[SIGNAL_CHANGED], 0);
}

void
wp_policy_unregister (WpPolicy *self)
{
  g_autoptr (WpPolicyManager) mgr = NULL;
  g_autoptr (WpCore) core = NULL;
  WpPolicyPrivate *priv;

  g_return_if_fail (WP_IS_POLICY (self));

  priv = wp_policy_get_instance_private (self);

  core = g_weak_ref_get (&priv->core);
  if (core) {
    mgr = wp_registry_find_object (wp_core_get_registry (core),
        (GEqualFunc) WP_IS_POLICY_MANAGER, NULL);
    if (G_UNLIKELY (!mgr)) {
      g_critical (WP_OBJECT_FORMAT " seems registered, but the policy manager "
          "is absent", WP_OBJECT_ARGS (self));
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
  g_autoptr (WpPolicyManager) mgr = NULL;
  g_autoptr (WpCore) core = NULL;
  WpPolicyPrivate *priv;

  g_return_if_fail (WP_IS_POLICY (self));

  priv = wp_policy_get_instance_private (self);

  core = g_weak_ref_get (&priv->core);
  if (core) {
    mgr = wp_registry_find_object (wp_core_get_registry (core),
        (GEqualFunc) WP_IS_POLICY_MANAGER, NULL);
    if (G_UNLIKELY (!mgr)) {
      g_critical (WP_OBJECT_FORMAT " seems registered, but the policy manager "
          "is absent", WP_OBJECT_ARGS (self));
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
WpBaseEndpoint *
wp_policy_find_endpoint (WpCore *core, GVariant *props,
    guint32 *stream_id)
{
  g_autoptr (WpPolicyManager) mgr = NULL;
  GList *l;
  WpPolicy *p;
  WpBaseEndpoint * ret;

  g_return_val_if_fail (WP_IS_CORE (core), NULL);
  g_return_val_if_fail (g_variant_is_of_type (props, G_VARIANT_TYPE_VARDICT), NULL);
  g_return_val_if_fail (stream_id != NULL, NULL);

  mgr = wp_registry_find_object (wp_core_get_registry (core),
        (GEqualFunc) WP_IS_POLICY_MANAGER, NULL);
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
