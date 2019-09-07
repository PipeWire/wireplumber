/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

G_DECLARE_FINAL_TYPE (WpMixerEndpoint,
    mixer_endpoint, WP, MIXER_ENDPOINT, WpEndpoint)

enum {
  PROP_0,
  PROP_STREAMS,
};

enum {
  CONTROL_VOLUME = 0,
  CONTROL_MUTE,
  N_CONTROLS,
};

#define MIXER_CONTROL_ID(stream_id, control_enum) \
    (stream_id * N_CONTROLS + control_enum)

struct group
{
  WpMixerEndpoint *mixer;
  gchar *name;
  guint32 mixer_stream_id;

  GWeakRef backend;
  guint32 backend_ctl_ids[N_CONTROLS];
};

struct _WpMixerEndpoint
{
  WpEndpoint parent;
  GVariant *streams;
  GArray *groups;
};

G_DEFINE_TYPE (WpMixerEndpoint, mixer_endpoint, WP_TYPE_ENDPOINT)

static void
backend_value_changed (WpEndpoint *backend, guint32 control_id,
    struct group *group)
{
  gint i;
  for (i = 0; i < N_CONTROLS; i++) {
    if (control_id == group->backend_ctl_ids[i]) {
      g_signal_emit_by_name (group->mixer, "notify-control-value",
          MIXER_CONTROL_ID (group->mixer_stream_id, i));
    }
  }
}

static void
group_find_backend (struct group *group, WpCore *core)
{
  g_autoptr (WpEndpoint) backend = NULL;
  g_autoptr (WpEndpoint) old_backend = NULL;
  guint32 stream_id;
  GVariantDict d;

  /* find the backend */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "action", "s", "mixer");
  g_variant_dict_insert (&d, "media.class", "s", "Alsa/Sink");
  g_variant_dict_insert (&d, "media.role", "s", group->name);

  backend = wp_policy_find_endpoint (core, g_variant_dict_end (&d),
      &stream_id);
  if (!backend)
    return;

  /* we found the same backend as before - no need to continue */
  old_backend = g_weak_ref_get (&group->backend);
  if (old_backend && old_backend == backend)
    return;

  /* attach to the backend */
  g_weak_ref_set (&group->backend, backend);
  group->backend_ctl_ids[CONTROL_VOLUME] = wp_endpoint_find_control (backend,
      stream_id, "volume");
  group->backend_ctl_ids[CONTROL_MUTE] = wp_endpoint_find_control (backend,
      stream_id, "mute");

  /* notify of changed values */
  g_signal_emit_by_name (group->mixer, "notify-control-value",
      MIXER_CONTROL_ID (group->mixer_stream_id, CONTROL_VOLUME));
  g_signal_emit_by_name (group->mixer, "notify-control-value",
      MIXER_CONTROL_ID (group->mixer_stream_id, CONTROL_MUTE));

  /* watch for further value changes in the backend */
  g_signal_connect (backend, "notify-control-value",
      (GCallback) backend_value_changed, group);
}

static void
policy_changed (WpPolicyManager *mgr, WpMixerEndpoint * self)
{
  int i;
  g_autoptr (WpCore) core = wp_endpoint_get_core (WP_ENDPOINT (self));

  for (i = 0; i < self->groups->len; i++) {
    group_find_backend (&g_array_index (self->groups, struct group, i), core);
  }
}

static void
mixer_endpoint_init (WpMixerEndpoint * self)
{
  self->groups = g_array_new (FALSE, TRUE, sizeof (struct group));
}

static void
mixer_endpoint_constructed (GObject * object)
{
  WpMixerEndpoint *self = WP_MIXER_ENDPOINT (object);
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpPolicyManager) policymgr = NULL;
  struct group empty_group = {0};
  GVariantDict d;
  GVariantIter iter;
  gint i;
  gchar *stream;

  core = wp_endpoint_get_core (WP_ENDPOINT (self));
  policymgr = wp_policy_manager_get_instance (core);
  g_signal_connect_object (policymgr, "policy-changed",
      (GCallback) policy_changed, self, 0);

  g_variant_iter_init (&iter, self->streams);
  for (i = 0; g_variant_iter_next (&iter, "s", &stream); i++) {
    struct group *group;

    g_array_append_val (self->groups, empty_group);
    group = &g_array_index (self->groups, struct group, i);

    g_variant_dict_init (&d, NULL);
    g_variant_dict_insert (&d, "id", "u", i);
    g_variant_dict_insert (&d, "name", "s", stream);
    wp_endpoint_register_stream (WP_ENDPOINT (self), g_variant_dict_end (&d));

    g_variant_dict_init (&d, NULL);
    g_variant_dict_insert (&d, "id", "u", MIXER_CONTROL_ID (i, CONTROL_VOLUME));
    g_variant_dict_insert (&d, "stream-id", "u", i);
    g_variant_dict_insert (&d, "name", "s", "volume");
    g_variant_dict_insert (&d, "type", "s", "d");
    g_variant_dict_insert (&d, "range", "(dd)", 0.0, 1.0);
    g_variant_dict_insert (&d, "default-value", "d", 1.0);
    wp_endpoint_register_control (WP_ENDPOINT (self), g_variant_dict_end (&d));

    g_variant_dict_init (&d, NULL);
    g_variant_dict_insert (&d, "id", "u", MIXER_CONTROL_ID (i, CONTROL_MUTE));
    g_variant_dict_insert (&d, "stream-id", "u", i);
    g_variant_dict_insert (&d, "name", "s", "mute");
    g_variant_dict_insert (&d, "type", "s", "b");
    g_variant_dict_insert (&d, "default-value", "b", FALSE);
    wp_endpoint_register_control (WP_ENDPOINT (self), g_variant_dict_end (&d));

    group->mixer = self;
    group->name = stream;
    group->mixer_stream_id = i;
    g_weak_ref_init (&group->backend, NULL);

    group_find_backend (group, core);
  }

  G_OBJECT_CLASS (mixer_endpoint_parent_class)->constructed (object);
}

