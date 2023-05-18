/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "log.h"
#include "wp.h"
#include <pipewire/pipewire.h>
#include <spa/support/log.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-log")

/*!
 * \defgroup wplog Debug Logging
 * \{
 */
/*!
 * \def WP_LOG_LEVEL_TRACE
 * \brief A custom GLib log level for trace messages (see GLogLevelFlags)
 */
/*!
 * \def WP_OBJECT_FORMAT
 * \brief A format string to print GObjects with WP_OBJECT_ARGS()
 * For example:
 * \code
 * GObject *myobj = ...;
 * wp_debug ("This: " WP_OBJECT_FORMAT " is an object", WP_OBJECT_ARGS (myobj));
 * \endcode
 */
/*!
 * \def WP_OBJECT_ARGS(object)
 * \brief A macro to format an object for printing with WP_OBJECT_FORMAT
 */
/*!
 * \def wp_critical(...)
 * \brief Logs a critical message to the standard log via GLib's logging system.
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_critical_object(object, ...)
 * \brief Logs a critical message to the standard log via GLib's logging system.
 * \param object A GObject associated with the log; this is printed in a special
 *   way to make it easier to track messages from a specific object
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_critical_boxed(type, object, ...)
 * \brief Logs a critical message to the standard log via GLib's logging system.
 * \param type The type of \a object
 * \param object A boxed object associated with the log; this is printed in a
 *   special way to make it easier to track messages from a specific object.
 *   For some object types, contents from the object are also printed (ex WpSpaPod)
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_warning(...)
 * \brief Logs a warning message to the standard log via GLib's logging system.
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_warning_object(object, ...)
 * \brief Logs a warning message to the standard log via GLib's logging system.
 * \param object A GObject associated with the log; this is printed in a special
 *   way to make it easier to track messages from a specific object
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_warning_boxed(type, object, ...)
 * \brief Logs a warning message to the standard log via GLib's logging system.
 * \param type The type of \a object
 * \param object A boxed object associated with the log; this is printed in a
 *   special way to make it easier to track messages from a specific object.
 *   For some object types, contents from the object are also printed (ex WpSpaPod)
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_notice(...)
 * \brief Logs a notice message to the standard log via GLib's logging system.
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_notice_object(object, ...)
 * \brief Logs a notice message to the standard log via GLib's logging system.
 * \param object A GObject associated with the log; this is printed in a special
 *   way to make it easier to track messages from a specific object
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_notice_boxed(type, object, ...)
 * \brief Logs a notice message to the standard log via GLib's logging system.
 * \param type The type of \a object
 * \param object A boxed object associated with the log; this is printed in a
 *   special way to make it easier to track messages from a specific object.
 *   For some object types, contents from the object are also printed (ex WpSpaPod)
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_info(...)
 * \brief Logs a info message to the standard log via GLib's logging system.
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_info_object(object, ...)
 * \brief Logs a info message to the standard log via GLib's logging system.
 * \param object A GObject associated with the log; this is printed in a special
 *   way to make it easier to track messages from a specific object
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_info_boxed(type, object, ...)
 * \brief Logs a info message to the standard log via GLib's logging system.
 * \param type The type of \a object
 * \param object A boxed object associated with the log; this is printed in a
 *   special way to make it easier to track messages from a specific object.
 *   For some object types, contents from the object are also printed (ex WpSpaPod)
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_debug(...)
 * \brief Logs a debug message to the standard log via GLib's logging system.
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_debug_object(object, ...)
 * \brief Logs a debug message to the standard log via GLib's logging system.
 * \param object A GObject associated with the log; this is printed in a special
 *   way to make it easier to track messages from a specific object
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_debug_boxed(type, object, ...)
 * \brief Logs a debug message to the standard log via GLib's logging system.
 * \param type The type of \a object
 * \param object A boxed object associated with the log; this is printed in a
 *   special way to make it easier to track messages from a specific object.
 *   For some object types, contents from the object are also printed (ex WpSpaPod)
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_trace(...)
 * \brief Logs a trace message to the standard log via GLib's logging system.
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_trace_object(object, ...)
 * \brief Logs a trace message to the standard log via GLib's logging system.
 * \param object A GObject associated with the log; this is printed in a special
 *   way to make it easier to track messages from a specific object
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_trace_boxed(type, object, ...)
 * \brief Logs a trace message to the standard log via GLib's logging system.
 * \param type The type of \a object
 * \param object A boxed object associated with the log; this is printed in a
 *   special way to make it easier to track messages from a specific object.
 *   For some object types, contents from the object are also printed (ex WpSpaPod)
 * \param ... A format string, followed by format arguments in printf() style
 */
