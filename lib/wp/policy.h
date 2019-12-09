/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_POLICY_H__
#define __WIREPLUMBER_POLICY_H__

#include "base-endpoint.h"

G_BEGIN_DECLS

/**
 * WpPolicyRank:
 * @WP_POLICY_RANK_UPSTREAM: should only be used inside WirePlumber
 * @WP_POLICY_RANK_PLATFORM: policies provided by the platform
 * @WP_POLICY_RANK_VENDOR: policies provided by hardware vendors
 *
 * The rank of a policy is an unsigned integer that can take an arbitrary
 * value from 0 to G_MAXINT32 (0x7fffffff). On invocation, policies ranked
 * with a higher number are tried first, which is how one can implement
 * overrides. This enum provides default values for certain kinds of policies.
 * Feel free to add/substract numbers to these constants in order to make a
 * hierarchy, if you are implementing multiple different policies that need to
 * be tried in a certain order.
 */
typedef enum {
  WP_POLICY_RANK_UPSTREAM = 1,
  WP_POLICY_RANK_PLATFORM = 128,
  WP_POLICY_RANK_VENDOR = 256,
} WpPolicyRank;

#define WP_TYPE_POLICY_MANAGER (wp_policy_manager_get_type ())
G_DECLARE_FINAL_TYPE (WpPolicyManager, wp_policy_manager, WP, POLICY_MANAGER, GObject)

#define WP_TYPE_POLICY (wp_policy_get_type ())
G_DECLARE_DERIVABLE_TYPE (WpPolicy, wp_policy, WP, POLICY, GObject)

struct _WpPolicyClass
{
  GObjectClass parent_class;

  void (*endpoint_added) (WpPolicy *self, WpBaseEndpoint *ep);
  void (*endpoint_removed) (WpPolicy *self, WpBaseEndpoint *ep);

  WpBaseEndpoint * (*find_endpoint) (WpPolicy *self, GVariant *props,
      guint32 *stream_id);
};

WpPolicyManager * wp_policy_manager_get_instance (WpCore *core);
GPtrArray * wp_policy_manager_list_endpoints (WpPolicyManager * self,
    const gchar * media_class);

guint32 wp_policy_get_rank (WpPolicy *self);
WpCore *wp_policy_get_core (WpPolicy *self);

void wp_policy_register (WpPolicy *self, WpCore *core);
void wp_policy_unregister (WpPolicy *self);

void wp_policy_notify_changed (WpPolicy *self);

WpBaseEndpoint * wp_policy_find_endpoint (WpCore *core, GVariant *props,
    guint32 *stream_id);

G_END_DECLS

#endif
