/* WirePlumber
 *
 * SPDX-FileCopyrightText: Copyright © 2026 The WirePlumber Developers
 *
 * @author Lawrence Iviani <lawrence.iviani@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <wp/wp.h>

#define DEFAULT_NAME "wireplumber.conf"
#define OVERRIDE_SECTION_PREFIX "override."

typedef struct {
  const gchar *name;
  gboolean recurse;
  gboolean newline;
  gboolean colors;
  gboolean spa;
} Options;

static void
show_help (const gchar *name, gboolean error)
{
  FILE *out = error ? stderr : stdout;

  fprintf (out,
      "%1$s: WirePlumber configuration manager.\n"
      "Usage:\n"
      "  %1$s [options] paths                  List config paths (default action)\n"
      "  %1$s [options] list [SECTION]         List config sections\n"
      "  %1$s [options] merge [SECTION]        Merge config sections\n\n"
      "Options:\n"
      "  -h, --help                            Show this help\n"
      "  -V, --version                         Show version\n"
      "  -n, --name=NAME                       Config name (default '%2$s')\n"
      "  -L, --no-newline                      Omit newlines after values\n"
      "  -r, --recurse                         Reformat config sections recursively\n"
      "  -N, --no-colors                       Disable color output\n"
      "  -C, --color[=WHEN]                    Enable colors: auto, always, or never\n"
      "  -s, --spa                             Use SPA JSON output\n",
      name, DEFAULT_NAME);
}

static void
show_version (const gchar *name)
{
  printf ("%s\n"
      "Compiled with libwireplumber %s\n"
      "Linked with libwireplumber %s\n",
      name, WIREPLUMBER_VERSION, wp_get_library_version ());
}

static void
print_color (const Options *opts, const gchar *color)
{
  if (opts->colors)
    fputs (color, stdout);
}

static void
print_indent (const Options *opts, guint depth)
{
  if (opts->newline)
    fprintf (stdout, "%*s", (int) depth * 2, "");
}

static gboolean
is_simple_spa_string (const gchar *str)
{
  g_autoptr (WpSpaJson) candidate = NULL;

  if (!str[0])
    return FALSE;

  candidate = wp_spa_json_new_wrap_string (str);
  if (wp_spa_json_is_container (candidate) ||
      wp_spa_json_is_boolean (candidate) || wp_spa_json_is_null (candidate) ||
      wp_spa_json_is_int (candidate) || wp_spa_json_is_float (candidate))
    return FALSE;

  for (const gchar *c = str; *c; c++) {
    if (!g_ascii_isalnum (*c) && *c != '_' && *c != '-' && *c != '.' &&
        *c != ':' && *c != '/')
      return FALSE;
  }
  return TRUE;
}

static void
print_quoted_string (const gchar *str)
{
  fputc ('"', stdout);
  for (const guchar *c = (const guchar *) str; *c; c++) {
    switch (*c) {
      case '"':
        fputs ("\\\"", stdout);
        break;
      case '\\':
        fputs ("\\\\", stdout);
        break;
      case '\b':
        fputs ("\\b", stdout);
        break;
      case '\f':
        fputs ("\\f", stdout);
        break;
      case '\n':
        fputs ("\\n", stdout);
        break;
      case '\r':
        fputs ("\\r", stdout);
        break;
      case '\t':
        fputs ("\\t", stdout);
        break;
      default:
        if (*c < 0x20)
          fprintf (stdout, "\\u%04x", *c);
        else
          fputc (*c, stdout);
        break;
    }
  }
  fputc ('"', stdout);
}

static void
print_string (const gchar *str, const Options *opts, gboolean key)
{
  print_color (opts, key ? "\033[1;34m" : "\033[1;32m");
  if (opts->spa && is_simple_spa_string (str))
    fputs (str, stdout);
  else
    print_quoted_string (str);
  print_color (opts, "\033[0m");
}

static gboolean print_json_value (WpSpaJson *json, const Options *opts,
    guint depth, gboolean expand_container);

static gboolean
print_json_object (WpSpaJson *json, const Options *opts, guint depth)
{
  g_autoptr (WpSpaJsonParser) parser = wp_spa_json_parser_new_object (json);
  gboolean first = TRUE;

  print_color (opts, "\033[1;33m");
  fputc ('{', stdout);
  print_color (opts, "\033[0m");

  while (TRUE) {
    g_autoptr (WpSpaJson) key_json = wp_spa_json_parser_get_json (parser);
    g_autoptr (WpSpaJson) value = NULL;
    g_autofree gchar *key = NULL;

    if (!key_json)
      break;
    value = wp_spa_json_parser_get_json (parser);
    if (!value)
      return FALSE;

    if (!first) {
      if (!opts->spa)
        fputc (',', stdout);
      if (!opts->newline)
        fputc (' ', stdout);
    }
    if (opts->newline) {
      fputc ('\n', stdout);
      print_indent (opts, depth + 1);
    }

    key = wp_spa_json_parse_string (key_json);
    print_string (key, opts, TRUE);
    fputs (opts->spa ? " = " : ": ", stdout);
    if (!print_json_value (value, opts, depth + 1, opts->recurse))
      return FALSE;
    first = FALSE;
  }

  if (!first && opts->newline) {
    fputc ('\n', stdout);
    print_indent (opts, depth);
  }
  print_color (opts, "\033[1;33m");
  fputc ('}', stdout);
  print_color (opts, "\033[0m");
  return TRUE;
}

static gboolean
print_json_array (WpSpaJson *json, const Options *opts, guint depth)
{
  g_autoptr (WpSpaJsonParser) parser = wp_spa_json_parser_new_array (json);
  gboolean first = TRUE;

  print_color (opts, "\033[1;33m");
  fputc ('[', stdout);
  print_color (opts, "\033[0m");

  while (TRUE) {
    g_autoptr (WpSpaJson) value = wp_spa_json_parser_get_json (parser);
    if (!value)
      break;

    if (!first) {
      if (!opts->spa)
        fputc (',', stdout);
      if (!opts->newline)
        fputc (' ', stdout);
    }
    if (opts->newline) {
      fputc ('\n', stdout);
      print_indent (opts, depth + 1);
    }

    if (!print_json_value (value, opts, depth + 1, opts->recurse))
      return FALSE;
    first = FALSE;
  }

  if (!first && opts->newline) {
    fputc ('\n', stdout);
    print_indent (opts, depth);
  }
  print_color (opts, "\033[1;33m");
  fputc (']', stdout);
  print_color (opts, "\033[0m");
  return TRUE;
}

static gboolean
print_json_value (WpSpaJson *json, const Options *opts, guint depth,
    gboolean expand_container)
{
  if (expand_container && wp_spa_json_is_object (json))
    return print_json_object (json, opts, depth);
  if (expand_container && wp_spa_json_is_array (json))
    return print_json_array (json, opts, depth);

  if (!wp_spa_json_is_container (json) && !wp_spa_json_is_boolean (json) &&
      !wp_spa_json_is_null (json) && !wp_spa_json_is_int (json) &&
      !wp_spa_json_is_float (json)) {
    g_autofree gchar *value = wp_spa_json_parse_string (json);
    print_string (value, opts, FALSE);
  } else {
    if (wp_spa_json_is_boolean (json) || wp_spa_json_is_null (json))
      print_color (opts, "\033[1;35m");
    else if (wp_spa_json_is_int (json) || wp_spa_json_is_float (json))
      print_color (opts, "\033[1;36m");
    else
      print_color (opts, "\033[1;33m");

    fwrite (wp_spa_json_get_data (json), 1, wp_spa_json_get_size (json), stdout);
    print_color (opts, "\033[0m");
  }

  return TRUE;
}

static gboolean
print_json (WpSpaJson *json, const Options *opts)
{
  if (!print_json_value (json, opts, 0, TRUE))
    return FALSE;
  fputc ('\n', stdout);
  return !ferror (stdout);
}

static gboolean
json_get_string_member (WpSpaJson *json, const gchar *member, gchar **value)
{
  return wp_spa_json_object_get (json, member, "s", value, NULL);
}

static int
do_paths (WpConf *conf)
{
  g_autoptr (WpIterator) it = wp_conf_new_locations_iterator (conf);
  g_auto (GValue) item = G_VALUE_INIT;

  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *json = g_value_get_boxed (&item);
    g_autofree gchar *path = NULL;

    if (!json_get_string_member (json, "section.location", &path))
      return 1;
    puts (path);
  }

  return ferror (stdout) ? 1 : 0;
}

static const gchar *
logical_section_name (const gchar *name)
{
  return g_str_has_prefix (name, OVERRIDE_SECTION_PREFIX) ?
      name + strlen (OVERRIDE_SECTION_PREFIX) : name;
}

static int
do_list (WpConf *conf, const gchar *filter, const Options *opts)
{
  g_autoptr (WpIterator) it =
      wp_conf_new_sections_iterator (conf, WP_CONF_SECTION_MODE_ALL);
  g_autoptr (WpSpaJsonBuilder) builder = wp_spa_json_builder_new_array ();
  g_autoptr (WpSpaJson) output = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *json = g_value_get_boxed (&item);
    g_autofree gchar *name = NULL;

    if (!json_get_string_member (json, "section.name", &name))
      return 1;
    if (!filter || g_str_equal (logical_section_name (name), filter))
      wp_spa_json_builder_add_json (builder, json);
  }

  output = wp_spa_json_builder_end (builder);
  return print_json (output, opts) ? 0 : 1;
}

static int
do_merge_one (WpConf *conf, const gchar *section, const Options *opts)
{
  g_autoptr (WpSpaJson) value = wp_conf_get_section (conf, section);
  g_autoptr (WpSpaJson) empty = NULL;

  if (!value) {
    empty = wp_spa_json_new_object (NULL, NULL, NULL);
    value = g_steal_pointer (&empty);
  }

  return print_json (value, opts) ? 0 : 1;
}

static int
do_merge_all (WpConf *conf, const Options *opts)
{
  g_autoptr (WpIterator) it =
      wp_conf_new_sections_iterator (conf, WP_CONF_SECTION_MODE_MERGED);
  g_autoptr (WpSpaJsonBuilder) builder = wp_spa_json_builder_new_object ();
  g_autoptr (WpSpaJson) output = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *json = g_value_get_boxed (&item);
    g_autofree gchar *name = NULL;
    g_autoptr (WpSpaJson) value = NULL;

    if (!json_get_string_member (json, "section.name", &name))
      return 1;

    value = wp_conf_get_section (conf, name);
    if (value) {
      wp_spa_json_builder_add_property (builder, name);
      wp_spa_json_builder_add_json (builder, value);
    }
  }

  output = wp_spa_json_builder_end (builder);
  return print_json (output, opts) ? 0 : 1;
}

int
main (int argc, char *argv[])
{
  Options opts = {
    .name = DEFAULT_NAME,
    .newline = TRUE,
    .colors = g_getenv ("NO_COLOR") == NULL && isatty (fileno (stdout)),
  };
  const gchar *command = "paths";
  const gchar *section = NULL;
  g_autoptr (WpConf) conf = NULL;
  g_autoptr (GError) error = NULL;
  int c;
  static const struct option long_options[] = {
    { "help", no_argument, NULL, 'h' },
    { "version", no_argument, NULL, 'V' },
    { "name", required_argument, NULL, 'n' },
    { "no-newline", no_argument, NULL, 'L' },
    { "recurse", no_argument, NULL, 'r' },
    { "no-colors", no_argument, NULL, 'N' },
    { "color", optional_argument, NULL, 'C' },
    { "spa", no_argument, NULL, 's' },
    { NULL, 0, NULL, 0 },
  };

  while ((c = getopt_long (argc, argv, "hVn:LrNC::s",
          long_options, NULL)) != -1) {
    switch (c) {
      case 'h':
        show_help (argv[0], FALSE);
        return 0;
      case 'V':
        show_version (argv[0]);
        return 0;
      case 'n':
        opts.name = optarg;
        break;
      case 'L':
        opts.newline = FALSE;
        break;
      case 'r':
        opts.recurse = TRUE;
        break;
      case 'N':
        opts.colors = FALSE;
        break;
      case 'C':
        if (!optarg || g_str_equal (optarg, "auto")) {
          /* Automatic terminal detection was done before parsing options */
        } else if (g_str_equal (optarg, "always")) {
          opts.colors = TRUE;
        } else if (g_str_equal (optarg, "never")) {
          opts.colors = FALSE;
        } else {
          fprintf (stderr, "Unknown color: %s\n", optarg);
          show_help (argv[0], TRUE);
          return 2;
        }
        break;
      case 's':
        opts.spa = TRUE;
        break;
      default:
        show_help (argv[0], TRUE);
        return 2;
    }
  }

  if (optind < argc)
    command = argv[optind++];

  if (g_str_equal (command, "paths")) {
    if (optind != argc) {
      fprintf (stderr, "%s accepts no section\n", command);
      show_help (argv[0], TRUE);
      return 2;
    }
  } else if (g_str_equal (command, "list") ||
      g_str_equal (command, "merge")) {
    if (optind < argc)
      section = argv[optind++];
    if (optind != argc) {
      fprintf (stderr, "%s accepts at most one section\n", command);
      show_help (argv[0], TRUE);
      return 2;
    }
  } else {
    fprintf (stderr, "Unknown command: %s\n", command);
    show_help (argv[0], TRUE);
    return 2;
  }

  wp_init (WP_INIT_ALL);
  if (!g_getenv ("WIREPLUMBER_DEBUG"))
    wp_log_set_level ("W");
  conf = wp_conf_new_open (opts.name, NULL, &error);
  if (!conf) {
    fprintf (stderr, "error loading config: %s\n", error->message);
    return 1;
  }

  if (g_str_equal (command, "paths"))
    return do_paths (conf);
  if (g_str_equal (command, "list"))
    return do_list (conf, section, &opts);
  if (section)
    return do_merge_one (conf, section, &opts);
  return do_merge_all (conf, &opts);
}
