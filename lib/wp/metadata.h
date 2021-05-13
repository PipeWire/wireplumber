/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Raghavendra Rao <raghavendra.rao@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_METADATA_H__
#define __WIREPLUMBER_METADATA_H__

#include "global-proxy.h"

G_BEGIN_DECLS

/*!
 * @memberof WpMetadata
 *
 * @brief
 * @em WP_METADATA_FEATURE_DATA: caches metadata locally
 *
 * An extension of [WpProxyFeatures](@ref proxy_features_section)
 */
typedef enum { /*< flags >*/
  WP_METADATA_FEATURE_DATA = (WP_PROXY_FEATURE_CUSTOM_START << 0),
} WpMetadataFeatures;

/*!
 * @memberof WpMetadata
 *
 * @brief The [WpMetadata](@ref metadata_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_METADATA (wp_metadata_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_METADATA (wp_metadata_get_type ())

WP_API
G_DECLARE_DERIVABLE_TYPE (WpMetadata, wp_metadata, WP, METADATA, WpGlobalProxy)

/*!
 * @memberof WpMetadata
 *
 * @brief
 * @em parent_class
 */
struct _WpMetadataClass
{
  WpGlobalProxyClass parent_class;
};

WP_API
WpIterator * wp_metadata_new_iterator (WpMetadata * self, guint32 subject);

WP_API
void wp_metadata_iterator_item_extract (const GValue * item, guint32 * subject,
    const gchar ** key, const gchar ** type, const gchar ** value);

WP_API
const gchar * wp_metadata_find (WpMetadata * self, guint32 subject,
    const gchar * key, const gchar ** type);

WP_API
void wp_metadata_set (WpMetadata * self, guint32 subject,
    const gchar * key, const gchar * type, const gchar * value);

WP_API
void wp_metadata_clear (WpMetadata * self);

/*!
 * @memberof WpMetadata
 *
 * @brief The [WpImplMetadata](@ref impl_metadata_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_IMPL_METADATA (wp_impl_metadata_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_IMPL_METADATA (wp_impl_metadata_get_type ())

WP_API
G_DECLARE_FINAL_TYPE (WpImplMetadata, wp_impl_metadata, WP, IMPL_METADATA, WpMetadata)

WP_API
WpImplMetadata * wp_impl_metadata_new (WpCore * core);

G_END_DECLS

#endif
