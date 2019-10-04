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
  GVariant *role_priorities;
  guint pending_rescan;
};

G_DECLARE_FINAL_TYPE (WpSimplePolicy, simple_policy, WP, SIMPLE_POLICY, WpPolicy)
G_DEFINE_TYPE (WpSimplePolicy, simple_policy, WP_TYPE_POLICY)

static void simple_policy_rescan (WpSimplePolicy *self);

static void
simple_policy_init (WpSimplePolicy *self)
{
}

static void
simple_policy_finalize (GObject *object)
{
  WpSimplePolicy *self = WP_SIMPLE_POLICY (object);

  g_free (self->default_playback);
  g_free (self->default_capture);
  g_clear_pointer (&self->role_priorities, g_variant_unref);

  if (self->pending_rescan)
    g_source_remove (self->pending_rescan);

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

  /* rescan for clients that need to be linked */
  simple_policy_rescan (self);
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

  /* rescan for clients that need to be linked */
  simple_policy_rescan (self);
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

  /* we only care about alsa device endpoints here */
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

  simple_policy_rescan (self);

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

static void
handle_client (WpPolicy *policy, WpEndpoint *ep)
{
  const char *media_class = wp_endpoint_get_media_class(ep);
  GVariantDict d;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpEndpoint) target = NULL;
  guint32 stream_id;
  gboolean is_capture = FALSE;
  gboolean is_persistent = FALSE;
  g_autofree gchar *role = NULL;

  /* Detect if the client is doing capture or playback */
  is_capture = g_str_has_prefix (media_class, "Stream/Input");
  is_persistent = g_str_has_prefix (media_class, "Persistent/");

  /* Locate the target endpoint */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "action", "s", "link");
  g_variant_dict_insert (&d, "media.class", "s",
      is_capture ? "Audio/Source" : "Audio/Sink");

  g_object_get (ep, "role", &role, NULL);
  if (role)
    g_variant_dict_insert (&d, "media.role", "s", role);

  /* TODO: more properties are needed here */

  core = wp_policy_get_core (policy);
  target = wp_policy_find_endpoint (core, g_variant_dict_end (&d), &stream_id);
  if (!target) {
    g_warning ("Could not find target endpoint");
    return;
  }

  /* if the client is already linked... */
  if (wp_endpoint_is_linked (ep)) {
    g_autoptr (WpEndpoint) existing_target = NULL;
    GPtrArray *links = wp_endpoint_get_links (ep);
    WpEndpointLink *l = g_ptr_array_index (links, 0);

    existing_target = is_capture ?
        wp_endpoint_link_get_source_endpoint (l) :
        wp_endpoint_link_get_sink_endpoint (l);

    if (existing_target == target) {
      /* ... do nothing if it's already linked to the correct target */
      g_debug ("Client '%s' already linked correctly",
          wp_endpoint_get_name (ep));
      return;
    } else {
      /* ... or else unlink it and continue */
      g_debug ("Unlink client '%s' from its previous target",
          wp_endpoint_get_name (ep));
      wp_endpoint_link_destroy (l);
    }
  }

  /* Unlink the target if it is already linked */
  /* At this point we are certain that if the target is linked, it is linked
   * with another client. If it was linked with @ep, we would have catched it
   * above, where we check if the client is linked.
   * In the capture case, we don't care, we just allow all clients to capture
   * from the same device.
   * In the playback case, we are certain that @ep has higher priority because
   * this function is being called after sorting all the client endpoints
   * and therefore we can safely unlink the previous client
   */
  if (!is_capture && !is_persistent && wp_endpoint_is_linked (target)) {
    g_debug ("Unlink target '%s' from other clients",
        wp_endpoint_get_name (target));
    wp_endpoint_unlink (target);
  }

  /* Link the client with the target */
  if (is_capture) {
    wp_endpoint_link_new (core, target, stream_id, ep, WP_STREAM_ID_NONE,
        on_endpoint_link_created, NULL);
  } else {
    wp_endpoint_link_new (core, ep, WP_STREAM_ID_NONE, target, stream_id,
        on_endpoint_link_created, NULL);
  }
}