static void
mixer_endpoint_finalize (GObject * object)
{
  WpMixerEndpoint *self = WP_MIXER_ENDPOINT (object);
  gint i;

  for (i = 0; i < self->groups->len; i++) {
    struct group *group = &g_array_index (self->groups, struct group, i);
    g_autoptr (WpEndpoint) backend = g_weak_ref_get (&group->backend);
    if (backend)
      g_signal_handlers_disconnect_by_data (backend, group);
    g_weak_ref_clear (&group->backend);
    g_free (group->name);
  }

  g_clear_pointer (&self->groups, g_array_unref);
  g_clear_pointer (&self->streams, g_variant_unref);

  G_OBJECT_CLASS (mixer_endpoint_parent_class)->finalize (object);
}

static void
mixer_endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpMixerEndpoint *self = WP_MIXER_ENDPOINT (object);

  switch (property_id) {
  case PROP_STREAMS:
    self->streams = g_value_dup_variant (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static GVariant *
mixer_endpoint_get_control_value (WpEndpoint * ep, guint32 control_id)
{
  WpMixerEndpoint *self = WP_MIXER_ENDPOINT (ep);
  guint32 stream_id;
  struct group *group;
  g_autoptr (WpEndpoint) backend = NULL;

  stream_id = control_id / N_CONTROLS;
  control_id = control_id % N_CONTROLS;

  if (stream_id >= self->groups->len) {
    g_warning ("Mixer:%p Invalid stream id %u", self, stream_id);
    return NULL;
  }

  group = &g_array_index (self->groups, struct group, stream_id);
  backend = g_weak_ref_get (&group->backend);

  /* if there is no backend, return the default value */
  if (!backend) {
    g_debug ("Mixer:%p Cannot get control value - no backend", self);

    switch (control_id) {
    case CONTROL_VOLUME:
      return g_variant_new_double (1.0);
    case CONTROL_MUTE:
      return g_variant_new_boolean (FALSE);
    default:
      g_assert_not_reached ();
    }
  }

  /* otherwise return the value provided by the backend */
  return wp_endpoint_get_control_value (backend,
      group->backend_ctl_ids[control_id]);
}

static gboolean
mixer_endpoint_set_control_value (WpEndpoint * ep, guint32 control_id,
    GVariant * value)
{
  WpMixerEndpoint *self = WP_MIXER_ENDPOINT (ep);
  guint32 stream_id;
  struct group *group;
  g_autoptr (WpEndpoint) backend = NULL;

  stream_id = control_id / N_CONTROLS;
  control_id = control_id % N_CONTROLS;

  if (stream_id >= self->groups->len) {
    g_warning ("Mixer:%p Invalid stream id %u", self, stream_id);
    return FALSE;
  }

  group = &g_array_index (self->groups, struct group, stream_id);
  backend = g_weak_ref_get (&group->backend);

  if (!backend) {
    g_debug ("Mixer:%p Cannot set control value - no backend", self);
    return FALSE;
  }

  return wp_endpoint_set_control_value (backend,
      group->backend_ctl_ids[control_id], value);
}

static void
mixer_endpoint_class_init (WpMixerEndpointClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpEndpointClass *endpoint_class = (WpEndpointClass *) klass;

  object_class->set_property = mixer_endpoint_set_property;
  object_class->constructed = mixer_endpoint_constructed;
  object_class->finalize = mixer_endpoint_finalize;

  endpoint_class->get_control_value = mixer_endpoint_get_control_value;
  endpoint_class->set_control_value = mixer_endpoint_set_control_value;

  g_object_class_install_property (object_class, PROP_STREAMS,
      g_param_spec_variant ("streams", "streams",
          "The stream names for the streams to create",
          G_VARIANT_TYPE ("as"), NULL,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
remote_connected (WpRemote *remote, WpRemoteState state, GVariant *streams)
{
  g_autoptr (WpCore) core = wp_remote_get_core (remote);
  g_autoptr (WpEndpoint) ep = g_object_new (mixer_endpoint_get_type (),
      "core", core,
      "name", "Mixer",
      "media-class", "Mixer/Audio",
      "streams", streams,
      NULL);
  wp_endpoint_register (ep);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  WpRemote *remote;
  GVariant *streams;

  remote = wp_core_get_global (core, WP_GLOBAL_REMOTE_PIPEWIRE);
  g_return_if_fail (remote != NULL);

  streams = g_variant_lookup_value (args, "streams", G_VARIANT_TYPE ("as"));

  g_signal_connect_data (remote, "state-changed::connected",
      (GCallback) remote_connected, streams, (GClosureNotify) g_variant_unref,
      0);
}
