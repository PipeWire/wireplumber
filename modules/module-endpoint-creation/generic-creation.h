/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_GENERIC_CREATION_H__
#define __WIREPLUMBER_GENERIC_CREATION_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_TYPE_GENERIC_CREATION (wp_generic_creation_get_type ())
G_DECLARE_FINAL_TYPE (WpGenericCreation, wp_generic_creation, WP,
    GENERIC_CREATION, GObject);

WpGenericCreation * wp_generic_creation_new (WpCore *core);

void wp_generic_creation_add_node (WpGenericCreation * self, WpNode *node);
void wp_generic_creation_remove_node (WpGenericCreation * self, WpNode *node);

G_END_DECLS

#endif
