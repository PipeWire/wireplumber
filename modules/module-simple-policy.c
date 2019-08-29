/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

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
try_select_new_endpoint (WpSimplePolicy *self, gint direction,
    const gchar *media_class)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (GPtrArray) ptr_array = NULL;
  WpEndpoint *other;
  guint32 control_id;
  gint i;

  /* Get the list of endpoints matching the media class */
  core = wp_policy_get_core (WP_POLICY (self));
  ptr_array = wp_endpoint_find (core, media_class);

  /* Find the endpoint in the list */
  for (i = 0; i < (ptr_array ? ptr_array->len : 0); i++) {
    other = g_ptr_array_index (ptr_array, i);
    if (g_str_has_prefix (media_class, "Alsa/")) {
      /* If Alsa, select the "selected" endpoint */
      control_id =
          wp_endpoint_find_control (other, WP_STREAM_ID_NONE, "selected");
      if (control_id == WP_CONTROL_ID_NONE)
        continue;
      select_endpoint (self, direction, other, control_id);
      return TRUE;
    } else {
      /* If non-Alsa (Bluez and Stream), select the first endpoint */
      select_endpoint (self, direction, other, WP_CONTROL_ID_NONE);
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
select_new_endpoint (WpSimplePolicy *self)
{
  gint direction;
  const gchar *bluez_headunit_media_class = NULL;
  const gchar *bluez_a2dp_media_class = NULL;
  const gchar *alsa_media_class = NULL;

  if (!self->selected[DIRECTION_SINK]) {
    direction = DIRECTION_SINK;
    bluez_headunit_media_class = "Bluez/Sink/Headunit";
    bluez_a2dp_media_class = "Bluez/Sink/A2dp";
    alsa_media_class = "Alsa/Sink";
  } else if (!self->selected[DIRECTION_SOURCE]) {
    direction = DIRECTION_SOURCE;
    bluez_headunit_media_class = "Bluez/Source/Headunit";
    bluez_a2dp_media_class = "Bluez/Source/A2dp";
    alsa_media_class = "Alsa/Source";
  } else
    return G_SOURCE_REMOVE;

  /* Bluez has higher priority than Alsa. Bluez A2DP profile has lower
   * priority than Bluez non-gatewat profile (Headunit). Bluez Gateway profiles
   * are not handled here because they always need to be linked with Alsa
   * endpoints, so the priority list is as folows (from higher to lower):
   * - Bluez Headunit
   * - Bluez A2DP
   * - Alsa
   */

  /* Try to select a Bluez Headunit endpoint */
  if (try_select_new_endpoint (self, direction, bluez_headunit_media_class))
    return G_SOURCE_REMOVE;

  /* Try to select a Bluez A2dp endpoint */
  if (try_select_new_endpoint (self, direction, bluez_a2dp_media_class))
    return G_SOURCE_REMOVE;

  /* Try to select an Alsa endpoint */
  try_select_new_endpoint (self, direction, alsa_media_class);

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
  if (!g_str_has_prefix (media_class, "Alsa/"))
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

static gboolean
link_endpoint (WpPolicy *policy, WpEndpoint *ep, GVariant *target_props)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpEndpoint) target = NULL;
  guint32 stream_id;
  guint direction;
  gboolean is_capture = FALSE;

  /* Check if the endpoint is capture or not */
  direction = wp_endpoint_get_direction (WP_ENDPOINT (ep));
  switch (direction) {
  case PW_DIRECTION_INPUT:
    is_capture = TRUE;
    break;
  case PW_DIRECTION_OUTPUT:
    is_capture = FALSE;
    break;
  default:
    return FALSE;
  }

  core = wp_policy_get_core (policy);
  target = wp_policy_find_endpoint (core, target_props, &stream_id);
  if (!target)
    return FALSE;

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
      return TRUE;
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
  if (wp_endpoint_is_linked (target) && !is_capture) {
    g_debug ("Unlink target '%s' from other clients",
        wp_endpoint_get_name (target));
    wp_endpoint_unlink (target);
  }

  /* Link the endpoint with the target */
  if (is_capture) {
    wp_endpoint_link_new (core, target, stream_id, ep, 0,
        on_endpoint_link_created, NULL);
  } else {
    wp_endpoint_link_new (core, ep, 0, target, stream_id,
        on_endpoint_link_created, NULL);
  }

  return TRUE;
}

static void
handle_client (WpPolicy *policy, WpEndpoint *ep)
{
  const char *media_class = wp_endpoint_get_media_class(ep);
  GVariantDict d;
  gboolean is_capture = FALSE;
  const gchar *role, *target_name = NULL;

  /* Detect if the client is doing capture or playback */
  is_capture = g_str_has_prefix (media_class, "Stream/Input");

  /* All Stream client endpoints need to be linked with a Bluez non-gateway
   * endpoint if any. If there is no Bluez non-gateway endpoints, the Stream
   * client needs to be linked with a Bluez A2DP endpoint. Finally, if none
   * of the previous endpoints are found, the Stream client needs to be linked
   * with an Alsa endpoint.
   */

  /* Link endpoint with Bluez non-gateway target endpoint */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "action", "s", "link");
  g_variant_dict_insert (&d, "media.class", "s",
      is_capture ? "Bluez/Source/Headunit" : "Bluez/Sink/Headunit");
  if (link_endpoint (policy, ep, g_variant_dict_end (&d)))
    return;

  /* Link endpoint with Bluez A2DP target endpoint */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "action", "s", "link");
  g_variant_dict_insert (&d, "media.class", "s",
      is_capture ? "Bluez/Source/A2dp" : "Bluez/Sink/A2dp");
  if (link_endpoint (policy, ep, g_variant_dict_end (&d)))
    return;

  /* Link endpoint with Alsa target endpoint */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "action", "s", "link");
  g_variant_dict_insert (&d, "media.class", "s",
      is_capture ? "Alsa/Source" : "Alsa/Sink");
  g_object_get (ep, "role", &role, NULL);
  if (role)
    g_variant_dict_insert (&d, "media.role", "s", role);
  g_object_get (ep, "target", &target_name, NULL);
  if (target_name)
    g_variant_dict_insert (&d, "media.name", "s", target_name);
  if (!link_endpoint (policy, ep, g_variant_dict_end (&d)))
    g_info ("Could not find alsa target endpoint for client stream");
}

