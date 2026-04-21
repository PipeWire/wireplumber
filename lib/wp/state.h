/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_STATE_H__
#define __WIREPLUMBER_STATE_H__

#include "properties.h"
#include "core.h"

G_BEGIN_DECLS

/* WpState */

/*!
 * \brief The WpState GType
 * \ingroup wpstate
 */
#define WP_TYPE_STATE (wp_state_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpState, wp_state, WP, STATE, GObject)

WP_API
WpState * wp_state_new (const gchar *name);

WP_API
const gchar * wp_state_get_name (WpState *self);

WP_API
const gchar * wp_state_get_location (WpState *self);

WP_API
void wp_state_clear (WpState *self);

WP_API
gboolean wp_state_save (WpState *self, WpProperties *props, GError ** error);

WP_API
void wp_state_save_after_timeout (WpState *self, WpCore *core,
    WpProperties *props);

WP_API
WpProperties * wp_state_load (WpState *self);


/* WpStateMetadata */

/*!
 * \brief Flags to be used as WpObjectFeatures on WpStateMetadata.
 * \ingroup wpstate
 */
typedef enum { /*< flags >*/
  /*! Loads the state metadata */
  WP_STATE_METADATA_LOADED = (1 << 0),
} WpStateMetadataFeatures;

/*!
 * \brief The WpStateMetadata GType
 * \ingroup wpstatemetadata
 */
#define WP_TYPE_STATE_METADATA (wp_state_metadata_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpStateMetadata, wp_state_metadata, WP, STATE_METADATA,
    WpObject)

WP_API
WpStateMetadata * wp_state_metadata_new (WpCore *core, const gchar *name);

WP_API
const gchar * wp_state_metadata_get_name (WpStateMetadata *self);

WP_API
const gchar * wp_state_metadata_get_location (WpStateMetadata *self);

WP_API
void wp_state_metadata_clear (WpStateMetadata *self);

WP_API
const gchar * wp_state_metadata_get (WpStateMetadata *self, const gchar *key);

WP_API
void wp_state_metadata_set (WpStateMetadata *self, const gchar *key,
    const gchar *value);

G_END_DECLS

#endif