/*!
 * \def wp_log(level, type, object, ...)
 * \brief The generic form of all the logging macros
 * \remark Don't use this directly, use one of the other logging macros
 */
/*! \} */

static GString *spa_dbg_str = NULL;
#define spa_debug(...) \
({ \
  g_string_append_printf (spa_dbg_str, __VA_ARGS__); \
  g_string_append_c (spa_dbg_str, '\n'); \
})

#include <spa/debug/pod.h>

struct log_topic_pattern
{
  GPatternSpec *spec;
  gint log_level;
};

static struct {
  gboolean use_color;
  gboolean output_is_journal;
  gint global_log_level;
  GLogLevelFlags global_log_level_flags;
  struct log_topic_pattern *patterns;
} log_state = {
  .use_color = FALSE,
  .output_is_journal = FALSE,
  .global_log_level = 4 /* MESSAGE */,
  .global_log_level_flags = G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR,
  .patterns = NULL,
};

/* reference: https://en.wikipedia.org/wiki/ANSI_escape_code#3/4_bit */
#define COLOR_RED            "\033[1;31m"
#define COLOR_GREEN          "\033[1;32m"
#define COLOR_YELLOW         "\033[1;33m"
#define COLOR_BLUE           "\033[1;34m"
#define COLOR_MAGENTA        "\033[1;35m"
#define COLOR_CYAN           "\033[1;36m"
#define COLOR_BRIGHT_RED     "\033[1;91m"
#define COLOR_BRIGHT_GREEN   "\033[1;92m"
#define COLOR_BRIGHT_YELLOW  "\033[1;93m"
#define COLOR_BRIGHT_BLUE    "\033[1;94m"
#define COLOR_BRIGHT_MAGENTA "\033[1;95m"
#define COLOR_BRIGHT_CYAN    "\033[1;96m"

#define RESET_COLOR          "\033[0m"

/* our palette */
#define DOMAIN_COLOR    COLOR_MAGENTA
#define LOCATION_COLOR  COLOR_BLUE

/* available colors for object printouts (the <Object:0xfoobar>) */
static const gchar *object_colors[] = {
  COLOR_RED, COLOR_GREEN, COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN,
  COLOR_BRIGHT_RED, COLOR_BRIGHT_GREEN, COLOR_BRIGHT_YELLOW,
  COLOR_BRIGHT_MAGENTA, COLOR_BRIGHT_CYAN
};

/*
 * priority numbers are based on GLib's gmessages.c
 * reference: http://man7.org/linux/man-pages/man3/syslog.3.html#DESCRIPTION
 */
