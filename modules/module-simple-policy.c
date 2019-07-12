/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

enum {
  DIRECTION_SINK = 0,
  DIRECTION_SOURCE
};

struct _WpSimplePolicy
{
  WpPolicy parent;
  WpEndpoint *selected[2];
  guint32 selected_ctl_id[2];
  gchar *default_playback;
  gchar *default_capture;
  GQueue *unhandled_endpoints;
};

G_DECLARE_FINAL_TYPE (WpSimplePolicy, simple_policy, WP, SIMPLE_POLICY, WpPolicy)
G_DEFINE_TYPE (WpSimplePolicy, simple_policy, WP_TYPE_POLICY)

static void
simple_policy_init (WpSimplePolicy *self)
{
  self->unhandled_endpoints = g_queue_new ();
}

static void
simple_policy_finalize (GObject *object)
{
  WpSimplePolicy *self = WP_SIMPLE_POLICY (object);

  g_free (self->default_playback);
  g_free (self->default_capture);
  g_queue_free_full (self->unhandled_endpoints, (GDestroyNotify)g_object_unref);

  G_OBJECT_CLASS (simple_policy_parent_class)->finalize (object);
}

static void
endpoint_notify_control_value (WpEndpoint * ep, guint control_id,
    WpSimplePolicy *self)
{
  g_autoptr (GVariant) v = NULL;
  const gchar *media_class;
  guint32 tmp_id;
  gint direction;
  WpEndpoint *old_selected;
  guint32 old_ctl_id;

  /* when an endpoint becomes "selected", unselect
   * all other endpoints of the same media class */

  /* the already "selected" endpoint cannot become even more "selected",
   * so we skip it */
  if (ep == self->selected[DIRECTION_SINK] ||
      ep == self->selected[DIRECTION_SOURCE])
    return;

  /* verify that the changed control is the "selected" */
  tmp_id = wp_endpoint_find_control (ep, WP_STREAM_ID_NONE, "selected");
  if (control_id != tmp_id)
    return;

  /* verify it changed to TRUE */
  v = wp_endpoint_get_control_value (ep, control_id);
  if (g_variant_get_boolean (v) == FALSE)
    return;

  media_class = wp_endpoint_get_media_class (ep);
  direction = strstr(media_class, "Sink") ? DIRECTION_SINK : DIRECTION_SOURCE;

  g_debug ("selected %s: %p, unselecting %p",
      (direction == DIRECTION_SINK) ? "sink" : "source",
      ep, self->selected);

  old_selected = self->selected[direction];
  old_ctl_id = self->selected_ctl_id[direction];
  self->selected[direction] = ep;
  self->selected_ctl_id[direction] = control_id;

  /* update the control value */
  wp_endpoint_set_control_value (old_selected, old_ctl_id,
      g_variant_new_boolean (FALSE));

  /* notify policy watchers that things have changed */
  wp_policy_notify_changed (WP_POLICY (self));
}

static void
select_endpoint (WpSimplePolicy *self, gint direction, WpEndpoint *ep,
    guint32 control_id)
{
  g_info ("selecting %s %p (%s)",
      (direction == DIRECTION_SINK) ? "sink" : "source",
      ep, wp_endpoint_get_name (ep));

  self->selected[direction] = ep;
  self->selected_ctl_id[direction] = control_id;

  /* update the control value */
  wp_endpoint_set_control_value (ep, control_id,
      g_variant_new_boolean (TRUE));

  /* notify policy watchers that things have changed */
  wp_policy_notify_changed (WP_POLICY (self));
}

static gboolean
select_new_endpoint (WpSimplePolicy *self)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (GPtrArray) ptr_array = NULL;
  const gchar *media_class = NULL;
  WpEndpoint *other;
  guint32 control_id;
  gint direction, i;

  if (!self->selected[DIRECTION_SINK]) {
    direction = DIRECTION_SINK;
    media_class = "Audio/Sink";
  } else if (!self->selected[DIRECTION_SOURCE]) {
    direction = DIRECTION_SOURCE;
    media_class = "Audio/Source";
  } else
    return G_SOURCE_REMOVE;

  core = wp_policy_get_core (WP_POLICY (self));

  /* Get all the endpoints with the same media class */
  ptr_array = wp_endpoint_find (core, media_class);

  /* select the first available that has the "selected" control */
  for (i = 0; i < (ptr_array ? ptr_array->len : 0); i++) {
    other = g_ptr_array_index (ptr_array, i);

    control_id =
        wp_endpoint_find_control (other, WP_STREAM_ID_NONE, "selected");
    if (control_id == WP_CONTROL_ID_NONE)
      continue;

    select_endpoint (self, direction, other, control_id);
    break;
  }

  return G_SOURCE_REMOVE;
}

