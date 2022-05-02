/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <math.h>
#include <pipewire/pipewire.h>
#include <spa/pod/iter.h>
#include <spa/param/audio/raw.h>

struct volume {
  uint8_t channels;
  float values[SPA_AUDIO_MAX_CHANNELS];
};

struct channel_map {
  uint8_t channels;
  uint32_t map[SPA_AUDIO_MAX_CHANNELS];
};

struct node_info {
  guint32 seq;

  guint32 device_id;
  gint32 route_index;
  gint32 route_device;

  struct volume volume;
  struct volume monitorVolume;
  struct channel_map map;
  bool mute;
  float svolume;
  float base;
  float step;
};

struct _WpMixerApi
{
  WpPlugin parent;
  WpObjectManager *om;
  GHashTable *node_infos;
  guint32 seq;

  /* properties */
  gint scale;
};

enum {
  ACTION_SET_VOLUME,
  ACTION_GET_VOLUME,
  SIGNAL_CHANGED,
  N_SIGNALS
};

enum {
  PROP_0,
  PROP_SCALE,
};

static guint signals[N_SIGNALS] = {0};

G_DECLARE_FINAL_TYPE (WpMixerApi, wp_mixer_api, WP, MIXER_API, WpPlugin)
G_DEFINE_TYPE (WpMixerApi, wp_mixer_api, WP_TYPE_PLUGIN)

enum {
  SCALE_LINEAR,
  SCALE_CUBIC,
};

static GType
wp_mixer_api_volume_scale_enum_get_type (void)
{
  static gsize gtype_id = 0;
  static const GEnumValue values[] = {
    { (gint) SCALE_LINEAR, "SCALE_LINEAR", "linear" },
    { (gint) SCALE_CUBIC, "SCALE_CUBIC", "cubic" },
    { 0, NULL, NULL }
  };
  if (g_once_init_enter (&gtype_id)) {
    GType new_type = g_enum_register_static (
        g_intern_static_string ("WpMixerApiVolumeScale"), values);
    g_once_init_leave (&gtype_id, new_type);
  }
  return (GType) gtype_id;
}

static void
wp_mixer_api_init (WpMixerApi * self)
{
}