static const struct {
  GLogLevelFlags log_level_flags;
  enum spa_log_level spa_level;
  gchar name[6];
  gchar priority[2];
  gchar color[8];
} log_level_info[] = {
  { 0,                   0,                  "U", "0", COLOR_BRIGHT_MAGENTA },
  { G_LOG_LEVEL_ERROR,   0,                  "F", "3" /* LOG_ERR */, COLOR_BRIGHT_RED },
  { G_LOG_LEVEL_CRITICAL,SPA_LOG_LEVEL_ERROR,"E", "4" /* LOG_WARNING */, COLOR_RED },
  { G_LOG_LEVEL_WARNING, SPA_LOG_LEVEL_WARN, "W", "4" /* LOG_WARNING */, COLOR_BRIGHT_YELLOW },
  { G_LOG_LEVEL_MESSAGE, SPA_LOG_LEVEL_WARN, "N", "5" /* LOG_NOTICE */, COLOR_BRIGHT_GREEN },
  { G_LOG_LEVEL_INFO,    SPA_LOG_LEVEL_INFO, "I", "6" /* LOG_INFO */, COLOR_GREEN },
  { G_LOG_LEVEL_DEBUG,   SPA_LOG_LEVEL_DEBUG,"D", "7" /* LOG_DEBUG */, COLOR_BRIGHT_CYAN },
  { WP_LOG_LEVEL_TRACE,  SPA_LOG_LEVEL_TRACE,"T", "7" /* LOG_DEBUG */, COLOR_CYAN },
};

/* map glib's log levels, which are flags in the range (1<<2) to (1<<8),
  to the 1-7 range; first calculate the integer part of log2(log_level)
  to bring it down to 2-8 and substract 1 */
static G_GNUC_CONST inline gint
level_index_from_flags (GLogLevelFlags log_level)
{
  gint logarithm = 0;
  while ((log_level >>= 1) != 0)
    logarithm += 1;
  return (logarithm >= 2 && logarithm <= 8) ? (logarithm - 1) : 0;
}

/* map an index in the log_level_info table to a single GLogLevelFlags flag */
static G_GNUC_CONST inline GLogLevelFlags
level_index_to_flag (gint lvl_index)
{
  return log_level_info [lvl_index].log_level_flags;
}

/* map an index in the log_level_info table to an OR combination of all the
   GLogLevelFlags that are enabled at this level */
static G_GNUC_CONST inline GLogLevelFlags
level_index_to_full_flags (gint lvl_index)
{
  GLogLevelFlags flags = 0;
  for (gint i = 1; i <= lvl_index; i++) {
    flags |= level_index_to_flag (i);
  }
  return flags;
}

/* map a SPA_LOG_LEVEL_* to an index in the log_level_info table;
   note that SPA_LOG_LEVEL_WARN maps to 4 (G_LOG_LEVEL_MESSAGE) and
   index 3 (G_LOG_LEVEL_WARNING) can never be returned */
static G_GNUC_CONST inline gint
level_index_from_spa (gint spa_lvl)
{
  if (G_UNLIKELY (spa_lvl <= SPA_LOG_LEVEL_NONE))
    return 0;
  else if (spa_lvl < SPA_LOG_LEVEL_WARN)
    return spa_lvl + 1;
  else if (G_UNLIKELY (spa_lvl > SPA_LOG_LEVEL_TRACE))
    return (gint) G_N_ELEMENTS (log_level_info) - 1;
  else
    return spa_lvl + 2;
}

/* map an index in the log_level_info table to a SPA_LOG_LEVEL_*
   here, G_LOG_LEVEL_MESSAGE maps to SPA_LOG_LEVEL_WARN */
static G_GNUC_CONST inline gint
level_index_to_spa (gint lvl_index)
{
  return log_level_info [lvl_index].spa_level;
}

static gint
level_index_from_string (const char *str)
{
  g_return_val_if_fail (str != NULL, 0);

  for (guint i = 0; i < G_N_ELEMENTS (log_level_info); i++) {
    if (g_str_equal (str, log_level_info[i].name))
      return i;
  }

  return level_index_from_spa (atoi (str));
}

