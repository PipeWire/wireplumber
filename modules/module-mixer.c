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

static const char * streams[] = {
  "Master"
};
#define N_STREAMS (sizeof (streams) / sizeof (const char *))

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
  const gchar *name;
  guint32 mixer_stream_id;

  GWeakRef backend;
  guint32 backend_ctl_ids[N_CONTROLS];
};

struct _WpMixerEndpoint
{
  WpEndpoint parent;
  struct group groups[N_STREAMS];
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
  g_variant_dict_insert (&d, "media.class", "s", "Audio/Sink");
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

  for (i = 0; i < N_STREAMS; i++) {
    group_find_backend (&self->groups[i], core);
  }
}

static void
mixer_endpoint_init (WpMixerEndpoint * self)
{
}

static void
mixer_endpoint_constructed (GObject * object)
{
  WpMixerEndpoint *self = WP_MIXER_ENDPOINT (object);
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpPolicyManager) policymgr = NULL;
  GVariantDict d;
  gint i;

  core = wp_endpoint_get_core (WP_ENDPOINT (self));
  policymgr = wp_policy_manager_get_instance (core);
  g_signal_connect_object (policymgr, "policy-changed",
      (GCallback) policy_changed, self, 0);

  for (i = 0; i < N_STREAMS; i++) {
    g_variant_dict_init (&d, NULL);
    g_variant_dict_insert (&d, "id", "u", i);
    g_variant_dict_insert (&d, "name", "s", streams[i]);
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

    self->groups[i].mixer = self;
    self->groups[i].name = streams[i];
    self->groups[i].mixer_stream_id = i;
    g_weak_ref_init (&self->groups[i].backend, NULL);

    group_find_backend (&self->groups[i], core);
  }

  G_OBJECT_CLASS (mixer_endpoint_parent_class)->constructed (object);
}

static void
mixer_endpoint_finalize (GObject * object)
{
  WpMixerEndpoint *self = WP_MIXER_ENDPOINT (object);
  gint i;

  for (i = 0; i < N_STREAMS; i++) {
    g_autoptr (WpEndpoint) backend = g_weak_ref_get (&self->groups[i].backend);
    if (backend)
      g_signal_handlers_disconnect_by_data (backend, &self->groups[i]);
    g_weak_ref_clear (&self->groups[i].backend);
  }

  G_OBJECT_CLASS (mixer_endpoint_parent_class)->finalize (object);
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

  if (stream_id >= N_STREAMS) {
    g_warning ("Mixer:%p Invalid stream id %u", self, stream_id);
    return NULL;
  }

  group = &self->groups[stream_id];
  backend = g_weak_ref_get (&group->backend);

  /* if there is no backend, return the default value */
  if (!backend) {
    g_warning ("Mixer:%p Cannot get control value - no backend", self);

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

  if (stream_id >= N_STREAMS) {
    g_warning ("Mixer:%p Invalid stream id %u", self, stream_id);
    return FALSE;
  }

  group = &self->groups[stream_id];
  backend = g_weak_ref_get (&group->backend);

  if (!backend) {
    g_warning ("Mixer:%p Cannot set control value - no backend", self);
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

  object_class->constructed = mixer_endpoint_constructed;
  object_class->finalize = mixer_endpoint_finalize;

  endpoint_class->get_control_value = mixer_endpoint_get_control_value;
  endpoint_class->set_control_value = mixer_endpoint_set_control_value;
}

static void
remote_connected (WpRemote *remote, WpRemoteState state, WpCore *core)
{
  g_autoptr (WpEndpoint) ep = g_object_new (mixer_endpoint_get_type (),
      "core", core,
      "name", "Mixer",
      "media-class", "Mixer/Audio",
      NULL);
  wp_endpoint_register (ep);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  WpRemote *remote;

  remote = wp_core_get_global (core, WP_GLOBAL_REMOTE_PIPEWIRE);
  g_return_if_fail (remote != NULL);

  g_signal_connect (remote, "state-changed::connected",
      (GCallback) remote_connected, core);
}
