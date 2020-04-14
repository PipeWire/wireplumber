/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_DEBUG_H__
#define __WIREPLUMBER_DEBUG_H__

#include <glib-object.h>
#include "defs.h"

G_BEGIN_DECLS

#define WP_LOG_LEVEL_TRACE (1 << G_LOG_LEVEL_USER_SHIFT)

#define WP_OBJECT_FORMAT "<%s:%p>"
#define WP_OBJECT_ARGS(object) G_OBJECT_TYPE_NAME(object), object

WP_API
gboolean wp_log_level_is_enabled (GLogLevelFlags log_level);

WP_API
GLogWriterOutput wp_log_writer_default (GLogLevelFlags log_level,
    const GLogField *fields, gsize n_fields, gpointer user_data);

WP_API
void wp_log_structured_standard (const gchar *log_domain,
    GLogLevelFlags log_level, const gchar *file, const gchar *line,
    const gchar *func, GType object_type, gconstpointer object,
    const gchar *message_format, ...) G_GNUC_PRINTF (8, 9);

#define wp_log(level, type, object, ...) \
({ \
  if (G_UNLIKELY (wp_log_level_is_enabled (level))) \
    wp_log_structured_standard (G_LOG_DOMAIN, level, __FILE__, \
        G_STRINGIFY (__LINE__), G_STRFUNC, type, object, __VA_ARGS__); \
})

#define wp_warning(...) \
    wp_log (G_LOG_LEVEL_WARNING, 0, NULL, __VA_ARGS__)
#define wp_message(...) \
    wp_log (G_LOG_LEVEL_MESSAGE, 0, NULL, __VA_ARGS__)
#define wp_info(...) \
    wp_log (G_LOG_LEVEL_INFO, 0, NULL, __VA_ARGS__)
#define wp_debug(...) \
    wp_log (G_LOG_LEVEL_DEBUG, 0, NULL, __VA_ARGS__)
#define wp_trace(...) \
    wp_log (WP_LOG_LEVEL_TRACE, 0, NULL, __VA_ARGS__)

#define wp_warning_object(object, ...)  \
    wp_log (G_LOG_LEVEL_WARNING, G_TYPE_FROM_INSTANCE (object), object, __VA_ARGS__)
#define wp_message_object(object, ...)  \
    wp_log (G_LOG_LEVEL_MESSAGE, G_TYPE_FROM_INSTANCE (object), object, __VA_ARGS__)
#define wp_info_object(object, ...)  \
    wp_log (G_LOG_LEVEL_INFO, G_TYPE_FROM_INSTANCE (object), object, __VA_ARGS__)
#define wp_debug_object(object, ...)  \
    wp_log (G_LOG_LEVEL_DEBUG, G_TYPE_FROM_INSTANCE (object), object, __VA_ARGS__)
#define wp_trace_object(object, ...)  \
    wp_log (WP_LOG_LEVEL_TRACE, G_TYPE_FROM_INSTANCE (object), object, __VA_ARGS__)

#define wp_warning_boxed(type, object, ...) \
    wp_log (G_LOG_LEVEL_WARNING, type, object, __VA_ARGS__)
#define wp_message_boxed(type, object, ...) \
    wp_log (G_LOG_LEVEL_MESSAGE, type, object, __VA_ARGS__)
#define wp_info_boxed(type, object, ...) \
    wp_log (G_LOG_LEVEL_INFO, type, object, __VA_ARGS__)
#define wp_debug_boxed(type, object, ...) \
    wp_log (G_LOG_LEVEL_DEBUG, type, object, __VA_ARGS__)
#define wp_trace_boxed(type, object, ...) \
    wp_log (WP_LOG_LEVEL_TRACE, type, object, __VA_ARGS__)

WP_API
void wp_install_glib_pw_log (void);

G_END_DECLS

#endif