/* private, called from wp_init() */
void
wp_log_init (gint flags)
{
  const gchar *level_str;
  gint global_log_level = log_state.global_log_level;
  struct log_topic_pattern *patterns = NULL, *pttrn;
  gint n_tokens = 0;
  gchar **tokens = NULL;

  level_str = g_getenv ("WIREPLUMBER_DEBUG");

  log_state.use_color = g_log_writer_supports_color (fileno (stderr));
  log_state.output_is_journal = g_log_writer_is_journald (fileno (stderr));

  if (level_str && level_str[0] != '\0') {
    /* [<glob>:]<level>,..., */
    tokens = pw_split_strv (level_str, ",", INT_MAX, &n_tokens);
  }

  /* allocate enough space to hold all pattern specs */
  patterns = g_malloc_n ((n_tokens + 2), sizeof (struct log_topic_pattern));
  pttrn = patterns;
  if (!patterns)
    g_error ("unable to allocate space for %d log patterns", n_tokens + 2);

  for (gint i = 0; i < n_tokens; i++) {
    gint n_tok;
    gchar **tok;
    gint lvl;

    tok = pw_split_strv (tokens[i], ":", 2, &n_tok);
    if (n_tok == 2 && (lvl = level_index_from_string (tok[1]))) {
      pttrn->spec = g_pattern_spec_new (tok[0]);
      pttrn->log_level = lvl;
      pttrn++;
    } else if (n_tok == 1 && (lvl = level_index_from_string (tok[0]))) {
      global_log_level = lvl;
    } else {
      /* note that this is going to initialize the wp-log topic here */
      wp_warning ("Ignoring invalid format in WIREPLUMBER_DEBUG: '%s'",
          tokens[i]);
    }

    pw_free_strv (tok);
  }

  /* disable pipewire connection trace by default */
  pttrn->spec = g_pattern_spec_new ("conn.*");
  pttrn->log_level = 0;
  pttrn++;

  /* terminate with NULL */
  pttrn->spec = NULL;
  pttrn->log_level = 0;

  pw_free_strv (tokens);

  log_state.patterns = patterns;
  log_state.global_log_level = global_log_level;
  log_state.global_log_level_flags =
      level_index_to_full_flags (global_log_level);

  /* set the log level also on the spa_log */
  wp_spa_log_get_instance()->level = level_index_to_spa (global_log_level);

  if (flags & WP_INIT_SET_GLIB_LOG)
    g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  /* set PIPEWIRE_DEBUG and the spa_log interface that pipewire will use */
  if (flags & WP_INIT_SET_PW_LOG && !g_getenv ("WIREPLUMBER_NO_PW_LOG")) {
    /* always set PIPEWIRE_DEBUG for 2 reasons:
     * 1. to overwrite it from the environment, in case the user has set it
     * 2. to prevent pw_context from parsing "log.level" from the config file;
     *    we do this ourselves here and allows us to have more control over
     *    the whole process.
     */
    gchar lvl_str[2];
    g_snprintf (lvl_str, 2, "%d", wp_spa_log_get_instance ()->level);
    g_warn_if_fail (g_setenv ("PIPEWIRE_DEBUG", lvl_str, TRUE));
    pw_log_set_level (wp_spa_log_get_instance ()->level);
    pw_log_set (wp_spa_log_get_instance ());
  }
}

/* private, called from core.c when reading the config file */
void
wp_log_set_global_level (const gchar *log_level)
{
  gint level = level_index_from_string (log_level);
  log_state.global_log_level = level;
  log_state.global_log_level_flags = level_index_to_full_flags (level);
  wp_spa_log_get_instance()->level = level_index_to_spa (level);
  pw_log_set_level (level_index_to_spa (level));
}

static gint
find_topic_log_level (const gchar *log_topic, bool *has_custom_level)
{
  struct log_topic_pattern *pttrn = log_state.patterns;
  guint len;
  g_autofree gchar *reverse_topic = NULL;
  gint log_level = log_state.global_log_level;

  /* reverse string and length required for pattern match */
  len = strlen (log_topic);
  reverse_topic = g_strreverse (g_strndup (log_topic, len));

  while (pttrn && pttrn->spec &&
        !g_pattern_match (pttrn->spec, len, log_topic, reverse_topic))
    pttrn++;

  if (pttrn && pttrn->spec) {
    if (has_custom_level)
      *has_custom_level = true;
    log_level = pttrn->log_level;
  } else if (has_custom_level) {
    *has_custom_level = false;
  }

  return log_level;
}