static void
simple_policy_endpoint_added (WpPolicy *policy, WpEndpoint *ep)
{
  WpSimplePolicy *self = WP_SIMPLE_POLICY (policy);
  const gchar *media_class = wp_endpoint_get_media_class (ep);
  guint32 control_id;
  gint direction;

  /* we only care about audio device endpoints here */
  if (!g_str_has_prefix (media_class, "Audio/"))
    return;

  /* verify it has the "selected" control available */
  control_id = wp_endpoint_find_control (ep, WP_STREAM_ID_NONE, "selected");
  if (control_id == WP_CONTROL_ID_NONE)
    return;

  /* connect to control value changes */
  g_debug ("connecting to notify-control-value for %p", ep);
  g_signal_connect_object (ep, "notify-control-value",
      (GCallback) endpoint_notify_control_value, self, 0);

  /* select this endpoint if no other is already selected */
  direction = strstr (media_class, "Sink") ? DIRECTION_SINK : DIRECTION_SOURCE;

  if (!self->selected[direction]) {
    select_endpoint (self, direction, ep, control_id);
  } else {
    /* we already have a selected endpoint, but maybe this one is better... */
    const gchar *new_name = wp_endpoint_get_name (ep);
    const gchar *default_dev = (direction == DIRECTION_SINK) ?
        self->default_playback : self->default_capture;

    /* FIXME: this is a crude way of searching for properties;
     * we should have an API here */
    if ((default_dev && strstr (new_name, default_dev)) ||
        (!default_dev && strstr (new_name, "hw:0,0")))
    {
      wp_endpoint_set_control_value (self->selected[direction],
          self->selected_ctl_id[direction],
          g_variant_new_boolean (FALSE));
      select_endpoint (self, direction, ep, control_id);
    }
  }
}

static void
simple_policy_endpoint_removed (WpPolicy *policy, WpEndpoint *ep)
{
  WpSimplePolicy *self = WP_SIMPLE_POLICY (policy);
  gint direction;

  /* if the "selected" endpoint was removed, select another one */

  if (ep == self->selected[DIRECTION_SINK])
    direction = DIRECTION_SINK;
  else if (ep == self->selected[DIRECTION_SOURCE])
    direction = DIRECTION_SOURCE;
  else
    return;

  self->selected[direction] = NULL;
  self->selected_ctl_id[direction] = WP_CONTROL_ID_NONE;

  /* do the rest later, to possibly let other endpoints be removed as well
   * before we try to pick a new one, to avoid multiple notifications
   * and to also avoid crashing when the pipewire remote disconnects
   * (in which case all endpoints are getting removed and changing controls
   * triggers a crash in remote-endpoint, trying to export a value change
   * without a valid connection)
   */
  g_idle_add_full (G_PRIORITY_HIGH, (GSourceFunc) select_new_endpoint,
      g_object_ref (self), g_object_unref);
}

static void
on_endpoint_link_created(GObject *initable, GAsyncResult *res, gpointer d)
{
  g_autoptr (WpEndpointLink) link = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WpEndpoint) src_ep = NULL;
  g_autoptr (WpEndpoint) sink_ep = NULL;

  /* Get the link */
  link = wp_endpoint_link_new_finish(initable, res, &error);
  g_return_if_fail (link);

  /* Log linking info */
  if (error) {
    g_warning ("Could not link endpoints: %s\n", error->message);
  } else {
    src_ep = wp_endpoint_link_get_source_endpoint (link);
    sink_ep = wp_endpoint_link_get_sink_endpoint (link);
    g_info ("Sucessfully linked '%s' to '%s'\n", wp_endpoint_get_name (src_ep),
        wp_endpoint_get_name (sink_ep));
  }
}

static gboolean
handle_client (WpPolicy *policy, WpEndpoint *ep)
{
  const char *media_class = wp_endpoint_get_media_class(ep);
  GVariantDict d;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpEndpoint) target = NULL;
  guint32 stream_id;
  gboolean is_sink = FALSE;
  g_autofree gchar *role = NULL;

  /* Detect if the client is a sink or a source */
  is_sink = g_str_has_prefix (media_class, "Stream/Input");

  /* Locate the target endpoint */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "action", "s", "link");
  g_variant_dict_insert (&d, "media.class", "s",
      is_sink ? "Audio/Source" : "Audio/Sink");

  g_object_get (ep, "role", &role, NULL);
  if (role)
    g_variant_dict_insert (&d, "media.role", "s", role);

  /* TODO: more properties are needed here */

  core = wp_policy_get_core (policy);
  target = wp_policy_find_endpoint (core, g_variant_dict_end (&d), &stream_id);
  if (!target)
    return FALSE;

  /* Unlink the target if it is already linked */
  if (wp_endpoint_is_linked (target))
    wp_endpoint_unlink (target);

  /* Link the client with the target */
  if (is_sink) {
    wp_endpoint_link_new (core, target, 0, ep, stream_id,
        on_endpoint_link_created, NULL);
  } else {
    wp_endpoint_link_new (core, ep, 0, target, stream_id,
        on_endpoint_link_created, NULL);
  }

  return TRUE;
}

