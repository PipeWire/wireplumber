/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PROPERTIES_H__
#define __WIREPLUMBER_PROPERTIES_H__

#include <glib-object.h>

G_BEGIN_DECLS

struct pw_properties;
struct spa_dict;

#define WP_TYPE_PROPERTIES (wp_properties_get_type ())
GType wp_properties_get_type (void);

typedef struct _WpProperties WpProperties;

WpProperties * wp_properties_new_empty (void);
WpProperties * wp_properties_new (const gchar * key, ...) G_GNUC_NULL_TERMINATED;
WpProperties * wp_properties_new_valist (const gchar * key, va_list args);
WpProperties * wp_properties_new_string (const gchar * str);

WpProperties * wp_properties_new_wrap (struct pw_properties * props);
WpProperties * wp_properties_new_take (struct pw_properties * props);
WpProperties * wp_properties_new_copy (const struct pw_properties * props);

WpProperties * wp_properties_new_wrap_dict (const struct spa_dict * dict);
WpProperties * wp_properties_new_copy_dict (const struct spa_dict * dict);

WpProperties * wp_properties_copy (WpProperties * other);

WpProperties * wp_properties_ref (WpProperties * self);
void wp_properties_unref (WpProperties * self);

gint wp_properties_update_from_dict (WpProperties * self,
    const struct spa_dict * dict);

gint wp_properties_copy_keys (WpProperties * src, WpProperties * dst,
    const gchar *key1, ...) G_GNUC_NULL_TERMINATED;
gint wp_properties_copy_keys_valist (WpProperties * src, WpProperties * dst,
    const gchar *key1, va_list args);
void wp_properties_copy_all (WpProperties * src, WpProperties * dst);

const gchar * wp_properties_get (WpProperties * self, const gchar * key);

gint wp_properties_set (WpProperties * self, const gchar * key,
    const gchar * value);
gint wp_properties_setf (WpProperties * self, const gchar * key,
    const gchar * format, ...) G_GNUC_PRINTF(3, 4);
gint wp_properties_setf_valist (WpProperties * self, const gchar * key,
    const gchar * format, va_list args);

const struct spa_dict * wp_properties_peek_dict (WpProperties * self);
struct pw_properties * wp_properties_to_pw_properties (WpProperties * self);
gboolean wp_properties_matches (WpProperties * self, WpProperties *other);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (WpProperties, wp_properties_unref)

G_END_DECLS

#endif
