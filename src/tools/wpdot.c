/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <locale.h>
#include <libintl.h>

#include <spa/utils/defs.h>
#include <spa/utils/string.h>
#include <pipewire/pipewire.h>
#include <pipewire/keys.h>

#include <wp/wp.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wpdot")

typedef struct _WpDot WpDot;
struct _WpDot
{
  GOptionContext *context;
  GMainLoop *loop;
  WpCore *core;
  WpObjectManager *om;
  guint pending_plugins;
  gint exit_code;
  gchar *output_file;
  gboolean show_pipewire_object_properties;
};

static void
wp_dot_clear (WpDot * self)
{
  g_clear_object (&self->om);
  g_clear_object (&self->core);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_clear_pointer (&self->context, g_option_context_free);
  g_clear_pointer (&self->output_file, g_free);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (WpDot, wp_dot_clear)

static void
async_quit (WpConnection * connection, GAsyncResult *res, WpDot *self)
{
  g_main_loop_quit (self->loop);
}

static gboolean
parse_output_file_arg (const gchar *option_name, const gchar *value,
    gpointer data, GError **error)
{
  WpDot *self = data;

  if (!self)
    return FALSE;

  g_free (self->output_file);
  self->output_file = g_strdup (value);
  return TRUE;
}

static gboolean
parse_show_pipewire_object_properties_arg (const gchar *option_name,
    const gchar *value, gpointer data, GError **error)
{
  WpDot *self = data;

  if (!self)
    return FALSE;

  self->show_pipewire_object_properties = TRUE;
  return TRUE;
}

static GOptionEntry entries[] =
{
  { "output", 'o', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK,
    parse_output_file_arg, "Write DOT output to FILE (default: stdout)",
    "FILE" },
  { "show-all-properties", 's', G_OPTION_FLAG_NO_ARG,
    G_OPTION_ARG_CALLBACK, parse_show_pipewire_object_properties_arg,
    "Show all object properties", NULL },
  { NULL }
};

typedef struct {
  gchar *link_group;
  GPtrArray *nodes;
} StreamDotGraphNodeGroup;

static guint32
get_proxy_bound_id_or_invalid (gpointer object)
{
  if (!object || !WP_IS_PROXY (object))
    return SPA_ID_INVALID;

  return wp_proxy_get_bound_id (WP_PROXY (object));
}

static void
fputs_html_escaped (FILE *out, const gchar *str)
{
  if (!str) {
    fputs ("(none)", out);
    return;
  }
  for (; *str; str++) {
    switch (*str) {
      case '<': fputs ("&lt;",   out); break;
      case '>': fputs ("&gt;",   out); break;
      case '&': fputs ("&amp;",  out); break;
      case '"': fputs ("&quot;", out); break;
      default:  fputc (*str, out);     break;
    }
  }
}

static void
fputs_dot_escaped (FILE *out, const gchar *str)
{
  if (!str) {
    fputs ("(none)", out);
    return;
  }
  for (; *str; str++) {
    if (*str == '"' || *str == '\\')
      fputc ('\\', out);
    fputc (*str, out);
  }
}

static void
fputs_html_escaped_truncated (FILE *out, const gchar *str, gsize max_chars)
{
  gsize count = 0;

  if (!str) {
    fputs ("(none)", out);
    return;
  }

  for (; *str && count < max_chars; str++, count++) {
    switch (*str) {
      case '<': fputs ("&lt;",   out); break;
      case '>': fputs ("&gt;",   out); break;
      case '&': fputs ("&amp;",  out); break;
      case '"': fputs ("&quot;", out); break;
      default:  fputc (*str, out);     break;
    }
  }

  if (*str)
    fputs ("...", out);
}

static void
stream_dot_graph_node_group_free (gpointer data)
{
  StreamDotGraphNodeGroup *group = data;

  if (!group)
    return;

  g_clear_pointer (&group->link_group, g_free);
  if (group->nodes)
    g_ptr_array_unref (group->nodes);
  g_free (group);
}

static void
print_link_row (FILE *out, WpLink *link)
{
  g_autoptr (WpProperties) link_props = NULL;
  const gchar *out_node;
  const gchar *out_port;
  const gchar *in_node;
  const gchar *in_port;
  guint32 link_id = get_proxy_bound_id_or_invalid (link);

  if (link_id == SPA_ID_INVALID)
    return;

  link_props = wp_global_proxy_get_global_properties (WP_GLOBAL_PROXY (link));
  out_node = wp_properties_get (link_props, PW_KEY_LINK_OUTPUT_NODE);
  out_port = wp_properties_get (link_props, PW_KEY_LINK_OUTPUT_PORT);
  in_node = wp_properties_get (link_props, PW_KEY_LINK_INPUT_NODE);
  in_port = wp_properties_get (link_props, PW_KEY_LINK_INPUT_PORT);

  fprintf (out,
      "            <TR><TD PORT=\"link_%u\" BGCOLOR=\"white\" ALIGN=\"LEFT\">"
      "link %u: (node:",
      link_id, link_id);
  fputs_html_escaped (out, out_node);
  fprintf (out, ", port:");
  fputs_html_escaped (out, out_port);
    fprintf (out, ") -&gt; (node:");
  fputs_html_escaped (out, in_node);
  fprintf (out, ", port:");
  fputs_html_escaped (out, in_port);
    fprintf (out, ")</TD></TR>\n");
}

static gboolean
get_link_port_ids (WpLink *link, guint32 *out_port_id, guint32 *in_port_id)
{
  g_autoptr (WpProperties) link_props = NULL;
  const gchar *value;

  link_props = wp_global_proxy_get_global_properties (WP_GLOBAL_PROXY (link));
  if (!link_props)
    return FALSE;

  value = wp_properties_get (link_props, PW_KEY_LINK_OUTPUT_PORT);
  if (!value || !spa_atou32 (value, out_port_id, 0))
    return FALSE;

  value = wp_properties_get (link_props, PW_KEY_LINK_INPUT_PORT);
  if (!value || !spa_atou32 (value, in_port_id, 0))
    return FALSE;

  return TRUE;
}

static gboolean
wp_dot_prepare (WpDot * self, GError ** error)
{
  wp_object_manager_add_interest (self->om, WP_TYPE_NODE, NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_NODE,
      WP_OBJECT_FEATURES_ALL);
  wp_object_manager_add_interest (self->om, WP_TYPE_STREAM, NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_STREAM,
      WP_OBJECT_FEATURES_ALL);
  wp_object_manager_add_interest (self->om, WP_TYPE_STREAM_LINK, NULL);
  wp_object_manager_request_object_features (self->om, WP_TYPE_STREAM_LINK,
      WP_OBJECT_FEATURES_ALL);
  return TRUE;
}

static void
wp_dot_emit_node (WpDot *self, FILE *out, WpNode *node)
{
  g_autoptr (WpProperties) node_props = NULL;
  g_autoptr (WpProperties) pw_node_props = NULL;
  g_autoptr (WpIterator) sit = NULL;
  g_auto (GValue) sval = G_VALUE_INIT;
  guint32 node_id;
  gboolean has_streams = FALSE;

  node_props = wp_global_proxy_get_global_properties (WP_GLOBAL_PROXY (node));
  if (self->show_pipewire_object_properties)
    pw_node_props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (node));
  node_id = get_proxy_bound_id_or_invalid (node);
  if (node_id == SPA_ID_INVALID)
    return;

  sit = wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_STREAM, NULL);
  for (; wp_iterator_next (sit, &sval); g_value_unset (&sval)) {
    WpStream *stream = g_value_get_object (&sval);
    g_autoptr (WpNode) snode = wp_stream_get_node (stream);
    if (snode == node) {
      has_streams = TRUE;
      g_value_unset (&sval);
      break;
    }
  }
  if (!has_streams)
    return;

  fprintf (out, "    subgraph cluster_%u {\n", node_id);
  fprintf (out, "        style=\"rounded,filled\";\n");
  fprintf (out, "        color=gray;\n");
  fprintf (out, "        fillcolor=\"#f3f8ff\";\n");
  fprintf (out, "        label=<\n");
  fprintf (out,
      "            <TABLE BORDER=\"0\" CELLBORDER=\"0\""
      " CELLSPACING=\"2\" CELLPADDING=\"2\">\n");
  fprintf (out, "            <TR><TD><B>Node %u</B></TD></TR>\n", node_id);
  {
    const gchar *node_name = node_props ?
        wp_properties_get (node_props, PW_KEY_NODE_NAME) : NULL;
    fprintf (out, "            <TR><TD>");
    fputs_html_escaped (out, node_name ? node_name : "");
    fprintf (out, "</TD></TR>\n");
  }
  if (self->show_pipewire_object_properties) {
    const struct spa_dict *dict =
        node_props ? wp_properties_peek_dict (node_props) : NULL;
    const struct spa_dict_item *item;
    if (dict) {
      fprintf (out,
          "            <TR><TD ALIGN=\"LEFT\"><B>Global Properties</B></TD></TR>\n");
      spa_dict_for_each (item, dict) {
        fprintf (out, "            <TR><TD ALIGN=\"LEFT\">");
        fputs_html_escaped (out, item->key);
        fprintf (out, " = ");
        fputs_html_escaped_truncated (out, item->value, 128);
        fprintf (out, "</TD></TR>\n");
      }
    }
  }
  if (self->show_pipewire_object_properties) {
    const struct spa_dict *dict =
        pw_node_props ? wp_properties_peek_dict (pw_node_props) : NULL;
    const struct spa_dict_item *item;

    if (dict) {
      fprintf (out,
          "            <TR><TD ALIGN=\"LEFT\"><B>PipeWire Object Properties</B></TD></TR>\n");
      spa_dict_for_each (item, dict) {
        fprintf (out, "            <TR><TD ALIGN=\"LEFT\">");
        fputs_html_escaped (out, item->key);
        fprintf (out, " = ");
        fputs_html_escaped_truncated (out, item->value, 64);
        fprintf (out, "</TD></TR>\n");
      }
    }
  }
  fprintf (out, "            </TABLE>>;\n");

  g_clear_pointer (&sit, wp_iterator_unref);
  sit = wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_STREAM, NULL);
  for (; wp_iterator_next (sit, &sval); g_value_unset (&sval)) {
    WpStream *stream = g_value_get_object (&sval);
    g_autoptr (WpNode) snode = wp_stream_get_node (stream);
    g_autoptr (WpIterator) pit = NULL;
    g_auto (GValue) pval = G_VALUE_INIT;
    const gchar *port_group;
    WpDirection dir;

    if (snode != node)
      continue;

    port_group = wp_stream_get_port_group (stream);
    dir = wp_stream_get_direction (stream);

    {
      const gchar *hdr_color =
          (dir == WP_DIRECTION_OUTPUT) ? "lightcoral" : "lightgreen";
      const gchar *dir_str =
          (dir == WP_DIRECTION_OUTPUT) ? "output" : "input";

      fprintf (out,
          "        stream_%u [shape=none margin=0 label=<\n"
          "            <TABLE BORDER=\"0\" CELLBORDER=\"1\""
          " CELLSPACING=\"0\" CELLPADDING=\"4\">\n"
          "            <TR><TD BGCOLOR=\"%s\"><B>Stream</B><BR/>"
          "port_group: ",
          wp_object_get_id (WP_OBJECT (stream)), hdr_color);
      fputs_html_escaped (out, port_group);
      fprintf (out, "<BR/>direction: %s</TD></TR>\n", dir_str);

      if (self->show_pipewire_object_properties) {
        g_autoptr (WpProperties) stream_props =
            wp_stream_get_properties (stream);
        const struct spa_dict *dict =
            stream_props ? wp_properties_peek_dict (stream_props) : NULL;
        const struct spa_dict_item *item;

        if (dict && dict->n_items > 0) {
          fprintf (out,
              "            <TR><TD BGCOLOR=\"#f5f5f5\" ALIGN=\"LEFT\">"
              "<B>Properties</B><BR ALIGN=\"LEFT\"/>");
          spa_dict_for_each (item, dict) {
            fputs_html_escaped (out, item->key);
            fprintf (out, " = ");
            fputs_html_escaped_truncated (out, item->value, 64);
            fprintf (out, "<BR ALIGN=\"LEFT\"/>");
          }
          fprintf (out, "</TD></TR>\n");
        }
      }

      pit = wp_stream_new_ports_iterator (stream);
      for (; wp_iterator_next (pit, &pval); g_value_unset (&pval)) {
        WpPort *port = g_value_get_object (&pval);
        guint32 port_id = get_proxy_bound_id_or_invalid (port);
        g_autoptr (WpProperties) port_props =
            wp_global_proxy_get_global_properties (WP_GLOBAL_PROXY (port));
        const gchar *port_name = port_props ?
            wp_properties_get (port_props, PW_KEY_PORT_NAME) : NULL;

        if (port_id == SPA_ID_INVALID)
          continue;

        fprintf (out,
            "            <TR><TD PORT=\"port_%u\" BGCOLOR=\"white\" ALIGN=\"LEFT\">"
            "port %u: ", port_id, port_id);
        fputs_html_escaped (out, port_name ? port_name : "");
        fprintf (out, " (%s)</TD></TR>\n",
            wp_port_get_direction (port) == WP_DIRECTION_INPUT ? "in" : "out");
      }

      g_clear_pointer (&pit, wp_iterator_unref);
      pit = wp_stream_new_monitor_ports_iterator (stream);
      for (; wp_iterator_next (pit, &pval); g_value_unset (&pval)) {
        WpPort *port = g_value_get_object (&pval);
        guint32 port_id = get_proxy_bound_id_or_invalid (port);
        g_autoptr (WpProperties) port_props =
            wp_global_proxy_get_global_properties (WP_GLOBAL_PROXY (port));
        const gchar *port_name = port_props ?
            wp_properties_get (port_props, PW_KEY_PORT_NAME) : NULL;

        if (port_id == SPA_ID_INVALID)
          continue;

        fprintf (out,
            "            <TR><TD PORT=\"port_%u\" BGCOLOR=\"lightyellow\" ALIGN=\"LEFT\">"
            "port %u: ", port_id, port_id);
        fputs_html_escaped (out, port_name ? port_name : "");
        fprintf (out, " (%s)</TD></TR>\n",
            wp_port_get_direction (port) == WP_DIRECTION_INPUT ? "in" : "out");
      }

      g_clear_pointer (&pit, wp_iterator_unref);
      pit = wp_stream_new_control_ports_iterator (stream);
      for (; wp_iterator_next (pit, &pval); g_value_unset (&pval)) {
        WpPort *port = g_value_get_object (&pval);
        guint32 port_id = get_proxy_bound_id_or_invalid (port);
        g_autoptr (WpProperties) port_props =
            wp_global_proxy_get_global_properties (WP_GLOBAL_PROXY (port));
        const gchar *port_name = port_props ?
            wp_properties_get (port_props, PW_KEY_PORT_NAME) : NULL;

        if (port_id == SPA_ID_INVALID)
          continue;

        fprintf (out,
            "            <TR><TD PORT=\"port_%u\" BGCOLOR=\"#ffe0ff\" ALIGN=\"LEFT\">"
            "port %u: ", port_id, port_id);
        fputs_html_escaped (out, port_name ? port_name : "");
        fprintf (out, " (%s, control)</TD></TR>\n",
            wp_port_get_direction (port) == WP_DIRECTION_INPUT ? "in" : "out");
      }

      fprintf (out, "            </TABLE>>];\n");
    }
  }

  fprintf (out, "    }\n\n");
}