static void
handle_bluez_non_gateway (WpPolicy *policy, WpEndpoint *ep)
{
  GVariantDict d;
  const char *media_class = wp_endpoint_get_media_class(ep);
  gboolean is_sink = FALSE;

  /* All bluetooth non-gateway endpoints (A2DP/HSP_HS/HFP_HF) always
   * need to be linked with the stream endpoints so that the computer
   * does not play any sound
   */

  /* Detect if the client is a sink or not */
  is_sink = g_str_has_prefix (media_class, "Bluez/Sink");

  /* Link endpoint with Stream target endpoint */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "action", "s", "link");
  g_variant_dict_insert (&d, "media.class", "s",
      is_sink ? "Stream/Output/Audio" : "Stream/Input/Audio");
  if (!link_endpoint (policy, ep, g_variant_dict_end (&d)))
    g_info ("Could not find stream target endpoint for non-gateway bluez");
}

static void
handle_bluez_gateway (WpPolicy *policy, WpEndpoint *ep)
{
  /* All bluetooth gateway endpoints (HSP_GW/HFP_GW) always need to
   * be linked with the alsa endpoints so that the computer can act
   * as a head unit
   */
  GVariantDict d;
  const char *media_class = wp_endpoint_get_media_class(ep);
  gboolean is_sink = FALSE;

  /* Detect if the client is a sink or not */
  is_sink = g_str_has_prefix (media_class, "Bluez/Sink");

  /* Link endpoint with Alsa target endpoint */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "action", "s", "link");
  g_variant_dict_insert (&d, "media.class", "s",
      is_sink ? "Alsa/Source" : "Alsa/Sink");
  if (!link_endpoint (policy, ep, g_variant_dict_end (&d)))
    g_info ("Could not find alsa target endpoint for gateway bluez");
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

  /* when role priority is equal, the newest client wins */
  if (ret == 0) {
    guint64 a_time = 0, b_time = 0;

    g_object_get (ae, "creation-time", &a_time, NULL);
    g_object_get (be, "creation-time", &b_time, NULL);

    /* since a_time and b_time are expressed in system monotonic time,
     * there is absolutely no chance that they will be equal */
    ret = (b_time > a_time) ? 1 : -1;
  }

  return ret;
}

static gint
compare_bluez_non_gateway_priority (gconstpointer a, gconstpointer b,
    gpointer user_data)
{
  WpEndpoint *ae = *(const gpointer *) a;
  WpEndpoint *be = *(const gpointer *) b;
  const char *a_media_class, *b_media_class;
  gint a_priority, b_priority;

  /* Bluez priorities (Gateway is a different case):
   - Headset (1)
   - A2dp (0)
  */

  /* Get endpoint A priority */
  a_media_class = wp_endpoint_get_media_class(ae);
  a_priority = g_str_has_suffix (a_media_class, "Headset") ? 1 : 0;

  /* Get endpoint B priority */
  b_media_class = wp_endpoint_get_media_class(be);
  b_priority = g_str_has_suffix (b_media_class, "Headset") ? 1 : 0;

  /* Return the difference of both priorities */
  return a_priority - b_priority;
}

