/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <stdio.h>

typedef struct _WpCtl WpCtl;
struct _WpCtl
{
  GOptionContext *context;
  GMainLoop *loop;
  WpCore *core;
  WpObjectManager *om;
  gint exit_code;
};

static struct {
  union {
    struct {
      gboolean show_streams;
    } status;

    struct {
      guint32 id;
    } set_default;

    struct {
      guint32 id;
      gfloat volume;
    } set_volume;

    struct {
      guint32 id;
      guint mute;
    } set_mute;
  };
} cmdline;

G_DEFINE_QUARK (wpctl-error, wpctl_error_domain)

static void
wp_ctl_clear (WpCtl * self)
{
  g_clear_object (&self->om);
  g_clear_object (&self->core);
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_clear_pointer (&self->context, g_option_context_free);
}

static void
async_quit (WpCore *core, GAsyncResult *res, WpCtl * self)
{
  g_main_loop_quit (self->loop);
}

/* status */

static gboolean
status_prepare (WpCtl * self, GError ** error)
{
  wp_object_manager_add_interest (self->om, WP_TYPE_SESSION, NULL);
  wp_object_manager_request_proxy_features (self->om, WP_TYPE_SESSION,
      WP_SESSION_FEATURES_STANDARD);
  return TRUE;
}

#define TREE_INDENT_LINE " │  "
#define TREE_INDENT_NODE " ├─ "
#define TREE_INDENT_END  " └─ "
#define TREE_INDENT_EMPTY "    "

static void
print_controls (WpProxy * proxy)
{
  g_autoptr (WpSpaPod) ctrl = NULL;
  gboolean has_audio_controls = FALSE;
  gfloat volume = 0.0;
  gboolean mute = FALSE;

  if ((ctrl = wp_proxy_get_prop (proxy, "volume"))) {
    wp_spa_pod_get_float (ctrl, &volume);
    has_audio_controls = TRUE;
  }
  if ((ctrl = wp_proxy_get_prop (proxy, "mute"))) {
    wp_spa_pod_get_boolean (ctrl, &mute);
    has_audio_controls = TRUE;
  }

  if (has_audio_controls)
    printf (" vol: %.2f %s\n", volume, mute ? "MUTED" : "");
  else
    printf ("\n");
}

static void
print_stream (const GValue *item, gpointer data)
{
  WpEndpointStream *stream = g_value_get_object (item);
  guint32 id = wp_proxy_get_bound_id (WP_PROXY (stream));
  guint *n_streams = data;

  printf (TREE_INDENT_LINE TREE_INDENT_EMPTY " %s%4u. %-53s",
      (--(*n_streams) == 0) ? TREE_INDENT_END : TREE_INDENT_NODE,
      id, wp_endpoint_stream_get_name (stream));
  print_controls (WP_PROXY (stream));
}

static void
print_endpoint (const GValue *item, gpointer data)
{
  WpEndpoint *ep = g_value_get_object (item);
  guint32 id = wp_proxy_get_bound_id (WP_PROXY (ep));
  guint32 default_id = GPOINTER_TO_UINT (data);

  printf (TREE_INDENT_LINE "%c %4u. %-60s",
      (default_id == id) ? '*' : ' ', id, wp_endpoint_get_name (ep));
  print_controls (WP_PROXY (ep));

  if (cmdline.status.show_streams) {
    g_autoptr (WpIterator) it = wp_endpoint_iterate_streams (ep);
    guint n_streams = wp_endpoint_get_n_streams (ep);
    wp_iterator_foreach (it, print_stream, &n_streams);
    printf (TREE_INDENT_LINE "\n");
  }
}