static void
wp_dot_run (WpDot * self)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  g_autoptr (GPtrArray) node_groups = NULL;
  g_autoptr (GHashTable) node_groups_by_name = NULL;
  FILE *out = stdout;
  gboolean opened_file = FALSE;

  if (self->output_file) {
    out = fopen (self->output_file, "w");
    if (!out) {
      fprintf (stderr, "Error: cannot open output file '%s': %s\n",
          self->output_file, g_strerror (errno));
      self->exit_code = 1;
      wp_connection_sync (wp_core_get_connection (self->core),
          NULL, (GAsyncReadyCallback) async_quit, self);
      return;
    }
    opened_file = TRUE;
  }

  fprintf (out, "/* Generated by wpdot */\n");
  fprintf (out, "digraph stream_dot_graph {\n");
  fprintf (out, "    rankdir=LR;\n");
  fprintf (out, "    splines=line;\n");
  fprintf (out, "    node [style=filled];\n\n");

  node_groups = g_ptr_array_new_with_free_func (
      stream_dot_graph_node_group_free);
  node_groups_by_name = g_hash_table_new (g_str_hash, g_str_equal);

  it = wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_NODE, NULL);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpNode *node = g_value_get_object (&val);
    g_autoptr (WpProperties) node_props = NULL;
    const gchar *link_group;
    StreamDotGraphNodeGroup *group;

    node_props = wp_global_proxy_get_global_properties (WP_GLOBAL_PROXY (node));
    if (!node_props)
      continue;

    link_group = wp_pipewire_object_get_property (WP_PIPEWIRE_OBJECT (node),
        PW_KEY_NODE_LINK_GROUP);
    if (!link_group)
      link_group = "";

    group = g_hash_table_lookup (node_groups_by_name, link_group);
    if (!group) {
      group = g_new0 (StreamDotGraphNodeGroup, 1);
      group->link_group = g_strdup (link_group);
      group->nodes = g_ptr_array_new_with_free_func (g_object_unref);
      g_ptr_array_add (node_groups, group);
      g_hash_table_insert (node_groups_by_name, group->link_group, group);
    }
    g_ptr_array_add (group->nodes, g_object_ref (node));
  }

  for (guint i = 0; i < node_groups->len; i++) {
    StreamDotGraphNodeGroup *group = g_ptr_array_index (node_groups, i);

    if (!group || !group->nodes || group->nodes->len == 0)
      continue;

    if (group->link_group && *group->link_group != '\0') {
      fprintf (out, "    subgraph cluster_link_group_%u {\n", i);
      fprintf (out, "        style=\"rounded,filled\";\n");
      fprintf (out, "        color=gray;\n");
      fprintf (out, "        fillcolor=\"#ededed\";\n");
      fprintf (out, "        penwidth=2;\n");
      fprintf (out, "        label=\"link-group: ");
      fputs_dot_escaped (out, group->link_group);
      fprintf (out, "\";\n");
    }

    for (guint n = 0; n < group->nodes->len; n++)
      wp_dot_emit_node (self, out, g_ptr_array_index (group->nodes, n));

    if (group->link_group && *group->link_group != '\0')
      fprintf (out, "    }\n\n");
  }

  g_clear_pointer (&it, wp_iterator_unref);
  it = wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_STREAM_LINK,
      NULL);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpStreamLink *stream_link = g_value_get_object (&val);
    g_autoptr (WpStream) output = NULL;
    g_autoptr (WpStream) input = NULL;
    g_autoptr (WpIterator) lit = NULL;
    g_auto (GValue) lval = G_VALUE_INIT;
    gboolean is_monitor = FALSE;
    guint32 stream_link_id = wp_object_get_id (WP_OBJECT (stream_link));

    output = wp_stream_link_get_output_stream (stream_link, &is_monitor);
    input = wp_stream_link_get_input_stream (stream_link);
    if (!output || !input)
      continue;

    fprintf (out, "    streamlink_%u [shape=none margin=0 label=<\n",
        stream_link_id);
    fprintf (out,
        "            <TABLE BORDER=\"0\" CELLBORDER=\"1\""
        " CELLSPACING=\"0\" CELLPADDING=\"4\">\n"
        "            <TR><TD BGCOLOR=\"#ffdca8\"><B>StreamLink</B></TD></TR>\n");

    lit = wp_stream_link_new_links_iterator (stream_link);
    for (; wp_iterator_next (lit, &lval); g_value_unset (&lval)) {
      WpLink *link = g_value_get_object (&lval);
      print_link_row (out, link);
    }

    fprintf (out, "            </TABLE>>];\n\n");
  }

  g_clear_pointer (&it, wp_iterator_unref);
  it = wp_object_manager_new_filtered_iterator (self->om, WP_TYPE_STREAM_LINK,
      NULL);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpStreamLink *sl = g_value_get_object (&val);
    g_autoptr (WpStream) output = NULL;
    g_autoptr (WpStream) input = NULL;
    g_autoptr (WpIterator) lit = NULL;
    g_auto (GValue) lval = G_VALUE_INIT;
    gboolean is_monitor = FALSE;
    guint32 stream_link_id = wp_object_get_id (WP_OBJECT (sl));

    output = wp_stream_link_get_output_stream (sl, &is_monitor);
    input = wp_stream_link_get_input_stream (sl);
    if (!output || !input)
      continue;

    lit = wp_stream_link_new_links_iterator (sl);
    for (; wp_iterator_next (lit, &lval); g_value_unset (&lval)) {
      WpLink *link = g_value_get_object (&lval);
      guint32 link_id = get_proxy_bound_id_or_invalid (link);
      guint32 out_port_id = SPA_ID_INVALID;
      guint32 in_port_id = SPA_ID_INVALID;

      if (link_id == SPA_ID_INVALID)
        continue;

      if (!get_link_port_ids (link, &out_port_id, &in_port_id))
        continue;

      fprintf (out,
          "    stream_%u:port_%u -> streamlink_%u:link_%u [color=mediumpurple1%s];\n",
          wp_object_get_id (WP_OBJECT (output)), out_port_id, stream_link_id,
          link_id, is_monitor ? " style=dashed" : "");
      fprintf (out,
          "    streamlink_%u:link_%u -> stream_%u:port_%u [color=mediumpurple1%s];\n",
          stream_link_id, link_id, wp_object_get_id (WP_OBJECT (input)),
          in_port_id, is_monitor ? " style=dashed" : "");
    }
  }

  fprintf (out, "}\n");

  if (opened_file) {
    fclose (out);
    fprintf (stdout, "Graph written to %s\n", self->output_file);
  }

  wp_connection_sync (wp_core_get_connection (self->core),
      NULL, (GAsyncReadyCallback) async_quit, self);
}