static void
wp_mixer_api_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpMixerApi *self = WP_MIXER_API (object);

  switch (property_id) {
  case PROP_SCALE:
    g_value_set_enum (value, self->scale);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_mixer_api_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpMixerApi *self = WP_MIXER_API (object);

  switch (property_id) {
  case PROP_SCALE:
    self->scale = g_value_get_enum (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static gboolean
node_info_fill (struct node_info * info, WpSpaPod * props)
{
  g_autoptr (WpSpaPod) channelVolumes = NULL;
  g_autoptr (WpSpaPod) channelMap = NULL;
  g_autoptr (WpSpaPod) monitorVolumes = NULL;

  if (!wp_spa_pod_get_object (props, NULL,
          "mute", "b", &info->mute,
          "channelVolumes", "P", &channelVolumes,
          NULL))
    return FALSE;

  /* default values */
  info->svolume = 1.0;
  info->base = 1.0;
  info->step = 1.0 / 65536.0;

  wp_spa_pod_get_object (props, NULL,
      "channelMap", "?P", &channelMap,
      "volumeBase", "?f", &info->base,
      "volumeStep", "?f", &info->step,
      "volume",     "?f", &info->svolume,
      "monitorVolumes", "?P", &monitorVolumes,
      NULL);

  info->volume.channels = spa_pod_copy_array (
      wp_spa_pod_get_spa_pod (channelVolumes), SPA_TYPE_Float,
      info->volume.values, SPA_AUDIO_MAX_CHANNELS);

  if (channelMap)
    info->map.channels = spa_pod_copy_array (
        wp_spa_pod_get_spa_pod (channelMap), SPA_TYPE_Id,
        info->map.map, SPA_AUDIO_MAX_CHANNELS);

  if (monitorVolumes)
    info->monitorVolume.channels = spa_pod_copy_array (
        wp_spa_pod_get_spa_pod (monitorVolumes), SPA_TYPE_Float,
        info->monitorVolume.values, SPA_AUDIO_MAX_CHANNELS);

  return TRUE;
}

static void
collect_node_info (WpMixerApi * self, struct node_info *info,
    WpPipewireObject * node)
{
  g_autoptr (WpPipewireObject) dev = NULL;
  const gchar *str = NULL;
  gboolean have_volume = FALSE;

  info->device_id = SPA_ID_INVALID;
  info->route_index = -1;
  info->route_device = -1;

  if ((str = wp_pipewire_object_get_property (node, PW_KEY_DEVICE_ID))) {
    dev = wp_object_manager_lookup (self->om, WP_TYPE_DEVICE,
        WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=s", str, NULL);
  }

  if (dev && (str = wp_pipewire_object_get_property (node, "card.profile.device"))) {
    gint32 p_device = atoi (str);
    g_autoptr (WpIterator) it = NULL;
    g_auto (GValue) val = G_VALUE_INIT;

    it = wp_pipewire_object_enum_params_sync (dev, "Route", NULL);
    for (; it && wp_iterator_next (it, &val); g_value_unset (&val)) {
      WpSpaPod *param = g_value_get_boxed (&val);
      gint32 r_index = -1, r_device = -1;
      g_autoptr (WpSpaPod) props = NULL;

      if (!wp_spa_pod_get_object (param, NULL,
              "index", "i", &r_index,
              "device", "i", &r_device,
              "props", "P", &props,
              NULL))
        continue;
      if (r_device != p_device)
        continue;

      if (props && node_info_fill (info, props)) {
        info->device_id = wp_proxy_get_bound_id (WP_PROXY (dev));
        info->route_index = r_index;
        info->route_device = r_device;
        have_volume = TRUE;
        g_value_unset (&val);
        break;
      }
    }
  }

  if (!have_volume) {
    g_autoptr (WpIterator) it = NULL;
    g_auto (GValue) val = G_VALUE_INIT;

    it = wp_pipewire_object_enum_params_sync (node, "Props", NULL);
    for (; it && wp_iterator_next (it, &val); g_value_unset (&val)) {
      WpSpaPod *param = g_value_get_boxed (&val);
      if (node_info_fill (info, param)) {
        g_value_unset (&val);
        break;
      }
    }
  }
}

static void on_objects_changed (WpObjectManager * om, WpMixerApi * self);

static void
on_sync_done (WpCore * core, GAsyncResult * res, WpMixerApi * self)
{
  g_autoptr (GError) error = NULL;
  if (!wp_core_sync_finish (core, res, &error))
    wp_warning_object (core, "sync error: %s", error->message);
  if (self->om) {
    on_objects_changed (self->om, self);
  }
}

static void
on_params_changed (WpPipewireObject * obj, const gchar * param_name,
    WpMixerApi * self)
{
  if ((WP_IS_NODE (obj) && !g_strcmp0 (param_name, "Props")) ||
      (WP_IS_DEVICE (obj) && !g_strcmp0 (param_name, "Route"))) {
    g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
    wp_core_sync (core, NULL, (GAsyncReadyCallback) on_sync_done, self);
  }
}

static void
on_objects_changed (WpObjectManager * om, WpMixerApi * self)
{
  g_autoptr (WpIterator) it =
      wp_object_manager_new_filtered_iterator (om, WP_TYPE_NODE, NULL);
  g_auto (GValue) val = G_VALUE_INIT;
  GHashTableIter infos_it;
  struct node_info *info;
  struct node_info old;

  self->seq++;

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpPipewireObject *node = g_value_get_object (&val);
    guint id = wp_proxy_get_bound_id (WP_PROXY (node));

    info = g_hash_table_lookup (self->node_infos, GUINT_TO_POINTER (id));
    if (!info) {
      info = g_slice_new0 (struct node_info);
      g_hash_table_insert (self->node_infos, GUINT_TO_POINTER (id), info);
    }
    info->seq = self->seq;

    old = *info;
    collect_node_info (self, info, node);
    if (memcmp (&old, info, sizeof (struct node_info)) != 0) {
      wp_debug_object (self, "node %u changed volume props", id);
      g_signal_emit (self, signals[SIGNAL_CHANGED], 0, id);
    }
  }

  /* remove node_info of nodes that were removed from the object manager */
  g_hash_table_iter_init (&infos_it, self->node_infos);
  while (g_hash_table_iter_next (&infos_it, NULL, (gpointer *) &info)) {
    if (info->seq != self->seq)
      g_hash_table_iter_remove (&infos_it);
  }
}

static void
on_object_added (WpObjectManager * om, WpProxy * obj, WpMixerApi * self)
{
  g_signal_connect (obj, "params-changed", G_CALLBACK (on_params_changed), self);
}

static void
on_object_removed (WpObjectManager * om, WpProxy * obj, WpMixerApi * self)
{
  g_signal_handlers_disconnect_by_func (obj, G_CALLBACK (on_params_changed), self);
}

static void
node_info_free (gpointer info)
{
  g_slice_free (struct node_info, info);
}

static void
on_om_installed (WpObjectManager * om, WpMixerApi * self)
{
  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_mixer_api_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpMixerApi * self = WP_MIXER_API (plugin);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  g_return_if_fail (core);

  self->node_infos = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, node_info_free);

  self->om = wp_object_manager_new ();
  wp_object_manager_add_interest (self->om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "media.class", "#s", "*Audio*",
      NULL);
  wp_object_manager_add_interest (self->om, WP_TYPE_DEVICE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "media.class", "=s", "Audio/Device",
      NULL);
  wp_object_manager_request_object_features (self->om,
      WP_TYPE_GLOBAL_PROXY, WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (self->om, "objects-changed",
      G_CALLBACK (on_objects_changed), self, 0);
  g_signal_connect_object (self->om, "object-added",
      G_CALLBACK (on_object_added), self, 0);
  g_signal_connect_object (self->om, "object-removed",
      G_CALLBACK (on_object_removed), self, 0);
  g_signal_connect_object (self->om, "installed",
      G_CALLBACK (on_om_installed), self, 0);
  wp_core_install_object_manager (core, self->om);
}

static void
wp_mixer_api_disable (WpPlugin * plugin)
{
  WpMixerApi * self = WP_MIXER_API (plugin);

  {
    g_autoptr (WpIterator) it = wp_object_manager_new_iterator (self->om);
    g_auto (GValue) val = G_VALUE_INIT;

    for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      WpProxy *obj = g_value_get_object (&val);
      on_object_removed (self->om, obj, self);
    }
  }

  g_clear_object (&self->om);
  g_clear_pointer (&self->node_infos, g_hash_table_unref);
}

static inline gdouble
volume_from_linear (float vol, gint scale)
{
  if (vol <= 0.0f)
    return 0.0;
  else if (scale == SCALE_CUBIC)
    return cbrt(vol);
  else
    return vol;
}

static inline float
volume_to_linear (gdouble vol, gint scale)
{
  if (vol <= 0.0f)
    return 0.0;
  else if (scale == SCALE_CUBIC)
    return vol * vol * vol;
  else
    return vol;
}

static gboolean
wp_mixer_api_set_volume (WpMixerApi * self, guint32 id, GVariant * vvolume)
{
  struct node_info *info = self->node_infos ?
      g_hash_table_lookup (self->node_infos, GUINT_TO_POINTER (id)) : NULL;
  struct volume new_volume = {0};
  struct volume new_monVolume = {0};
  gboolean has_mute = FALSE;
  gboolean mute = FALSE;
  WpSpaIdTable t_audioChannel =
      wp_spa_id_table_from_name ("Spa:Enum:AudioChannel");

  if (!info || !vvolume)
    return FALSE;

  if (g_variant_is_of_type (vvolume, G_VARIANT_TYPE_DOUBLE)) {
    gdouble val = g_variant_get_double (vvolume);
    new_volume = info->volume;
    for (uint i = 0; i < new_volume.channels; i++)
      new_volume.values[i] = volume_to_linear (val, self->scale);
  }
  else if (g_variant_is_of_type (vvolume, G_VARIANT_TYPE_VARDICT)) {
    GVariantIter *iter;
    const gchar *idx_str;
    GVariant *v;
    gdouble val;

    has_mute = g_variant_lookup (vvolume, "mute", "b", &mute);

    if (g_variant_lookup (vvolume, "volume", "d", &val)) {
      new_volume = info->volume;
      for (uint i = 0; i < new_volume.channels; i++)
        new_volume.values[i] = volume_to_linear (val, self->scale);
    }

    if (g_variant_lookup (vvolume, "monitorVolume", "d", &val)) {
      new_monVolume = info->monitorVolume;
      for (uint i = 0; i < new_monVolume.channels; i++)
        new_monVolume.values[i] = volume_to_linear (val, self->scale);
    }

    if (g_variant_lookup (vvolume, "channelVolumes", "a{sv}", &iter)) {
      /* keep the existing volume values for unspecified channels */
      new_volume = info->volume;
      new_monVolume = info->monitorVolume;

      while (g_variant_iter_loop (iter, "{&sv}", &idx_str, &v)) {
        guint index = atoi (idx_str);
        const gchar *channel_str = NULL;
        WpSpaIdValue channel = NULL;

        if (g_variant_lookup (v, "channel", "&s", &channel_str)) {
          channel = wp_spa_id_table_find_value_from_short_name (
              t_audioChannel, channel_str);
          if (!channel)
            wp_message_object (self, "invalid channel: %s", channel_str);
        }

        if (channel) {
          for (uint i = 0; i < info->map.channels; i++)
            if (info->map.map[i] == wp_spa_id_value_number (channel)) {
              index = i;
              break;
            }
        }

        if (index >= MIN(new_volume.channels, SPA_AUDIO_MAX_CHANNELS)) {
          wp_message_object (self, "invalid channel index: %u", index);
          continue;
        }

        if (g_variant_lookup (v, "volume", "d", &val)) {
          new_volume.values[index] = volume_to_linear (val, self->scale);
        }
        if (g_variant_lookup (v, "monitorVolume", "d", &val)) {
          new_monVolume.values[index] = volume_to_linear (val, self->scale);
        }
      }
      g_variant_iter_free (iter);
    }
  } else {
    return FALSE;
  }

  /* set param */
  g_autoptr (WpSpaPod) props = NULL;
  g_autoptr (WpSpaPodBuilder) b =
      wp_spa_pod_builder_new_object ("Spa:Pod:Object:Param:Props", "Props");

  if (new_volume.channels > 0)
    wp_spa_pod_builder_add (b, "channelVolumes", "a",
        sizeof(float), SPA_TYPE_Float,
        new_volume.channels, new_volume.values, NULL);
  if (new_monVolume.channels > 0)
    wp_spa_pod_builder_add (b, "monitorVolumes", "a",
        sizeof(float), SPA_TYPE_Float,
        new_monVolume.channels, new_monVolume.values, NULL);
  if (has_mute)
    wp_spa_pod_builder_add (b, "mute", "b", mute, NULL);

  props = wp_spa_pod_builder_end (b);

  if (info->device_id != SPA_ID_INVALID) {
    WpPipewireObject *device = wp_object_manager_lookup (self->om,
        WP_TYPE_DEVICE, WP_CONSTRAINT_TYPE_G_PROPERTY,
        "bound-id", "=u", info->device_id, NULL);
    g_return_val_if_fail (device != NULL, FALSE);

    wp_pipewire_object_set_param (device, "Route", 0, wp_spa_pod_new_object (
        "Spa:Pod:Object:Param:Route", "Route",
        "index", "i", info->route_index,
        "device", "i", info->route_device,
        "props", "P", props,
        "save", "b", true,
        NULL));
  } else {
    WpPipewireObject *node = wp_object_manager_lookup (self->om,
        WP_TYPE_NODE, WP_CONSTRAINT_TYPE_G_PROPERTY,
        "bound-id", "=u", id, NULL);
    g_return_val_if_fail (node != NULL, FALSE);

    wp_pipewire_object_set_param (node, "Props", 0, g_steal_pointer (&props));
  }

  return TRUE;
}

static GVariant *
wp_mixer_api_get_volume (WpMixerApi * self, guint32 id)
{
  struct node_info *info = self->node_infos ?
      g_hash_table_lookup (self->node_infos, GUINT_TO_POINTER (id)) : NULL;
  g_auto (GVariantBuilder) b =
      G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
  g_auto (GVariantBuilder) b_vol =
      G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
  WpSpaIdTable t_audioChannel =
      wp_spa_id_table_from_name ("Spa:Enum:AudioChannel");

  if (!info)
    return NULL;

  g_variant_builder_add (&b, "{sv}", "id", g_variant_new_uint32 (id));
  g_variant_builder_add (&b, "{sv}", "mute", g_variant_new_boolean (info->mute));
  g_variant_builder_add (&b, "{sv}", "base", g_variant_new_double (info->base));
  g_variant_builder_add (&b, "{sv}", "step", g_variant_new_double (info->step));
  g_variant_builder_add (&b, "{sv}", "volume", g_variant_new_double (
          volume_from_linear ((info->volume.channels > 0) ?
              info->volume.values[0] : info->svolume, self->scale)));
  if (info->monitorVolume.channels > 0) {
    g_variant_builder_add (&b, "{sv}", "monitorVolume", g_variant_new_double (
          volume_from_linear (info->monitorVolume.values[0], self->scale)));
  }

  for (guint i = 0; i < info->volume.channels; i++) {
    gchar index_str[10];
    g_auto (GVariantBuilder) b_vol_nested =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);

    g_variant_builder_add (&b_vol_nested, "{sv}",
        "volume", g_variant_new_double (
            volume_from_linear (info->volume.values[i], self->scale)));

    if (i < info->map.channels) {
      WpSpaIdValue v =
          wp_spa_id_table_find_value (t_audioChannel, info->map.map[i]);
      if (v) {
        const gchar *channel_str = wp_spa_id_value_short_name (v);
        g_variant_builder_add (&b_vol_nested, "{sv}",
          "channel", g_variant_new_string (channel_str));
      }
    }

    if (i < info->monitorVolume.channels) {
      g_variant_builder_add (&b_vol_nested, "{sv}",
          "monitorVolume", g_variant_new_double (
              volume_from_linear (info->monitorVolume.values[i], self->scale)));
    }

    g_snprintf (index_str, 10, "%u", i);
    g_variant_builder_add (&b_vol, "{sv}", index_str,
        g_variant_builder_end (&b_vol_nested));
  }

  g_variant_builder_add (&b, "{sv}",
      "channelVolumes", g_variant_builder_end (&b_vol));
  return g_variant_builder_end (&b);
}

static void
wp_mixer_api_class_init (WpMixerApiClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->set_property = wp_mixer_api_set_property;
  object_class->get_property = wp_mixer_api_get_property;

  plugin_class->enable = wp_mixer_api_enable;
  plugin_class->disable = wp_mixer_api_disable;

  g_object_class_install_property (object_class, PROP_SCALE,
      g_param_spec_enum ("scale", "scale", "scale",
          wp_mixer_api_volume_scale_enum_get_type (),
          SCALE_LINEAR, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  signals[ACTION_SET_VOLUME] = g_signal_new_class_handler (
      "set-volume", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_mixer_api_set_volume,
      NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 2, G_TYPE_UINT, G_TYPE_VARIANT);

  signals[ACTION_GET_VOLUME] = g_signal_new_class_handler (
      "get-volume", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_mixer_api_get_volume,
      NULL, NULL, NULL,
      G_TYPE_VARIANT, 1, G_TYPE_UINT);

  signals[SIGNAL_CHANGED] = g_signal_new (
      "changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_mixer_api_get_type (),
          "name", "mixer-api",
          "core", core,
          NULL));
  return TRUE;
}
