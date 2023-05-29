/* WirePlumber
 *
 * Copyright © 2023 Collabora Ltd.
 *    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <stdio.h>
#include <locale.h>
#include <spa/utils/defs.h>
#include <pipewire/keys.h>
#include <pipewire/extensions/session-manager/keys.h>

static const char WHITESPACE[] = " \t";

typedef struct _WpCtl WpCtl;
struct _WpCtl
{
  GMainLoop *loop;
  WpCore *core;
  WpObjectManager *om;
  WpMetadata *policy_hub_m;
};

static void
wp_ctl_clear (WpCtl *self)
{
  g_clear_object (&self->om);
  g_clear_object (&self->core);
  g_clear_pointer (&self->loop, g_main_loop_unref);
}

static void
print_node (const GValue *item, gpointer data)
{
  WpPipewireObject *obj = g_value_get_object (item);
  guint32 id = wp_proxy_get_bound_id (WP_PROXY (obj));
  const gchar *name =
    wp_pipewire_object_get_property (obj, PW_KEY_NODE_NAME);
  if (!name)
    name = wp_pipewire_object_get_property (obj, PW_KEY_NODE_DESCRIPTION);

  printf ("%4u. %-35s\n", id, name);
}

static void
print_items (gchar *item, WpCtl *self)
{
  g_autoptr (WpIterator) it = NULL;
  gchar *media_class_s = NULL;

  if (g_str_equal (item, "nodes"))
    media_class_s = "*Audio*";
  else if (g_str_equal (item, "sources"))
    media_class_s = "Audio/Source";
  else if (g_str_equal (item, "sinks"))
    media_class_s = "Audio/Sink";
  else if (g_str_equal (item, "streams"))
    media_class_s = "*Audio";

  it = wp_object_manager_new_filtered_iterator (self->om,
    WP_TYPE_NODE,
    WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_MEDIA_CLASS, "#s", media_class_s,
    NULL);

  printf("%s:\n", item);
  wp_iterator_foreach (it, print_node, (gpointer) self);
  g_clear_pointer (&it, wp_iterator_unref);
}

static void
print_hub (gchar *item, WpCtl *self)
{
  g_autoptr (WpIterator) it = NULL;

  it = wp_object_manager_new_filtered_iterator (self->om,
    WP_TYPE_NODE,
    WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_GROUP, "#s", "*loopback*",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_NAME, "#s", "*-hub*",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_VIRTUAL, "#s", "true",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_MEDIA_CLASS, "#s", "Audio/Sink",
    NULL);

  printf("%s:\n", item);
  wp_iterator_foreach (it, print_node, (gpointer) self);

  it = wp_object_manager_new_filtered_iterator (self->om,
    WP_TYPE_NODE,
    WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_GROUP, "#s", "*loopback*",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_NAME, "#s", "*-hub*",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_VIRTUAL, "#s", "true",
    WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_MEDIA_CLASS, "#s", "Stream/Output/Audio",
    NULL);

  printf("%s output stream:\n", item);
  wp_iterator_foreach (it, print_node, (gpointer) self);
  g_clear_pointer (&it, wp_iterator_unref);
}

static void
on_node_added (WpObjectManager *om, WpPipewireObject *node, WpCtl *self)
{
  guint32 id = wp_proxy_get_bound_id (WP_PROXY (node));
  const gchar *name =
    wp_pipewire_object_get_property (node, PW_KEY_NODE_DESCRIPTION);
  if (!name)
    name = wp_pipewire_object_get_property (node, PW_KEY_APP_NAME);
  if (!name)
    name = wp_pipewire_object_get_property (node, PW_KEY_NODE_NAME);

  printf ("\n%4u. %-35s\n", id, name);
}
static void
scan_nodes (gchar *item, WpCtl *self)
{
  g_signal_connect (self->om, "object-added", G_CALLBACK (on_node_added), self);
}

static void
print_prompt ()
{
  printf("[wpctl]>>");
  fflush(stdout);
}

static void
unlink_nodes (gchar *args, WpCtl *self)
{
  g_autoptr (WpNode) source_node = NULL, target_node = NULL;
  gchar *source = NULL, *target = NULL;
  char *a[2];
  int n;

  n = pw_split_ip (args, WHITESPACE, 2, a);
  if (n < 1) {
    return;
  }

  if (n == 1)
    source = a[0];
  else if (n == 2) {
    source = a[0];
    target = a[1];
  }

  /* verify node ids */
  if (source) {
    source_node = wp_object_manager_lookup (self->om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u",
      atoi(source), NULL);
    if (!source_node) {
      source_node = wp_object_manager_lookup (self->om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", source, NULL);
      if (!source_node) {
        fprintf (stderr, "invalid source node '%s'\n", source);
        return;
      }

    }
  }

  if (target) {
    target_node = wp_object_manager_lookup (self->om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u",
      atoi(target), NULL);
    if (!target_node) {
      target_node = wp_object_manager_lookup (self->om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", target, NULL);
      if (!target_node) {
        fprintf (stderr, "invalid target node '%s'\n", target);
        return;
      }
    }
  }
  /* find metadata */
  if (!self->policy_hub_m) {
    self->policy_hub_m = wp_object_manager_lookup (self->om, WP_TYPE_METADATA,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s",
      "policy-hub", NULL);
    if (!self->policy_hub_m) {
      fprintf (stderr, "policy-hub metadata not found\n");
      return;
    }
  }

  /* update metadata */
  wp_metadata_set (self->policy_hub_m, 0, source, "Spa:String:JSON", "-1");

  if (target)
    wp_metadata_set (self->policy_hub_m, 0, target, "Spa:String:JSON", "-1");

  return;
}


static void
link_nodes (gchar *args, WpCtl *self)
{
  g_autoptr (WpNode) source_node = NULL, target_node = NULL;
  gchar *source = NULL, *target = NULL;
  char *a[2];
  int n;

  n = pw_split_ip (args, WHITESPACE, 2, a);
  if (n < 1) {
    return;
  }

  if (n == 1)
    source = a[0];
  else if (n == 2) {
    source = a[0];
    target = a[1];
  }

  /* verify nodes */
  if (source) {
    source_node = wp_object_manager_lookup (self->om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u",
      atoi(source), NULL);
    if (!source_node) {
      source_node = wp_object_manager_lookup (self->om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", source, NULL);
      if (!source_node) {
        fprintf (stderr, "invalid source node '%s'\n", source);
        return;
      }

    }
  }

  if (target) {
    target_node = wp_object_manager_lookup (self->om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u",
      atoi(target), NULL);
    if (!target_node) {
      target_node = wp_object_manager_lookup (self->om, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "node.name", "=s", target, NULL);
      if (!target_node) {
        fprintf (stderr, "invalid target node '%s'\n", target);
        return;
      }
    }
  } else
    target = "main-hub";

  /* find metadata */
  if (!self->policy_hub_m) {
    self->policy_hub_m = wp_object_manager_lookup (self->om, WP_TYPE_METADATA,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s",
      "policy-hub", NULL);
    if (!self->policy_hub_m) {
      fprintf (stderr, "policy-hub metadata not found\n");
      return;
    }
  }

  /* update metadata */
  wp_metadata_set (self->policy_hub_m, 0, source, "Spa:String:JSON", target);

  return;
}

static void
show_help ()
{
  printf ("help or h               -- show this help text\n");
  printf ("nodes                   -- displays nodes\n");
  printf ("sources                 -- displays sources\n");
  printf ("sinks                   -- displays sinks\n");
  printf ("streams                 -- displays sinks\n");
  printf ("hub                     -- displays hub\n");
  printf ("scan                    -- scans and displays new items added\n");
  printf ("link <node1> <node2>    -- links the node to each other. node id or name will do\n");
  printf ("link <node1>            -- when invoked with single node connects it to the main-hub\n");
  printf ("unlink <node1> <node2>  -- unlinks the connected nodes node id or name will do\n");
  printf ("unlink <node1>          -- when invoked with single node, not is unlinked from all the links in which it is part of\n");
  printf ("quit or q\n");
}

static void
process_cmds (gchar *cmd, gchar *args, WpCtl *self)
{
  if (g_str_equal (cmd, "help") || g_str_equal (cmd, "h")) {
    show_help ();
  } else if ((g_str_equal (cmd, "nodes")) ||
      (g_str_equal (cmd, "sources")) ||
      (g_str_equal (cmd, "sinks")) ||
      (g_str_equal (cmd, "streams"))) {
    print_items (cmd, self);
  } else if (g_str_equal (cmd, "hub") || g_str_equal (cmd, "hubs"))
    print_hub (cmd, self);
  else if (g_str_equal (cmd, "scan"))
    scan_nodes (cmd, self);
  else if (g_str_equal (cmd, "link"))
    link_nodes (args, self);
  else if (g_str_equal (cmd, "unlink"))
    unlink_nodes (args, self);
  else
    printf ("Invalid command. say \"help\" for info\n");
}

typedef void (*KBCallback) (gchar *input, gpointer cookie);

void keyboard_input_handler (gchar *buff, void *cookie)
{
  WpCtl *self = cookie;
  gchar *a[2];
  int n;
  char *p, *cmd, *args;

  if ((buff[0] == '\n') || (!buff)) {
    print_prompt ();
    return;
  }

  p = pw_strip(buff, "\n\r \t");

  n = pw_split_ip(p, WHITESPACE, 2, a);

  cmd = a[0];
  args = n > 1 ? a[1] : "";

  if (g_str_equal (cmd, "quit") || g_str_equal (cmd, "q"))
    g_main_loop_quit (self->loop);
  else {
    process_cmds (cmd, args, self);
    print_prompt ();
  }
}

typedef struct {
  KBCallback callback;
  gpointer cookie;
} KBData;

static void
free_kb_data (KBData *data)
{
  g_slice_free (KBData, data);
}

static gboolean
handle_kb_input (GIOChannel *ioc, GIOCondition cond,
    KBData *data)
{
  GIOStatus status;
  gsize count = 4095;
  gchar buf[4096];

  while (g_io_channel_get_flags (ioc) & G_IO_FLAG_IS_READABLE && count > 0) {
    status = g_io_channel_read_chars (ioc, buf, count, &count, NULL);
    switch (status) {

    case G_IO_STATUS_NORMAL:
      if (count > 0) {
        buf[count] = '\0'; /* Add null terminator */
        data->callback (buf, data->cookie);
      }
      break;

    case G_IO_STATUS_ERROR:
      g_critical ("Error reading keyboard input");
      return FALSE;
      break;

    case G_IO_STATUS_EOF:
    case G_IO_STATUS_AGAIN:
      continue;
      break;
    }
  }
  return TRUE;
}

void set_kb_input_handler (KBCallback kb_func, gpointer cookie)
{
  GIOChannel *ioc = g_io_channel_unix_new (STDIN_FILENO);
  g_return_if_fail (ioc != NULL);
  KBData *data;

  /* Turn on non-blocking */
  g_io_channel_set_flags (ioc,
      g_io_channel_get_flags (ioc) | G_IO_FLAG_NONBLOCK, NULL);
  data = g_slice_new (KBData);
  data->callback = kb_func;
  data->cookie = cookie;
  g_io_add_watch_full (ioc, G_PRIORITY_DEFAULT, G_IO_IN,
      (GIOFunc) (handle_kb_input), data, (GDestroyNotify) free_kb_data);
}

gint
main (gint argc, gchar **argv)
{
  WpCtl ctl = { 0 };

  setlocale (LC_ALL, "");
  setlocale (LC_NUMERIC, "C");

  wp_init (WP_INIT_ALL);

  ctl.loop = g_main_loop_new (NULL, FALSE);
  ctl.core = wp_core_new (NULL, NULL);

  if (!wp_core_connect (ctl.core)) {
    fprintf (stderr, "Could not connect to PipeWire\n");
    return 2;
  }

  ctl.om = wp_object_manager_new ();
  wp_object_manager_add_interest (ctl.om, WP_TYPE_NODE, NULL);
  wp_object_manager_add_interest (ctl.om, WP_TYPE_METADATA, NULL);
  wp_object_manager_request_object_features (ctl.om, WP_TYPE_NODE,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  wp_object_manager_request_object_features (ctl.om, WP_TYPE_METADATA,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  wp_core_install_object_manager (ctl.core, ctl.om);

  g_signal_connect_swapped (ctl.om, "installed",
      (GCallback) print_prompt, NULL);

  g_signal_connect_swapped (ctl.core, "disconnected",
      (GCallback) g_main_loop_quit, ctl.loop);

  set_kb_input_handler (keyboard_input_handler, &ctl);

  g_main_loop_run (ctl.loop);
  wp_ctl_clear (&ctl);
  return 0;
}
