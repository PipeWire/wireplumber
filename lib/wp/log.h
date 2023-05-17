/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_LOG_H__
#define __WIREPLUMBER_LOG_H__

#include <glib-object.h>
#include "defs.h"

G_BEGIN_DECLS

#define WP_LOG_LEVEL_TRACE (1 << G_LOG_LEVEL_USER_SHIFT)

#define WP_OBJECT_FORMAT "<%s:%p>"
#define WP_OBJECT_ARGS(object) \
    (object ? G_OBJECT_TYPE_NAME(object) : "invalid"), object

WP_PRIVATE_API
void wp_log_init (gint flags);

WP_PRIVATE_API
void wp_log_set_global_level (const gchar *log_level);

typedef struct _WpLogTopic WpLogTopic;
struct _WpLogTopic {
  const char *topic_name;

  /*< private >*/
  /*
   * lower 16 bits: GLogLevelFlags
   * bit 29: has_custom_level
   * bit 30: a g_bit_lock
   * bit 31: 1 - initialized, 0 - not initialized
   */
  gint flags;
  gint *global_flags;
  WP_PADDING(2)
};

#define WP_LOG_TOPIC_EXTERN(var) \
  extern WpLogTopic * var;

#define WP_LOG_TOPIC(var, t) \
  WpLogTopic var##_struct = { .topic_name = t, .flags = 0 }; \
  WpLogTopic * var = &(var##_struct);

#define WP_LOG_TOPIC_STATIC(var, t) \
  static WpLogTopic var##_struct = { .topic_name = t, .flags = 0 }; \
  static G_GNUC_UNUSED WpLogTopic * var = &(var##_struct);

#define WP_DEFINE_LOCAL_LOG_TOPIC(t) \
  WP_LOG_TOPIC_STATIC(WP_LOCAL_LOG_TOPIC, t)

/* make glib log functions also use the local log topic */
#ifdef G_LOG_DOMAIN
# undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN (WP_LOCAL_LOG_TOPIC->topic_name)

WP_API
void wp_log_topic_init (WpLogTopic *topic);

static inline gboolean
wp_log_topic_is_initialized (WpLogTopic *topic)
{
  return (topic->flags & (1u << 31)) != 0;
}

static inline gboolean
wp_log_topic_has_custom_level (WpLogTopic *topic)
{
  return (topic->flags & (1u << 29)) != 0;
}

static inline gboolean
wp_log_topic_is_enabled (WpLogTopic *topic, GLogLevelFlags log_level)
{
  /* first time initialization */
  if (G_UNLIKELY (!wp_log_topic_is_initialized (topic)))
    wp_log_topic_init (topic);

  if (wp_log_topic_has_custom_level (topic))
    return (topic->flags & (log_level & 0xFF)) != 0;
  else
    return (*topic->global_flags & (log_level & 0xFF)) != 0;
}

#define wp_local_log_topic_is_enabled(log_level) \
  (wp_log_topic_is_enabled (WP_LOCAL_LOG_TOPIC, log_level))

WP_API
GLogWriterOutput wp_log_writer_default (GLogLevelFlags log_level,
    const GLogField *fields, gsize n_fields, gpointer user_data);

WP_API
void wp_log_checked (const gchar *log_topic, GLogLevelFlags log_level,
    const gchar *file, const gchar *line, const gchar *func,
    GType object_type, gconstpointer object,
    const gchar *message_format, ...) G_GNUC_PRINTF (8, 9);

#define wp_log(topic, level, type, object, ...) \
({ \
  if (G_UNLIKELY (wp_log_topic_is_enabled (topic, level))) \
    wp_log_checked (topic->topic_name, level, __FILE__, G_STRINGIFY (__LINE__), \
        G_STRFUNC, type, object, __VA_ARGS__); \
})

#define wp_critical(...) \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_CRITICAL, 0, NULL, __VA_ARGS__)
#define wp_warning(...) \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_WARNING, 0, NULL, __VA_ARGS__)
#define wp_message(...) \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_MESSAGE, 0, NULL, __VA_ARGS__)
#define wp_info(...) \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_INFO, 0, NULL, __VA_ARGS__)
#define wp_debug(...) \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_DEBUG, 0, NULL, __VA_ARGS__)
#define wp_trace(...) \
    wp_log (WP_LOCAL_LOG_TOPIC, WP_LOG_LEVEL_TRACE, 0, NULL, __VA_ARGS__)

#define wp_critical_object(object, ...)  \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_CRITICAL, (object) ? G_TYPE_FROM_INSTANCE (object) : G_TYPE_NONE, object, __VA_ARGS__)
#define wp_warning_object(object, ...)  \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_WARNING, (object) ? G_TYPE_FROM_INSTANCE (object) : G_TYPE_NONE, object, __VA_ARGS__)
#define wp_message_object(object, ...)  \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_MESSAGE, (object) ? G_TYPE_FROM_INSTANCE (object) : G_TYPE_NONE, object, __VA_ARGS__)
#define wp_info_object(object, ...)  \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_INFO, (object) ? G_TYPE_FROM_INSTANCE (object) : G_TYPE_NONE, object, __VA_ARGS__)
#define wp_debug_object(object, ...)  \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_DEBUG, (object) ? G_TYPE_FROM_INSTANCE (object) : G_TYPE_NONE, object, __VA_ARGS__)
#define wp_trace_object(object, ...)  \
    wp_log (WP_LOCAL_LOG_TOPIC, WP_LOG_LEVEL_TRACE, (object) ? G_TYPE_FROM_INSTANCE (object) : G_TYPE_NONE, object, __VA_ARGS__)

#define wp_critical_boxed(type, object, ...) \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_CRITICAL, type, object, __VA_ARGS__)
#define wp_warning_boxed(type, object, ...) \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_WARNING, type, object, __VA_ARGS__)
#define wp_message_boxed(type, object, ...) \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_MESSAGE, type, object, __VA_ARGS__)
#define wp_info_boxed(type, object, ...) \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_INFO, type, object, __VA_ARGS__)
#define wp_debug_boxed(type, object, ...) \
    wp_log (WP_LOCAL_LOG_TOPIC, G_LOG_LEVEL_DEBUG, type, object, __VA_ARGS__)
#define wp_trace_boxed(type, object, ...) \
    wp_log (WP_LOCAL_LOG_TOPIC, WP_LOG_LEVEL_TRACE, type, object, __VA_ARGS__)

struct spa_log;

WP_API
struct spa_log * wp_spa_log_get_instance (void);

G_END_DECLS

#endif