static gint
compare_bluez_gateway_priority (gconstpointer a, gconstpointer b,
    gpointer user_data)
{
  /* Since Bluez Gateway profile does not have any priorities, just
   * return positive to indicate endpoint A has higher priority than
   * endpoint B */
  return 1;
}

static void
rescan_sink_endpoints (WpSimplePolicy *self, const gchar *media_class,
    void (*handler) (WpPolicy *policy, WpEndpoint *ep))
{
  g_autoptr (WpCore) core = wp_policy_get_core (WP_POLICY (self));
  g_autoptr (GPtrArray) endpoints = NULL;
  WpEndpoint *ep;
  gint i;

  endpoints = wp_endpoint_find (core, media_class);
  if (endpoints) {
    /* link all sink endpoints */
    for (i = 0; i < endpoints->len; i++) {
      ep = g_ptr_array_index (endpoints, i);
      handler (WP_POLICY (self), ep);
    }
  }
}

static void
rescan_source_endpoints (WpSimplePolicy *self, const gchar *media_class,
    void (*handle) (WpPolicy *policy, WpEndpoint *ep),
    GCompareDataFunc comp_func)
{
  g_autoptr (WpCore) core = wp_policy_get_core (WP_POLICY (self));
  g_autoptr (GPtrArray) endpoints = NULL;
  WpEndpoint *ep;

  endpoints = wp_endpoint_find (core, media_class);
  if (endpoints && endpoints->len > 0) {
    /* sort based on priorities */
    g_ptr_array_sort_with_data (endpoints, comp_func, self->role_priorities);

    /* link the highest priority */
    ep = g_ptr_array_index (endpoints, 0);
    handle (WP_POLICY (self), ep);
  }
}

static gboolean
simple_policy_rescan_in_idle (WpSimplePolicy *self)
{
  /* Alsa endpoints are never handled */

  /* Handle clients */
  rescan_sink_endpoints (self, "Stream/Input/Audio", handle_client);
  rescan_source_endpoints (self, "Stream/Output/Audio", handle_client,
      compare_client_priority);

  /* Handle Bluez non-gateway */
  rescan_sink_endpoints (self, "Bluez/Sink/Headunit", handle_bluez_non_gateway);
  rescan_source_endpoints (self, "Bluez/Source/Headunit",
      handle_bluez_non_gateway, compare_bluez_non_gateway_priority);
  rescan_sink_endpoints (self, "Bluez/Sink/A2dp", handle_bluez_non_gateway);
  rescan_source_endpoints (self, "Bluez/Source/A2dp", handle_bluez_non_gateway,
      compare_bluez_non_gateway_priority);

  /* Handle Bluez gateway */
  rescan_sink_endpoints (self, "Bluez/Sink/Gateway", handle_bluez_gateway);
  rescan_source_endpoints (self, "Bluez/Source/Gateway",
      handle_bluez_gateway, compare_bluez_gateway_priority);

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
  const char *media_class = wp_endpoint_get_media_class(ep);

  /* Schedule rescan only if endpoint is audio stream or bluez */
  if ((g_str_has_prefix (media_class, "Stream") &&
      g_str_has_suffix (media_class, "Audio")) ||
      g_str_has_prefix (media_class, "Bluez")) {
    simple_policy_rescan (self);
    return TRUE;
  }

  return FALSE;
}

static WpEndpoint *
simple_policy_find_endpoint (WpPolicy *policy, GVariant *props,
    guint32 *stream_id)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (GPtrArray) ptr_array = NULL;
  const char *action = NULL;
  const char *name = NULL;
  const char *media_class = NULL;
  const char *role = NULL;
  WpEndpoint *ep;
  int i;

  core = wp_policy_get_core (policy);

  g_variant_lookup (props, "action", "&s", &action);
  g_variant_lookup (props, "media.name", "&s", &name);

  /* Get all the endpoints with the specific media class*/
  g_variant_lookup (props, "media.class", "&s", &media_class);
  ptr_array = wp_endpoint_find (core, media_class);
  if (!ptr_array)
    return NULL;

  /* Find the endpoint with the matching name, otherwise get the one with the
   * "selected" flag (if it is an alsa endpoint) */
  for (i = 0; i < ptr_array->len; i++) {
    ep = g_ptr_array_index (ptr_array, i);
    if (name) {
      if (g_str_has_prefix(wp_endpoint_get_name (ep), name)) {
        g_object_ref (ep);
        goto select_stream;
      }
    } else if (g_str_has_prefix (media_class, "Alsa/")) {
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

  /* Don't select any stream if it is not an alsa endpoint */
  if (!g_str_has_prefix (media_class, "Alsa/"))
    return ep;

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