static void
print_endpoint_link (const GValue *item, gpointer data)
{
  WpEndpointLink *link = g_value_get_object (item);
  WpSession *session = data;
  guint32 id = wp_proxy_get_bound_id (WP_PROXY (link));
  guint32 out_ep_id, out_stream_id, in_ep_id, in_stream_id;
  g_autoptr (WpEndpoint) out_ep = NULL;
  g_autoptr (WpEndpoint) in_ep = NULL;
  g_autoptr (WpEndpointStream) out_stream = NULL;
  g_autoptr (WpEndpointStream) in_stream = NULL;

  wp_endpoint_link_get_linked_object_ids (link,
      &out_ep_id, &out_stream_id, &in_ep_id, &in_stream_id);

  out_ep = wp_session_lookup_endpoint (session,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", out_ep_id, NULL);
  in_ep = wp_session_lookup_endpoint (session,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", in_ep_id, NULL);

  out_stream = wp_endpoint_lookup_stream (out_ep,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", out_stream_id, NULL);
  in_stream = wp_endpoint_lookup_stream (in_ep,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", in_stream_id, NULL);

  printf (TREE_INDENT_EMPTY "  %4u. [%u. %s|%s] ➞ [%u. %s|%s]\n", id,
      out_ep_id, wp_endpoint_get_name (out_ep),
      wp_endpoint_stream_get_name (out_stream),
      in_ep_id, wp_endpoint_get_name (in_ep),
      wp_endpoint_stream_get_name (in_stream));
}

static void
status_run (WpCtl * self)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  it = wp_object_manager_iterate (self->om);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSession *session = g_value_get_object (&val);
    g_autoptr (WpIterator) child_it = NULL;
    guint32 default_sink =
        wp_session_get_default_endpoint (session, WP_DIRECTION_INPUT);
    guint32 default_source =
        wp_session_get_default_endpoint (session, WP_DIRECTION_OUTPUT);

    printf ("Session %u (%s)\n",
        wp_proxy_get_bound_id (WP_PROXY (session)),
        wp_session_get_name (session));

    printf (TREE_INDENT_LINE "\n");

    printf (TREE_INDENT_NODE "Sink endpoints:\n");
    child_it = wp_session_iterate_endpoints_filtered (session,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s", "*/Sink",
        NULL);
    wp_iterator_foreach (child_it, print_endpoint,
        GUINT_TO_POINTER (default_sink));
    g_clear_pointer (&child_it, wp_iterator_unref);

    printf (TREE_INDENT_LINE "\n");

    printf (TREE_INDENT_NODE "Source endpoints:\n");
    child_it = wp_session_iterate_endpoints_filtered (session,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s", "*/Source",
        NULL);
    wp_iterator_foreach (child_it, print_endpoint,
        GUINT_TO_POINTER (default_source));
    g_clear_pointer (&child_it, wp_iterator_unref);

    printf (TREE_INDENT_LINE "\n");

    printf (TREE_INDENT_NODE "Playback client endpoints:\n");
    child_it = wp_session_iterate_endpoints_filtered (session,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s", "Stream/Output/*",
        NULL);
    wp_iterator_foreach (child_it, print_endpoint, NULL);
    g_clear_pointer (&child_it, wp_iterator_unref);

    printf (TREE_INDENT_LINE "\n");

    printf (TREE_INDENT_NODE "Capture client endpoints:\n");
    child_it = wp_session_iterate_endpoints_filtered (session,
        WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "#s", "Stream/Input/*",
        NULL);
    wp_iterator_foreach (child_it, print_endpoint, NULL);
    g_clear_pointer (&child_it, wp_iterator_unref);

    printf (TREE_INDENT_LINE "\n");

    printf (TREE_INDENT_END "Endpoint links:\n");
    child_it = wp_session_iterate_links (session);
    wp_iterator_foreach (child_it, print_endpoint_link, session);
    g_clear_pointer (&child_it, wp_iterator_unref);

    printf ("\n");
  }

  g_main_loop_quit (self->loop);
}

/* set-default */

static gboolean
set_default_parse_positional (gint argc, gchar ** argv, GError **error)
{
  if (argc < 3) {
    g_set_error (error, wpctl_error_domain_quark(), 0, "ID is required");
    return FALSE;
  }

  long id = strtol (argv[2], NULL, 10);
  if (id <= 0) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "'%s' is not a valid number", argv[2]);
    return FALSE;
  }

  cmdline.set_default.id = id;
  return TRUE;
}

static gboolean
set_default_prepare (WpCtl * self, GError ** error)
{
  wp_object_manager_add_interest (self->om, WP_TYPE_SESSION, NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_ENDPOINT,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      "object.id", "=u", cmdline.set_default.id,
      NULL);
  wp_object_manager_request_proxy_features (self->om, WP_TYPE_SESSION,
      WP_PROXY_FEATURES_STANDARD | WP_PROXY_FEATURE_PROPS);
  wp_object_manager_request_proxy_features (self->om, WP_TYPE_ENDPOINT,
      WP_PROXY_FEATURES_STANDARD);
  return TRUE;
}

static void
set_default_run (WpCtl * self)
{
  g_autoptr (WpEndpoint) ep = NULL;
  g_autoptr (WpSession) session = NULL;
  guint32 id = cmdline.set_default.id;
  const gchar *sess_id_str;
  guint32 sess_id;
  WpDirection dir;

  ep = wp_object_manager_lookup (self->om, WP_TYPE_ENDPOINT, NULL);
  if (!ep) {
    printf ("Endpoint '%d' not found\n", id);
    goto out;
  }

  sess_id_str = wp_proxy_get_property (WP_PROXY (ep), "session.id");
  sess_id = sess_id_str ? atoi (sess_id_str) : 0;

  session = wp_object_manager_lookup (self->om, WP_TYPE_SESSION,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", sess_id, NULL);
  if (!session) {
    printf ("Endpoint %u has invalid session id %u\n", id, sess_id);
    goto out;
  }

  if (g_str_has_suffix (wp_endpoint_get_media_class (ep), "/Sink"))
    dir = WP_DIRECTION_INPUT;
  else if (g_str_has_suffix (wp_endpoint_get_media_class (ep), "/Source"))
    dir = WP_DIRECTION_OUTPUT;
  else {
    printf ("%u is not a device endpoint (media.class = %s)\n",
        id, wp_endpoint_get_media_class (ep));
    goto out;
  }

  wp_session_set_default_endpoint (session, dir, id);
  wp_core_sync (self->core, NULL, (GAsyncReadyCallback) async_quit, self);
  return;

out:
  self->exit_code = 3;
  g_main_loop_quit (self->loop);
}

/* set-volume */

static gboolean
set_volume_parse_positional (gint argc, gchar ** argv, GError **error)
{
  if (argc < 4) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "ID and VOL are required");
    return FALSE;
  }

  long id = strtol (argv[2], NULL, 10);
  float volume = strtof (argv[3], NULL);
  if (id <= 0) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "'%s' is not a valid number", argv[2]);
    return FALSE;
  }

  cmdline.set_volume.id = id;
  cmdline.set_volume.volume = volume;
  return TRUE;
}

