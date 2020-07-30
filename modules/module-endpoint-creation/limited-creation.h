/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_LIMITED_CREATION_H__
#define __WIREPLUMBER_LIMITED_CREATION_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_TYPE_LIMITED_CREATION (wp_limited_creation_get_type ())
G_DECLARE_DERIVABLE_TYPE (WpLimitedCreation, wp_limited_creation,
    WP, LIMITED_CREATION, GObject);

struct _WpLimitedCreationClass
{
  GObjectClass parent_class;

  void (*nodes_changed) (WpLimitedCreation * self);
  void (*node_added) (WpLimitedCreation * self, WpNode *node);
  void (*node_removed) (WpLimitedCreation * self, WpNode *node);
};

WpDevice * wp_limited_creation_get_device (WpLimitedCreation * self);

WpNode * wp_limited_creation_lookup_node (WpLimitedCreation *self, ...);

WpNode * wp_limited_creation_lookup_node_full (WpLimitedCreation *self,
  WpObjectInterest * interest);

WpSession * wp_limited_creation_lookup_session (WpLimitedCreation *self, ...);

WpSession * wp_limited_creation_lookup_session_full (WpLimitedCreation *self,
  WpObjectInterest * interest);

void wp_limited_creation_add_node (WpLimitedCreation * self, WpNode *node);

void wp_limited_creation_remove_node (WpLimitedCreation * self, WpNode *node);

/* for subclasses only */
void wp_endpoint_creation_notify_endpoint_created(WpLimitedCreation * self,
    WpSessionItem *ep);

G_END_DECLS

#endif
