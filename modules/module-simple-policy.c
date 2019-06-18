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
};

G_DECLARE_FINAL_TYPE (WpSimplePolicy, simple_policy, WP, SIMPLE_POLICY, WpPolicy)
G_DEFINE_TYPE (WpSimplePolicy, simple_policy, WP_TYPE_POLICY)

static void
simple_policy_init (WpSimplePolicy *self)
{
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
  g_debug ("selecting %s %p (%s)",
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

static gboolean
simple_policy_handle_endpoint (WpPolicy *policy, WpEndpoint *ep)
{
  const char *media_class = NULL;
  GVariantDict d;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpEndpoint) target = NULL;
  g_autoptr (GError) error = NULL;
  guint32 stream_id;

  /* TODO: For now we only accept audio output clients */
  media_class = wp_endpoint_get_media_class(ep);
  if (!g_str_equal (media_class, "Stream/Output/Audio"))
    return FALSE;

  /* Locate the target endpoint */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "action", "s", "link");
  g_variant_dict_insert (&d, "media.class", "s", "Audio/Sink");
  /* TODO: more properties are needed here */

  core = wp_policy_get_core (policy);
  target = wp_policy_find_endpoint (core, g_variant_dict_end (&d), &stream_id);
  if (!target) {
    g_warning ("Could not find an Audio/Sink target endpoint\n");
    /* TODO: we should kill the client, otherwise it's going to hang waiting */
    return FALSE;
  }

  /* Link the client with the target */
  if (!wp_endpoint_link_new (core, ep, 0, target, stream_id, &error)) {
    g_warning ("Could not link endpoints: %s\n", error->message);
  } else {
    g_info ("Sucessfully linked '%s' to '%s'\n", wp_endpoint_get_name (ep),
        wp_endpoint_get_name (target));
  }

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

  /* TODO: for now we statically return the first stream
   * we should be looking into the media.role eventually */
  g_variant_lookup (props, "media.role", "&s", &role);
  if (!g_strcmp0 (action, "mixer") && !g_strcmp0 (role, "Master"))
    *stream_id = WP_STREAM_ID_NONE;
  else
    *stream_id = 0;

  /* Find and return the "selected" endpoint */
  /* FIXME: fix the endpoint API, this is terrible */
  for (i = 0; i < ptr_array->len; i++) {
    ep = g_ptr_array_index (ptr_array, i);
    GVariantIter iter;
    g_autoptr (GVariant) controls = NULL;
    g_autoptr (GVariant) value = NULL;
    const gchar *name;
    guint id;

    controls = wp_endpoint_list_controls (ep);
    g_variant_iter_init (&iter, controls);
    while ((value = g_variant_iter_next_value (&iter))) {
      if (!g_variant_lookup (value, "name", "&s", &name)
          || !g_str_equal (name, "selected")) {
        g_variant_unref (value);
        continue;
      }
      g_variant_lookup (value, "id", "u", &id);
      g_variant_unref (value);
    }

    value = wp_endpoint_get_control_value (ep, id);
    if (value && g_variant_get_boolean (value))
      return g_object_ref (ep);
  }

  /* If not found, return the first endpoint */
  ep = (ptr_array->len > 1) ? g_ptr_array_index (ptr_array, 0) : NULL;
  return g_object_ref (ep);
}

static void
simple_policy_class_init (WpSimplePolicyClass *klass)
{
  WpPolicyClass *policy_class = (WpPolicyClass *) klass;

  policy_class->endpoint_added = simple_policy_endpoint_added;
  policy_class->endpoint_removed = simple_policy_endpoint_removed;
  policy_class->handle_endpoint = simple_policy_handle_endpoint;
  policy_class->find_endpoint = simple_policy_find_endpoint;
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  WpPolicy *p = g_object_new (simple_policy_get_type (),
      "rank", WP_POLICY_RANK_UPSTREAM,
      NULL);
  wp_policy_register (p, core);
}