static gboolean
set_volume_prepare (WpCtl * self, GError ** error)
{
  wp_object_manager_add_interest (self->om, WP_TYPE_ENDPOINT,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      "object.id", "=u", cmdline.set_volume.id,
      NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_ENDPOINT_STREAM,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      "object.id", "=u", cmdline.set_volume.id,
      NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      "object.id", "=u", cmdline.set_volume.id,
      NULL);
  wp_object_manager_request_proxy_features (self->om, WP_TYPE_PROXY,
      WP_PROXY_FEATURES_STANDARD | WP_PROXY_FEATURE_PROPS);
  return TRUE;
}

static void
set_volume_run (WpCtl * self)
{
  g_autoptr (WpProxy) proxy = NULL;
  g_autoptr (WpSpaPod) pod = NULL;
  gfloat volume;

  proxy = wp_object_manager_lookup (self->om, WP_TYPE_PROXY, NULL);
  if (!proxy) {
    printf ("Object '%d' not found\n", cmdline.set_volume.id);
    goto out;
  }

  pod = wp_proxy_get_prop (proxy, "volume");
  if (!pod || !wp_spa_pod_get_float (pod, &volume)) {
    printf ("Object '%d' does not support volume\n", cmdline.set_volume.id);
    goto out;
  }

  wp_proxy_set_prop (proxy, "volume",
      wp_spa_pod_new_float (cmdline.set_volume.volume));
  wp_core_sync (self->core, NULL, (GAsyncReadyCallback) async_quit, self);
  return;

out:
  self->exit_code = 3;
  g_main_loop_quit (self->loop);
}