static void
try_unhandled_clients (WpPolicy *policy)
{
  WpSimplePolicy *self = WP_SIMPLE_POLICY (policy);
  WpEndpoint *ep = NULL;
  GQueue *tmp = g_queue_new ();

  /* Try to handle all the unhandled endpoints, and add them into a tmp queue
   * if they were not handled */
  while ((ep = g_queue_pop_head (self->unhandled_endpoints))) {
    if (handle_client (policy, ep))
      g_object_unref (ep);
    else
      g_queue_push_tail (tmp, ep);
  }

  /* Add back the unhandled endpoints to the unhandled queue */
  while ((ep = g_queue_pop_head (tmp)))
    g_queue_push_tail (self->unhandled_endpoints, ep);

  /* Clean up */
  g_queue_free_full (tmp, (GDestroyNotify)g_object_unref);
}

static gboolean
simple_policy_handle_endpoint (WpPolicy *policy, WpEndpoint *ep)
{
  WpSimplePolicy *self = WP_SIMPLE_POLICY (policy);
  const char *media_class = NULL;

  /* TODO: For now we only accept audio stream clients */
  media_class = wp_endpoint_get_media_class(ep);
  if (!g_str_has_prefix (media_class, "Stream") ||
      !g_str_has_suffix (media_class, "Audio")) {
    /* Try handling unhandled endpoints if a non client one has been added */
    try_unhandled_clients (policy);
    return FALSE;
  }

  /* Handle the endpoint */
  if (handle_client (policy, ep))
    return TRUE;

  /* Otherwise add it to the unhandled queue */
  g_queue_push_tail (self->unhandled_endpoints, g_object_ref (ep));
  return FALSE;
}

static WpEndpoint *
simple_policy_find_endpoint (WpPolicy *policy, GVariant *props,
    guint32 *stream_id)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (GPtrArray) ptr_array = NULL;
  const char *action = NULL;
  const char *media_class = NULL;
  const char *role = NULL;
  WpEndpoint *ep;
  int i;

  core = wp_policy_get_core (policy);

  g_variant_lookup (props, "action", "&s", &action);

  /* Get all the endpoints with the specific media class*/
  g_variant_lookup (props, "media.class", "&s", &media_class);
  ptr_array = wp_endpoint_find (core, media_class);
  if (!ptr_array)
    return NULL;

  /* Find and return the "selected" endpoint */
  for (i = 0; i < ptr_array->len; i++) {
    ep = g_ptr_array_index (ptr_array, i);
    g_autoptr (GVariant) value = NULL;
    guint id;

    id = wp_endpoint_find_control (ep, WP_STREAM_ID_NONE, "selected");
    if (id == WP_CONTROL_ID_NONE)
      continue;

    value = wp_endpoint_get_control_value (ep, id);
    if (value && g_variant_get_boolean (value)) {
      g_object_ref (ep);
      goto select_stream;
    }
  }

  /* If not found, return the first endpoint */
  ep = (ptr_array->len > 1) ?
    g_object_ref (g_ptr_array_index (ptr_array, 0)) : NULL;

select_stream:
  g_variant_lookup (props, "media.role", "&s", &role);
  if (!g_strcmp0 (action, "mixer") && !g_strcmp0 (role, "Master"))
    *stream_id = WP_STREAM_ID_NONE;
  else if (ep) {
    /* the default role is "Multimedia" */
    if (!role)
      role = "Multimedia";
    *stream_id = wp_endpoint_find_stream (ep, role);

    /* role not found, try the first stream */
    if (*stream_id == WP_STREAM_ID_NONE) {
      g_warning ("role '%s' not found in endpoint", role);
      *stream_id = 0;
    }
  }

  return ep;
}

static void
simple_policy_class_init (WpSimplePolicyClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPolicyClass *policy_class = (WpPolicyClass *) klass;

  object_class->finalize = simple_policy_finalize;

  policy_class->endpoint_added = simple_policy_endpoint_added;
  policy_class->endpoint_removed = simple_policy_endpoint_removed;
  policy_class->handle_endpoint = simple_policy_handle_endpoint;
  policy_class->find_endpoint = simple_policy_find_endpoint;
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  WpSimplePolicy *p = g_object_new (simple_policy_get_type (),
      "rank", WP_POLICY_RANK_UPSTREAM,
      NULL);
  g_variant_lookup (args, "default-playback-device", "s", &p->default_playback);
  g_variant_lookup (args, "default-capture-device", "s", &p->default_capture);
  wp_policy_register (WP_POLICY (p), core);
}