/*!
 * \brief Initializes a log topic. Internal function, don't use it directly
 * \ingroup wplog
 */
void
wp_log_topic_init (WpLogTopic *topic)
{
  g_bit_lock (&topic->flags, 30);
  if ((topic->flags & (1u << 31)) == 0) {
    bool has_custom_level;
    gint log_level = find_topic_log_level (topic->topic_name, &has_custom_level);

    gint flags = topic->flags;
    flags |= level_index_to_full_flags (log_level);
    flags |= (1u << 31); /* initialized = true */
    if (has_custom_level)
      flags |= (1u << 29); /* has_custom_level = true */

    topic->global_flags = &log_state.global_log_level_flags;
    topic->flags = flags;
  }
  g_bit_unlock (&topic->flags, 30);
}

typedef struct _WpLogFields WpLogFields;
struct _WpLogFields
{
  const gchar *log_topic;
  const gchar *file;
  const gchar *line;
  const gchar *func;
  const gchar *message;
  gint log_level;
  GType object_type;
  gconstpointer object;
};

static void
wp_log_fields_init (WpLogFields *lf,
    const gchar *log_topic,
    gint log_level,
    const gchar *file,
    const gchar *line,
    const gchar *func,
    GType object_type,
    gconstpointer object,
    const gchar *message)
{
  lf->log_topic = log_topic ? log_topic : "default";
  lf->log_level = log_level;
  lf->file = file;
  lf->line = line;
  lf->func = func;
  lf->object_type = object_type;
  lf->object = object;
  lf->message = message ? message : "(null)";
}

static void
wp_log_fields_init_from_glib (WpLogFields *lf, GLogLevelFlags log_level_flags,
    const GLogField *fields, gsize n_fields)
{
  wp_log_fields_init (lf, NULL, level_index_from_flags (log_level_flags),
      NULL, NULL, NULL, 0, NULL, NULL);

  for (guint i = 0; i < n_fields; i++) {
    if (g_strcmp0 (fields[i].key, "GLIB_DOMAIN") == 0 && fields[i].value) {
      lf->log_topic = fields[i].value;
    }
    else if (g_strcmp0 (fields[i].key, "MESSAGE") == 0 && fields[i].value) {
      lf->message = fields[i].value;
    }
    else if (g_strcmp0 (fields[i].key, "CODE_FILE") == 0) {
      lf->file = fields[i].value;
    }
    else if (g_strcmp0 (fields[i].key, "CODE_LINE") == 0) {
      lf->line = fields[i].value;
    }
    else if (g_strcmp0 (fields[i].key, "CODE_FUNC") == 0) {
      lf->func = fields[i].value;
    }
  }
}

static void
wp_log_fields_write_to_stream (WpLogFields *lf, FILE *s)
{
  gint64 now;
  time_t now_secs;
  struct tm now_tm;
  gchar time_buf[128];

  now = g_get_real_time ();
  now_secs = (time_t) (now / G_USEC_PER_SEC);
  localtime_r (&now_secs, &now_tm);
  strftime (time_buf, sizeof (time_buf), "%H:%M:%S", &now_tm);

  fprintf (s, "%s%s %s.%06d %s%18.18s %s%s:%s:%s:%s %s\n",
      /* level */
      log_state.use_color ? log_level_info[lf->log_level].color : "",
      log_level_info[lf->log_level].name,
      /* timestamp */
      time_buf,
      (gint) (now % G_USEC_PER_SEC),
      /* domain */
      log_state.use_color ? DOMAIN_COLOR : "",
      lf->log_topic,
      /* file, line, function */
      log_state.use_color ? LOCATION_COLOR : "",
      lf->file,
      lf->line,
      lf->func,
      log_state.use_color ? RESET_COLOR : "",
      /* message */
      lf->message);
  fflush (s);
}