/* set-mute */

static gboolean
set_mute_parse_positional (gint argc, gchar ** argv, GError **error)
{
  if (argc < 4) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "ID and one of '1', '0' or 'toggle' are required");
    return FALSE;
  }

  long id = strtol (argv[2], NULL, 10);
  if (id <= 0) {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "'%s' is not a valid number", argv[2]);
    return FALSE;
  }
  cmdline.set_mute.id = id;

  if (!g_strcmp0 (argv[3], "1"))
    cmdline.set_mute.mute = 1;
  else if (!g_strcmp0 (argv[3], "0"))
    cmdline.set_mute.mute = 0;
  else if (!g_strcmp0 (argv[3], "toggle"))
    cmdline.set_mute.mute = 2;
  else {
    g_set_error (error, wpctl_error_domain_quark(), 0,
        "'%s' is not a valid mute option", argv[3]);
    return FALSE;
  }

  return TRUE;
}

static gboolean
set_mute_prepare (WpCtl * self, GError ** error)
{
  wp_object_manager_add_interest (self->om, WP_TYPE_ENDPOINT,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      "object.id", "=u", cmdline.set_mute.id,
      NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_ENDPOINT_STREAM,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      "object.id", "=u", cmdline.set_mute.id,
      NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      "object.id", "=u", cmdline.set_mute.id,
      NULL);
  wp_object_manager_request_proxy_features (self->om, WP_TYPE_PROXY,
      WP_PROXY_FEATURES_STANDARD | WP_PROXY_FEATURE_PROPS);
  return TRUE;
}

static void
set_mute_run (WpCtl * self)
{
  g_autoptr (WpProxy) proxy = NULL;
  g_autoptr (WpSpaPod) pod = NULL;
  gboolean mute;

  proxy = wp_object_manager_lookup (self->om, WP_TYPE_PROXY, NULL);
  if (!proxy) {
    printf ("Object '%d' not found\n", cmdline.set_mute.id);
    goto out;
  }

  pod = wp_proxy_get_prop (proxy, "mute");
  if (!pod || !wp_spa_pod_get_boolean (pod, &mute)) {
    printf ("Object '%d' does not support mute\n", cmdline.set_mute.id);
    goto out;
  }

  if (cmdline.set_mute.mute == 2)
    mute = !mute;
  else
    mute = !!cmdline.set_mute.mute;

  wp_proxy_set_prop (proxy, "mute", wp_spa_pod_new_boolean (mute));
  wp_core_sync (self->core, NULL, (GAsyncReadyCallback) async_quit, self);
  return;

out:
  self->exit_code = 3;
  g_main_loop_quit (self->loop);
}

#define N_ENTRIES 2

static const struct subcommand {
  /* the name to match on the command line */
  const gchar *name;
  /* description of positional arguments, shown in the help message */
  const gchar *positional_args;
  /* short description, shown at the top of the help message */
  const gchar *summary;
  /* long description, shown at the bottom of the help message */
  const gchar *description;
  /* additional cmdline arguments for this subcommand */
  const GOptionEntry entries[N_ENTRIES];
  /* function to parse positional arguments */
  gboolean (*parse_positional) (gint, gchar **, GError **);
  /* function to prepare the object manager */
  gboolean (*prepare) (WpCtl *, GError **);
  /* function to run after the object manager is installed */
  void (*run) (WpCtl *);
} subcommands[] = {
  {
    .name = "status",
    .positional_args = "",
    .summary = "Displays the current state of objects in PipeWire",
    .description = NULL,
    .entries = {
      { "streams", 's', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
        &cmdline.status.show_streams, "Also show endpoint streams", NULL },
      { NULL }
    },
    .parse_positional = NULL,
    .prepare = status_prepare,
    .run = status_run,
  },
  {
    .name = "set-default",
    .positional_args = "ID",
    .summary = "Sets ID to be the default endpoint of its kind "
               "(capture/playback) in its session",
    .description = NULL,
    .entries = { { NULL } },
    .parse_positional = set_default_parse_positional,
    .prepare = set_default_prepare,
    .run = set_default_run,
  },
  {
    .name = "set-volume",
    .positional_args = "ID VOL",
    .summary = "Sets the volume of ID to VOL (floating point, 1.0 is 100%%)",
    .description = NULL,
    .entries = { { NULL } },
    .parse_positional = set_volume_parse_positional,
    .prepare = set_volume_prepare,
    .run = set_volume_run,
  },
  {
    .name = "set-mute",
    .positional_args = "ID 1|0|toggle",
    .summary = "Changes the mute state of ID",
    .description = NULL,
    .entries = { { NULL } },
    .parse_positional = set_mute_parse_positional,
    .prepare = set_mute_prepare,
    .run = set_mute_run,
  },
};