static gint
compare_client_priority (gconstpointer a, gconstpointer b, gpointer user_data)
{
  GVariant *v = user_data;
  WpEndpoint *ae = *(const gpointer *) a;
  WpEndpoint *be = *(const gpointer *) b;
  gint ret = 0;

  /* if no role priorities are specified, we treat all roles as equal */
  if (v) {
    g_autofree gchar *a_role = NULL;
    g_autofree gchar *b_role = NULL;
    gint a_priority = 0, b_priority = 0;

    g_object_get (ae, "role", &a_role, NULL);
    g_object_get (be, "role", &b_role, NULL);

    if (a_role)
      g_variant_lookup (v, a_role, "i", &a_priority);
    if (b_role)
      g_variant_lookup (v, b_role, "i", &b_priority);

    /* return b - a in order to sort descending */
    ret = b_priority - a_priority;
  }

  return ret;
}

static gboolean
simple_policy_rescan_in_idle (WpSimplePolicy *self)
{
  g_autoptr (WpCore) core = wp_policy_get_core (WP_POLICY (self));
  g_autoptr (GPtrArray) endpoints = NULL;
  WpEndpoint *ep;
  gint i;

  g_debug ("rescanning for clients that need linking");

  endpoints = wp_endpoint_find (core, "Stream/Input/Audio");
  if (endpoints) {
    /* link all capture clients */
    for (i = 0; i < endpoints->len; i++) {
      ep = g_ptr_array_index (endpoints, i);
      handle_client (WP_POLICY (self), ep);
    }
  }
  g_clear_pointer (&endpoints, g_ptr_array_unref);

  endpoints = wp_endpoint_find (core, "Persistent/Stream/Input/Audio");
  if (endpoints) {
    /* link all persistent capture clients */
    for (i = 0; i < endpoints->len; i++) {
      ep = g_ptr_array_index (endpoints, i);
      handle_client (WP_POLICY (self), ep);
    }
  }
  g_clear_pointer (&endpoints, g_ptr_array_unref);

  endpoints = wp_endpoint_find (core, "Stream/Output/Audio");
  if (endpoints && endpoints->len > 0) {
    /* sort based on role priorities */
    g_ptr_array_sort_with_data (endpoints, compare_client_priority,
        self->role_priorities);

    /* link the highest priority client */
    ep = g_ptr_array_index (endpoints, 0);
    handle_client (WP_POLICY (self), ep);
  }
  g_clear_pointer (&endpoints, g_ptr_array_unref);

  endpoints = wp_endpoint_find (core, "Persistent/Stream/Output/Audio");
  if (endpoints) {
    /* link all persistent output clients */
    for (i = 0; i < endpoints->len; i++) {
      ep = g_ptr_array_index (endpoints, i);
      handle_client (WP_POLICY (self), ep);
    }
  }
  g_clear_pointer (&endpoints, g_ptr_array_unref);

  self->pending_rescan = 0;
  return G_SOURCE_REMOVE;
}

static void
simple_policy_rescan (WpSimplePolicy *self)
{
  if (!self->pending_rescan)
    self->pending_rescan = g_idle_add (
        (GSourceFunc) simple_policy_rescan_in_idle, self);
}

static gboolean
simple_policy_handle_endpoint (WpPolicy *policy, WpEndpoint *ep)
{
  WpSimplePolicy *self = WP_SIMPLE_POLICY (policy);
  const char *media_class = NULL;

  /* TODO: For now we only accept audio stream clients */
  media_class = wp_endpoint_get_media_class(ep);
  if (!g_str_has_suffix (media_class, "Audio")) {
    return FALSE;
  }

  /* Schedule a rescan that will handle the endpoint */
  simple_policy_rescan (self);
  return TRUE;
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

  /* Get the endpoint with the "selected" flag (if it is an alsa endpoint) */
  for (i = 0; i < ptr_array->len; i++) {
    ep = g_ptr_array_index (ptr_array, i);
    if (g_str_has_prefix (media_class, "Audio/")) {
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
  }

  /* If not found, return the first endpoint */
  ep = (ptr_array->len > 0) ?
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
  p->role_priorities = g_variant_lookup_value (args, "role-priorities",
      G_VARIANT_TYPE ("a{si}"));
  wp_policy_register (WP_POLICY (p), core);
}