static gboolean
wp_log_fields_write_to_journal (WpLogFields *lf)
{
  gsize n_fields = 6;
  GLogField fields[6] = {
    { "PRIORITY", log_level_info[lf->log_level].priority, -1 },
    { "CODE_FILE", lf->file, -1 },
    { "CODE_LINE", lf->line, -1 },
    { "CODE_FUNC", lf->func, -1 },
    { "TOPIC", lf->log_topic, -1 },
    { "MESSAGE", lf->message, -1 },
  };

  /* the log level flags are not used in this function, so we can pass 0 */
  return (g_log_writer_journald (0, fields, n_fields, NULL) == G_LOG_WRITER_HANDLED);
}

static inline gchar *
wp_log_fields_format_message (WpLogFields *lf)
{
  g_autofree gchar *extra_message = NULL;
  g_autofree gchar *extra_object = NULL;
  const gchar *object_color = "";

  if (log_state.use_color) {
    guint h = g_direct_hash (lf->object) % G_N_ELEMENTS (object_colors);
    object_color = object_colors[h];
  }

  if (lf->object_type == WP_TYPE_SPA_POD && lf->object && !spa_dbg_str) {
    spa_dbg_str = g_string_new (lf->message);
    g_string_append (spa_dbg_str, ":\n");
    spa_debug_pod (2, NULL, wp_spa_pod_get_spa_pod (lf->object));
    extra_message = g_string_free (spa_dbg_str, FALSE);
    spa_dbg_str = NULL;
  }
  else if (lf->object && g_type_is_a (lf->object_type, WP_TYPE_PROXY) &&
      (wp_object_get_active_features ((WpObject *) lf->object) & WP_PROXY_FEATURE_BOUND)) {
    extra_object = g_strdup_printf (":%u:",
        wp_proxy_get_bound_id ((WpProxy *) lf->object));
  }

  return g_strdup_printf ("%s<%s%s%p>%s %s",
      object_color,
      lf->object_type != 0 ? g_type_name (lf->object_type) : "",
      extra_object ? extra_object : ":",
      lf->object,
      log_state.use_color ? RESET_COLOR : "",
      extra_message ? extra_message : lf->message);
}

static GLogWriterOutput
wp_log_fields_log (WpLogFields *lf)
{
  g_autofree gchar *full_message = NULL;

  /* in the unlikely event that someone messed with stderr... */
  if (G_UNLIKELY (!stderr || fileno (stderr) < 0))
    return G_LOG_WRITER_UNHANDLED;

  /* format the message to include the object */
  if (lf->object_type) {
    lf->message = full_message = wp_log_fields_format_message (lf);
  }

  /* write complete field information to the journal if we are logging to it */
  if (log_state.output_is_journal && wp_log_fields_write_to_journal (lf))
    return G_LOG_WRITER_HANDLED;

  wp_log_fields_write_to_stream (lf, stderr);
  return G_LOG_WRITER_HANDLED;
}

/*!
 * \brief WirePlumber's GLogWriterFunc
 *
 * This is installed automatically when you call wp_init() with
 * WP_INIT_SET_GLIB_LOG set in the flags
 * \ingroup wplog
 */
GLogWriterOutput
wp_log_writer_default (GLogLevelFlags log_level_flags,
    const GLogField *fields, gsize n_fields, gpointer user_data)
{
  WpLogFields lf = {0};

  g_return_val_if_fail (fields != NULL, G_LOG_WRITER_UNHANDLED);
  g_return_val_if_fail (n_fields > 0, G_LOG_WRITER_UNHANDLED);

  wp_log_fields_init_from_glib (&lf, log_level_flags, fields, n_fields);

  /* check if debug level & topic is enabled */
  if (lf.log_level > find_topic_log_level (lf.log_topic, NULL))
    return G_LOG_WRITER_HANDLED;

  return wp_log_fields_log (&lf);
}