gint
main (gint argc, gchar **argv)
{
  WpCtl ctl = {0};
  const struct subcommand *cmd = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *summary = NULL;
  g_autofree gchar *group_desc = NULL;
  g_autofree gchar *group_help_desc = NULL;

  wp_init (WP_INIT_ALL);

  ctl.context = g_option_context_new (
      "COMMAND [COMMAND_OPTIONS] - WirePlumber Control CLI");
  ctl.loop = g_main_loop_new (NULL, FALSE);
  ctl.core = wp_core_new (NULL, NULL);
  ctl.om = wp_object_manager_new ();

  /* find the subcommand */
  if (argc > 1) {
    for (guint i = 0; i < G_N_ELEMENTS (subcommands); i++) {
      if (!g_strcmp0 (argv[1], subcommands[i].name)) {
        cmd = &subcommands[i];
        break;
      }
    }
  }

  /* prepare the subcommand options */
  if (cmd) {
    GOptionGroup *group;

    /* options */
    group = g_option_group_new (cmd->name, NULL, NULL, &ctl, NULL);
    g_option_group_add_entries (group, cmd->entries);
    g_option_context_set_main_group (ctl.context, group);

    /* summary */
    summary = g_strdup_printf ("Command: %s %s\n  %s",
        cmd->name, cmd->positional_args, cmd->summary);
    g_option_context_set_summary (ctl.context, summary);

    /* description */
    if (cmd->description)
      g_option_context_set_description (ctl.context, cmd->description);
  }
  else {
    /* build the generic summary */
    GString *summary_str = g_string_new ("Commands:");
    for (guint i = 0; i < G_N_ELEMENTS (subcommands); i++) {
      g_string_append_printf (summary_str, "\n  %s %s", subcommands[i].name,
          subcommands[i].positional_args);
    }
    summary = g_string_free (summary_str, FALSE);
    g_option_context_set_summary (ctl.context, summary);
    g_option_context_set_description (ctl.context, "Pass -h after a command "
        "to see command-specific options\n");
  }

  /* parse options */
  if (!g_option_context_parse (ctl.context, &argc, &argv, &error) ||
      (cmd && cmd->parse_positional &&
          !cmd->parse_positional (argc, argv, &error))) {
    fprintf (stderr, "Error: %s\n\n", error->message);
    cmd = NULL;
  }

  /* no active subcommand, show usage and exit */
  if (!cmd) {
    g_autofree gchar *help =
        g_option_context_get_help (ctl.context, FALSE, NULL);
    printf ("%s", help);
    return 1;
  }

  /* prepare the subcommand */
  if (!cmd->prepare (&ctl, &error)) {
    fprintf (stderr, "%s\n", error->message);
    return 1;
  }

  /* connect */
  if (!wp_core_connect (ctl.core)) {
    fprintf (stderr, "Could not connect to PipeWire\n");
    return 2;
  }

  /* run */
  g_signal_connect_swapped (ctl.core, "disconnected",
      (GCallback) g_main_loop_quit, ctl.loop);
  g_signal_connect_swapped (ctl.om, "installed",
      (GCallback) cmd->run, &ctl);
  wp_core_install_object_manager (ctl.core, ctl.om);
  g_main_loop_run (ctl.loop);

  wp_ctl_clear (&ctl);
  return ctl.exit_code;
}