static void
on_singleton_activated (WpObject *o, GAsyncResult *res, WpDot *dot)
{
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (o, res, &error)) {
    fprintf (stderr, "%s\n", error->message);
    dot->exit_code = 1;
    g_main_loop_quit (dot->loop);
    return;
  }

  wp_registry_register_object (wp_core_get_registry (dot->core),
      g_object_ref (o));

  if (--dot->pending_plugins == 0)
    wp_registry_install_object_manager (wp_core_get_registry (dot->core),
        dot->om);
}

static void
on_stream_manager_activated (WpObject *o, GAsyncResult *res, WpDot *dot)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (WpStreamLinkManager) stream_link_manager = NULL;

  if (!wp_object_activate_finish (o, res, &error)) {
    fprintf (stderr, "%s\n", error->message);
    dot->exit_code = 1;
    g_main_loop_quit (dot->loop);
    return;
  }

  wp_registry_register_object (wp_core_get_registry (dot->core),
      g_object_ref (o));

  --dot->pending_plugins;

  stream_link_manager = wp_stream_link_manager_new (dot->core, NULL);
  wp_object_activate (WP_OBJECT (stream_link_manager),
      WP_OBJECT_FEATURES_ALL,
      NULL,
      (GAsyncReadyCallback)on_singleton_activated,
      dot);
}