/*!
 * \brief Used internally by the debug logging macros. Avoid using it directly.
 *
 * This assumes that the arguments are correct and that the log_topic is
 * enabled for the given log_level. No additional checks are performed.
 * \ingroup wplog
 */
void
wp_log_checked (
    const gchar *log_topic,
    GLogLevelFlags log_level_flags,
    const gchar *file,
    const gchar *line,
    const gchar *func,
    GType object_type,
    gconstpointer object,
    const gchar *message_format,
    ...)
{
  WpLogFields lf = {0};
  g_autofree gchar *message = NULL;
  va_list args;

  va_start (args, message_format);
  message = g_strdup_vprintf (message_format, args);
  va_end (args);

  wp_log_fields_init (&lf, log_topic, level_index_from_flags (log_level_flags),
      file, line, func, object_type, object, message);
  wp_log_fields_log (&lf);
}

static G_GNUC_PRINTF (7, 0) void
wp_spa_log_logtv (void *object,
    enum spa_log_level level,
    const struct spa_log_topic *topic,
    const char *file,
    int line,
    const char *func,
    const char *fmt,
    va_list args)
{
  WpLogFields lf = {0};
  gint log_level = level_index_from_spa (level);
  g_autofree gchar *message = NULL;
  gchar line_str[11];

  sprintf (line_str, "%d", line);
  message = g_strdup_vprintf (fmt, args);

  wp_log_fields_init (&lf, topic ? topic->topic : NULL, log_level,
      file, line_str, func, 0, NULL, message);
  wp_log_fields_log (&lf);
}

static G_GNUC_PRINTF (7, 8) void
wp_spa_log_logt (void *object,
    enum spa_log_level level,
    const struct spa_log_topic *topic,
    const char *file,
    int line,
    const char *func,
    const char *fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  wp_spa_log_logtv (object, level, topic, file, line, func, fmt, args);
  va_end (args);
}

static G_GNUC_PRINTF (6, 0) void
wp_spa_log_logv (void *object,
    enum spa_log_level level,
    const char *file,
    int line,
    const char *func,
    const char *fmt,
    va_list args)
{
  wp_spa_log_logtv (object, level, NULL, file, line, func, fmt, args);
}

static G_GNUC_PRINTF (6, 7) void
wp_spa_log_log (void *object,
    enum spa_log_level level,
    const char *file,
    int line,
    const char *func,
    const char *fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  wp_spa_log_logtv (object, level, NULL, file, line, func, fmt, args);
  va_end (args);
}

static void
wp_spa_log_topic_init (void *object, struct spa_log_topic *topic)
{
  gint log_level = find_topic_log_level (topic->topic, &topic->has_custom_level);
  topic->level = level_index_to_spa (log_level);
}

static const struct spa_log_methods wp_spa_log_methods = {
  SPA_VERSION_LOG_METHODS,
  .log = wp_spa_log_log,
  .logv = wp_spa_log_logv,
  .logt = wp_spa_log_logt,
  .logtv = wp_spa_log_logtv,
  .topic_init = wp_spa_log_topic_init,
};

static struct spa_log wp_spa_log = {
  .iface = { SPA_TYPE_INTERFACE_Log, SPA_VERSION_LOG, { &wp_spa_log_methods, NULL } },
  .level = SPA_LOG_LEVEL_WARN,
};

/*!
 * \brief Gets WirePlumber's instance of `spa_log`
 * \ingroup wplog
 * \returns WirePlumber's instance of `spa_log`, which can be used to redirect
 *   PipeWire's log messages to the currently installed GLogWriterFunc.
 *   This is installed automatically when you call wp_init() with
 *   WP_INIT_SET_PW_LOG set in the flags
 */
struct spa_log *
wp_spa_log_get_instance (void)
{
  return &wp_spa_log;
}
