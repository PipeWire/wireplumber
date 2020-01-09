/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <pipewire/pipewire.h>
#include <pipewire/impl.h>

static GOptionEntry entries[] =
{
  { NULL }
};

#define WP_DOMAIN_DAEMON (wp_domain_daemon_quark ())
static G_DEFINE_QUARK (wireplumber-daemon, wp_domain_daemon);

enum WpExitCode
{
  WP_CODE_DISCONNECTED = 0,
  WP_CODE_INTERRUPTED,
  WP_CODE_OPERATION_FAILED,
  WP_CODE_INVALID_ARGUMENT,
};

struct WpDaemonData
{
  WpCore *core;
  GMainLoop *loop;

  gint exit_code;
  gchar *exit_message;
  GDestroyNotify free_message;
};

static void
daemon_exit (struct WpDaemonData * d, gint code, const gchar *format, ...)
{
  va_list args;
  va_start (args, format);
  d->exit_code = code;
  d->exit_message = g_strdup_vprintf (format, args);
  d->free_message = g_free;
  va_end (args);
  g_main_loop_quit (d->loop);
}

static void
daemon_exit_static_str (struct WpDaemonData * d, gint code, const gchar *str)
{
  d->exit_code = code;
  d->exit_message = (gchar *) str;
  d->free_message = NULL;
  g_main_loop_quit (d->loop);
}

static gboolean
signal_handler (gpointer data)
{
  struct WpDaemonData *d = data;
  daemon_exit_static_str (d, WP_CODE_INTERRUPTED, "interrupted by signal");
  return G_SOURCE_CONTINUE;
}

static void
on_disconnected (WpCore *core, struct WpDaemonData * d)
{
  /* something else triggered the exit; we will certainly get a state
   * change while destroying the remote, but let's not change the message */
  if (d->exit_message)
    return;

  daemon_exit_static_str (d, WP_CODE_DISCONNECTED,
      "disconnected from pipewire");
}

static gboolean
parse_commands_file (struct WpDaemonData *d, GInputStream * stream,
    GError ** error)
{
  gchar buffer[4096];
  gssize bytes_read;
  gchar *cur, *linestart, *saveptr;
  gchar *cmd;
  gint lineno = 1, block_lines = 1, in_block = 0;
  gboolean eof = FALSE;
  GVariant *properties;

  linestart = cur = buffer;

  do {
    bytes_read = g_input_stream_read (stream, cur, sizeof (buffer), NULL, error);
    if (bytes_read < 0)
      return FALSE;
    else if (bytes_read == 0) {
      eof = TRUE;
      /* terminate the remaining data, so that we consume it all */
      if (cur != linestart) {
        *cur = '\n';
      }
    }

    bytes_read += (cur - linestart);

    while (cur - buffer < bytes_read) {
      /* advance cur to the end of the line that is at the end of the block */
      while (cur - buffer < bytes_read && (in_block || *cur != '\n')) {
        switch (*cur) {
          case '{':
            in_block++;
            break;
          case '}':
            in_block--;
            break;
          case '\n':  // found a newline inside a block
            block_lines++;
            break;
          default:
            break;
        }
        cur++;
      }

      if (*cur == '\n') {
        /* found the end of a line */
        *cur = '\0';

        /* tokenize and execute */
        cmd = strtok_r (linestart, " ", &saveptr);

        if (!cmd || cmd[0] == '#') {
          /* empty line or comment, skip */
        } else if (!g_strcmp0 (cmd, "load-module")) {
          gchar *abi, *module, *props;

          abi = strtok_r (NULL, " ", &saveptr);
          module = strtok_r (NULL, " ", &saveptr);

          if (!abi || !module ||
              (abi && abi[0] == '{') || (module && module[0] == '{'))
          {
            g_set_error (error, WP_DOMAIN_DAEMON, WP_CODE_INVALID_ARGUMENT,
                "expected ABI and MODULE at line %i", lineno);
            return FALSE;
          }

          /* if there are remaining characters after the module name,
             treat it as a serialized GVariant for the properties */
          props = module + strlen(module) + 1;
          if (cur - props > 0) {
            g_autoptr (GError) tmperr = NULL;
            g_autofree gchar *context = NULL;

            properties = g_variant_parse (G_VARIANT_TYPE_VARDICT, props, cur,
                NULL, &tmperr);
            if (!properties) {
              context = g_variant_parse_error_print_context (tmperr, props);
              g_set_error (error, WP_DOMAIN_DAEMON, WP_CODE_INVALID_ARGUMENT,
                  "GVariant parse error after line %i:\n%s", lineno, context);
              return FALSE;
            }
          } else {
            properties = g_variant_new_parsed ("@a{sv} {}");
          }

          if (!wp_module_load (d->core, abi, module, properties, error)) {
            return FALSE;
          }
        } else if (!g_strcmp0 (cmd, "load-pipewire-module")) {
          gchar *module, *props;

          module = strtok_r (NULL, " ", &saveptr);
          props = module + strlen(module) + 1;

          if (!pw_context_load_module (wp_core_get_pw_context (d->core), module,
                  props, NULL)) {
            g_set_error (error, WP_DOMAIN_DAEMON, WP_CODE_OPERATION_FAILED,
                "failed to load pipewire module '%s': %s", module,
                g_strerror (errno));
            return FALSE;
          }
        } else if (!g_strcmp0 (cmd, "add-spa-lib")) {
          gchar *regex, *lib;
          gint ret;

          regex = strtok_r (NULL, " ", &saveptr);
          lib = strtok_r (NULL, " ", &saveptr);

          if (!regex || !lib ||
              (regex && regex[0] == '{') || (lib && lib[0] == '{'))
          {
            g_set_error (error, WP_DOMAIN_DAEMON, WP_CODE_INVALID_ARGUMENT,
                "expected REGEX and LIB at line %i", lineno);
            return FALSE;
          }

          ret = pw_context_add_spa_lib (wp_core_get_pw_context (d->core), regex,
              lib);
          if (ret < 0) {
            g_set_error (error, WP_DOMAIN_DAEMON, WP_CODE_OPERATION_FAILED,
                "failed to add spa lib ('%s' on '%s'): %s", regex, lib,
                g_strerror (-ret));
            return FALSE;
          }
        } else {
          g_set_error (error, WP_DOMAIN_DAEMON, WP_CODE_INVALID_ARGUMENT,
              "unknown command '%s' at line %i", cmd, lineno);
          return FALSE;
        }

        /* continue with the next line */
        linestart = ++cur;
        lineno += block_lines;
        block_lines = 1;
      }
    }

    /* reached the end of the data that was read */

    if (cur - linestart >= sizeof (buffer)) {
      g_set_error (error, WP_DOMAIN_DAEMON, WP_CODE_OPERATION_FAILED,
          "line %i exceeds the maximum allowed line size (%d bytes)",
          lineno, (gint) sizeof (buffer));
      return FALSE;
    } else if (cur - linestart > 0) {
      /* we have unparsed data, move it to the
       * beginning of the buffer and continue */
      strncpy (buffer, linestart, cur - linestart);
      linestart = buffer;
      cur = buffer + (cur - linestart);
    }
  } while (!eof);

  return TRUE;
}