static void
on_core_activated (WpObject * core, GAsyncResult * res, WpDot * dot)
{
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (core, res, &error)) {
    fprintf (stderr, "%s\n", error->message);
    dot->exit_code = 2;
    g_main_loop_quit (dot->loop);
  }
}

gint
main (gint argc, gchar **argv)
{
  g_auto (WpDot) dot = {0};
  g_autoptr (GError) error = NULL;
  g_autoptr (WpStreamManager) stream_manager = NULL;
  GOptionGroup *group = NULL;

  setlocale (LC_ALL, "");
  setlocale (LC_NUMERIC, "C");
  wp_init (WP_INIT_ALL);

  dot.context = g_option_context_new (
      "- Generate a DOT graph including stream and stream-link objects");
  group = g_option_group_new ("wpdot", NULL, NULL, &dot, NULL);
  g_option_group_add_entries (group, entries);
  g_option_context_set_main_group (dot.context, group);

  if (!g_option_context_parse (dot.context, &argc, &argv, &error)) {
    fprintf (stderr, "Error: %s\n", error->message);
    return 1;
  }

  if (argc > 1) {
    g_autofree gchar *help = g_option_context_get_help (dot.context, TRUE, NULL);
    fprintf (stderr, "Error: unexpected argument '%s'\n\n", argv[1]);
    fprintf (stderr, "%s", help);
    return 1;
  }

  dot.loop = g_main_loop_new (NULL, FALSE);
  dot.core = wp_core_new (NULL, NULL, wp_properties_new (PW_KEY_APP_NAME,
      "wpdot",
      PW_KEY_REMOTE_NAME,
      ("[" PW_DEFAULT_REMOTE "-manager," PW_DEFAULT_REMOTE "]"),
      NULL));
  dot.om = wp_object_manager_new ();

  if (!wp_dot_prepare (&dot, &error)) {
    fprintf (stderr, "%s\n", error->message);
    return 1;
  }

  dot.pending_plugins = 2;

  stream_manager = wp_stream_manager_new (dot.core);
  wp_object_activate (WP_OBJECT (stream_manager),
      WP_OBJECT_FEATURES_ALL,
      NULL,
      (GAsyncReadyCallback)on_stream_manager_activated,
      &dot);

  wp_object_activate (WP_OBJECT (dot.core), WP_CORE_FEATURE_CONNECTED, NULL,
      (GAsyncReadyCallback) on_core_activated, &dot);

  g_signal_connect_swapped (wp_core_get_connection (dot.core), "disconnected",
      (GCallback) g_main_loop_quit, dot.loop);
  g_signal_connect_swapped (dot.om, "installed",
      (GCallback) wp_dot_run, &dot);

  g_main_loop_run (dot.loop);
  return dot.exit_code;
}