static gboolean
load_commands_file (struct WpDaemonData *d)
{
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GFileInputStream) istream = NULL;
  const gchar *filename;

  filename = g_getenv ("WIREPLUMBER_CONFIG_FILE");
  if (!filename)
    filename = WIREPLUMBER_DEFAULT_CONFIG_FILE;

  file = g_file_new_for_path (filename);
  istream = g_file_read (file, NULL, &error);
  if (!istream) {
    daemon_exit (d, WP_CODE_INVALID_ARGUMENT, "%s", error->message);
    return G_SOURCE_REMOVE;
  }

  if (!parse_commands_file (d, G_INPUT_STREAM (istream), &error)) {
    daemon_exit (d, error->code, "Failed to read '%s': %s", filename,
        error->message);
    return G_SOURCE_REMOVE;
  }

  /* connect to pipewire */
  if (!wp_core_connect (d->core))
    daemon_exit_static_str (d, WP_CODE_DISCONNECTED, "failed to connect");

  return G_SOURCE_REMOVE;
}

gint
main (gint argc, gchar **argv)
{
  struct WpDaemonData data = {0};
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpConfiguration) config = NULL;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  const gchar *configuration_path;

  context = g_option_context_new ("- PipeWire Session/Policy Manager");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    data.exit_message = error->message;
    data.exit_code = WP_CODE_INVALID_ARGUMENT;
    goto out;
  }

  /* init wireplumber */

  data.core = core = wp_core_new (NULL, NULL);
  g_signal_connect (core, "disconnected", (GCallback) on_disconnected, &data);

  /* init configuration */

  configuration_path = g_getenv ("WIREPLUMBER_CONFIG_DIR");
  if (!configuration_path)
    configuration_path = WIREPLUMBER_DEFAULT_CONFIG_DIR;
  config = wp_configuration_get_instance (core);
  wp_configuration_add_path (config, configuration_path);

  /* init main loop */

  data.loop = loop = g_main_loop_new (NULL, FALSE);

  /* watch for exit signals */

  g_unix_signal_add (SIGINT, signal_handler, &data);
  g_unix_signal_add (SIGTERM, signal_handler, &data);
  g_unix_signal_add (SIGHUP, signal_handler, &data);

  /* run */

  g_idle_add ((GSourceFunc) load_commands_file, &data);
  g_main_loop_run (data.loop);

out:
  if (data.exit_message) {
    g_message ("%s", data.exit_message);
    if (data.free_message)
      data.free_message (data.exit_message);
  }
  return data.exit_code;
}
