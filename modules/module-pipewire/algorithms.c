/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <spa/debug/types.h>
#include <spa/param/audio/type-info.h>

// for choose_sensible_raw_audio_format
#include <spa/param/format-utils.h>
#include <spa/param/audio/format.h>

#include "algorithms.h"

gboolean
multiport_link_create (
    GVariant * src_data,
    GVariant * sink_data,
    CreateLinkCb create_link_cb,
    gpointer user_data,
    GError ** error)
{
  g_autoptr (GPtrArray) in_ports = NULL;
  GVariantIter *iter;
  GVariant *child;
  guint32 out_node_id, in_node_id;
  guint32 out_port_id, in_port_id;
  guint32 out_channel, in_channel;
  guint8 direction;
  guint i;
  gboolean link_all = FALSE;

  /* tuple format:
      uint32 node_id;
      uint32 port_id;
      uint32 channel;  // enum spa_audio_channel
      uint8 direction; // enum spa_direction
   */
  if (!g_variant_is_of_type (src_data, G_VARIANT_TYPE("a(uuuy)")))
    goto invalid_argument;
  if (!g_variant_is_of_type (sink_data, G_VARIANT_TYPE("a(uuuy)")))
    goto invalid_argument;

  /* transfer the in ports to an array so that we can
     delete them when they are linked */
  in_ports = g_ptr_array_new_full (g_variant_n_children (sink_data),
      (GDestroyNotify) g_variant_unref);

  g_variant_get (sink_data, "a(uuuy)", &iter);
  while ((child = g_variant_iter_next_value (iter))) {
    g_variant_get (child, "(uuuy)", NULL, NULL, NULL, &direction);
    /* remove non-input direction ports right away */
    if (direction == PW_DIRECTION_INPUT)
      g_ptr_array_add (in_ports, child);
    else
      g_variant_unref (child);
  }
  g_variant_iter_free (iter);

  /* now loop over the out ports and figure out where they should be linked */
  g_variant_get (src_data, "a(uuuy)", &iter);

  /* special case for mono inputs: link to all outputs,
     since we don't support proper channel mapping yet */
  if (g_variant_iter_n_children (iter) == 1)
    link_all = TRUE;

  while (g_variant_iter_loop (iter, "(uuuy)", &out_node_id, &out_port_id,
              &out_channel, &direction))
  {
    /* skip non-output ports right away */
    if (direction != PW_DIRECTION_OUTPUT)
      continue;

    for (i = 0; i < in_ports->len; i++) {
      child = g_ptr_array_index (in_ports, i);
      g_variant_get (child, "(uuuy)", &in_node_id, &in_port_id, &in_channel, NULL);

      /* the channel has to match, unless we don't have any information
         on channel ordering on either side */
      if (link_all ||
          out_channel == in_channel ||
          out_channel == SPA_AUDIO_CHANNEL_UNKNOWN ||
          in_channel == SPA_AUDIO_CHANNEL_UNKNOWN)
      {
        g_autoptr (WpProperties) props = NULL;

        /* Create the properties */
        props = wp_properties_new_empty ();
        wp_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%u", out_node_id);
        wp_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", out_port_id);
        wp_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%u", in_node_id);
        wp_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", in_port_id);

        g_debug ("Create pw link: %u:%u (%s) -> %u:%u (%s)",
            out_node_id, out_port_id,
            spa_debug_type_find_name (spa_type_audio_channel, out_channel),
            in_node_id, in_port_id,
            spa_debug_type_find_name (spa_type_audio_channel, in_channel));

        /* and the link */
        create_link_cb (props, user_data);

        /* continue to link all input ports, if requested */
        if (link_all)
          continue;

        /* and remove the linked input port from the array */
        g_ptr_array_remove_index (in_ports, i);

        /* break out of the for loop; go for the next out port */
        break;
      }
    }
  }

  return TRUE;

invalid_argument:
  g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
      "Endpoint node/port descriptions don't have the required fields");
  return FALSE;
}


static enum spa_audio_format
select_format (WpSpaPod *value)
{
  enum spa_audio_format ret = SPA_AUDIO_FORMAT_UNKNOWN;

  static enum spa_audio_format fmt_order[] = {
    /* float 32 is the best because it needs
       no conversion from our internal pipeline format */
    SPA_AUDIO_FORMAT_F32,

    /* signed 16-bit is known to work very well;
       unsigned should also be fine */
    SPA_AUDIO_FORMAT_S16,
    SPA_AUDIO_FORMAT_U16,

    /* then go for the formats that are aligned to sizeof(int),
       from the best quality to the worst */
    SPA_AUDIO_FORMAT_S32,
    SPA_AUDIO_FORMAT_U32,
    SPA_AUDIO_FORMAT_S24_32,
    SPA_AUDIO_FORMAT_U24_32,

    /* then float 64, which should need little conversion from float 32 */
    SPA_AUDIO_FORMAT_F64,

    /* and then try the reverse endianess too */
    SPA_AUDIO_FORMAT_F32_OE,
    SPA_AUDIO_FORMAT_S16_OE,
    SPA_AUDIO_FORMAT_U16_OE,
    SPA_AUDIO_FORMAT_S32_OE,
    SPA_AUDIO_FORMAT_U32_OE,
    SPA_AUDIO_FORMAT_S24_32_OE,
    SPA_AUDIO_FORMAT_U24_32_OE,
    SPA_AUDIO_FORMAT_F64_OE,

    /* then go for unaligned strange formats */
    SPA_AUDIO_FORMAT_S24,
    SPA_AUDIO_FORMAT_U24,
    SPA_AUDIO_FORMAT_S20,
    SPA_AUDIO_FORMAT_U20,
    SPA_AUDIO_FORMAT_S18,
    SPA_AUDIO_FORMAT_U18,
    SPA_AUDIO_FORMAT_S24_OE,
    SPA_AUDIO_FORMAT_U24_OE,
    SPA_AUDIO_FORMAT_S20_OE,
    SPA_AUDIO_FORMAT_U20_OE,
    SPA_AUDIO_FORMAT_S18_OE,
    SPA_AUDIO_FORMAT_U18_OE,

    /* leave 8-bit last, that's bad quality */
    SPA_AUDIO_FORMAT_S8,
    SPA_AUDIO_FORMAT_U8,

    /* plannar formats are problematic currently, discourage their use */
    SPA_AUDIO_FORMAT_F32P,
    SPA_AUDIO_FORMAT_S16P,
    SPA_AUDIO_FORMAT_S32P,
    SPA_AUDIO_FORMAT_S24_32P,
    SPA_AUDIO_FORMAT_S24P,
    SPA_AUDIO_FORMAT_F64P,
    SPA_AUDIO_FORMAT_U8P,
  };
  guint32 best = SPA_N_ELEMENTS(fmt_order);

  /* Just return the value if it is not a choice value */
  if (!wp_spa_pod_is_choice (value)) {
    wp_spa_pod_get_id (value, &ret);
    return ret;
  }

  const gchar * choice_type_name = wp_spa_pod_get_choice_type_name (value);

  /* None */
  if (g_strcmp0 ("None", choice_type_name) == 0) {
    g_autoptr (WpSpaPod) child = wp_spa_pod_get_choice_child (value);
    wp_spa_pod_get_id (child, &ret);
  }

  /* Enum */
  else if (g_strcmp0 ("Enum", choice_type_name) == 0) {
    g_autoptr (WpIterator) it = wp_spa_pod_iterator_new (value);
    GValue next = G_VALUE_INIT;
    while (wp_iterator_next (it, &next)) {
      enum spa_audio_format *format_id = (enum spa_audio_format *)
          g_value_get_pointer (&next);
      for (guint j = 0; j < SPA_N_ELEMENTS(fmt_order); j++) {
        if (*format_id == fmt_order[j] && best > j) {
          best = j;
          break;
        }
      }
      g_value_unset (&next);
    }
    if (best < SPA_N_ELEMENTS(fmt_order))
      ret = fmt_order[best];
  }

  return ret;
}

static gint
select_rate (WpSpaPod *value)
{
  gint ret = 0;

  /* Just return the value if it is not a choice value */
  if (!wp_spa_pod_is_choice (value)) {
    wp_spa_pod_get_int (value, &ret);
    return ret;
  }

  const gchar * choice_type_name = wp_spa_pod_get_choice_type_name (value);

  /* None */
  if (g_strcmp0 ("None", choice_type_name) == 0) {
    g_autoptr (WpSpaPod) child = wp_spa_pod_get_choice_child (value);
    wp_spa_pod_get_int (child, &ret);
  }

  /* Enum */
  else if (g_strcmp0 ("Enum", choice_type_name) == 0) {
    /* pick the one closest to 48Khz */
    g_autoptr (WpIterator) it = wp_spa_pod_iterator_new (value);
    GValue next = G_VALUE_INIT;
    while (wp_iterator_next (it, &next)) {
      gint *rate = (gint *) g_value_get_pointer (&next);
      if (abs (*rate - 48000) < abs (ret - 48000))
        ret = *rate;
      g_value_unset (&next);
    }
  }

  /* Range */
  else if (g_strcmp0 ("Range", choice_type_name) == 0) {
    /* a range is typically 3 items: default, min, max;
       however, sometimes ALSA drivers give bad min & max values
       and pipewire picks a bad default... try to fix that here;
       the default should be the one closest to 48K */
    g_autoptr (WpIterator) it = wp_spa_pod_iterator_new (value);
    GValue next = G_VALUE_INIT;
    gint vals[3];
    gint i = 0, min, max;
    while (wp_iterator_next (it, &next) && i < 3) {
      vals[i] = *(gint *) g_value_get_pointer (&next);
      g_value_unset (&next);
      i++;
    }
    min = SPA_MIN (vals[1], vals[2]);
    max = SPA_MAX (vals[1], vals[2]);
    ret = SPA_CLAMP (48000, min, max);
  }

  return ret;
}

static gint
select_channels (WpSpaPod *value)
{
  gint ret = 0;

  /* Just return the value if it is not a choice value */
  if (!wp_spa_pod_is_choice (value)) {
    wp_spa_pod_get_int (value, &ret);
    return ret;
  }

  const gchar * choice_type_name = wp_spa_pod_get_choice_type_name (value);

  /* None */
  if (g_strcmp0 ("None", choice_type_name) == 0) {
    g_autoptr (WpSpaPod) child = wp_spa_pod_get_choice_child (value);
    wp_spa_pod_get_int (child, &ret);
  }

  /* Enum */
  else if (g_strcmp0 ("Enum", choice_type_name) == 0) {
    /* choose the most channels */
    g_autoptr (WpIterator) it = wp_spa_pod_iterator_new (value);
    GValue next = G_VALUE_INIT;
    while (wp_iterator_next (it, &next)) {
      gint *channel = (gint *) g_value_get_pointer (&next);
      if (*channel > ret)
        ret = *channel;
      g_value_unset (&next);
    }
  }

  /* Range */
  else if (g_strcmp0 ("Range", choice_type_name) == 0) {
    /* a range is typically 3 items: default, min, max;
       we want the most channels, but let's not trust max
       to really be the max... ALSA drivers can be broken */
    g_autoptr (WpIterator) it = wp_spa_pod_iterator_new (value);
    GValue next = G_VALUE_INIT;
    gint vals[3];
    gint i = 0;
    while (wp_iterator_next (it, &next) && i < 3) {
      vals[i] = *(gint *) g_value_get_pointer (&next);
      g_value_unset (&next);
      i++;
    }
    ret = SPA_MAX (vals[1], vals[2]);
  }

  return ret;
}

gboolean
choose_sensible_raw_audio_format (GPtrArray *formats,
    struct spa_audio_info_raw *result)
{
  guint i, most_channels = 0;
  struct spa_audio_info_raw *raw;

  raw = g_alloca (sizeof (struct spa_audio_info_raw) * formats->len);

  for (i = 0; i < formats->len; i++) {
    WpSpaPod *pod = g_ptr_array_index (formats, i);
    uint32_t mtype, mstype;

    /* initialize all fields to zero (SPA_AUDIO_FORMAT_UNKNOWN etc) and set
       the unpositioned flag, which means there is no channel position array */
    spa_memzero (&raw[i], sizeof(struct spa_audio_info_raw));
    SPA_FLAG_SET(raw[i].flags, SPA_AUDIO_FLAG_UNPOSITIONED);

    if (!wp_spa_pod_is_object (pod)) {
      g_warning ("non-object POD appeared on formats list; this node is buggy");
      continue;
    }

    if (!wp_spa_pod_get_object (pod,
        "Format", NULL,
        "mediaType", "I", &mtype,
        "mediaSubtype", "I", &mstype,
        NULL)) {
      g_warning ("format does not have media type / subtype");
      continue;
    }

    if (!(mtype == SPA_MEDIA_TYPE_audio && mstype == SPA_MEDIA_SUBTYPE_raw))
      continue;

    /* go through the fields and populate raw[i] */
    g_autoptr (WpIterator) it = wp_spa_pod_iterator_new (pod);
    GValue next = G_VALUE_INIT;
    while (wp_iterator_next (it, &next)) {
      WpSpaPod *p = g_value_get_boxed (&next);
      const gchar *key = NULL;
      g_autoptr (WpSpaPod) value = NULL;
      wp_spa_pod_get_property (p, &key, &value);

      /* format */
      if (g_strcmp0 (key, "format") == 0) {
        raw[i].format = select_format (value);
      }

      /* rate */
      else if (g_strcmp0 (key, "rate") == 0) {
        raw[i].rate = select_rate (value);
      }

      /* channels */
      else if (g_strcmp0 (key, "channels") == 0) {
        raw[i].channels = select_channels (value);
      }

      /* position */
      else if (g_strcmp0 (key, "position") == 0) {
        /* just copy the array, there is no choice here */
        g_return_val_if_fail (wp_spa_pod_is_array (value), FALSE);
        SPA_FLAG_CLEAR (raw[i].flags, SPA_AUDIO_FLAG_UNPOSITIONED);
        g_autoptr (WpIterator) array_it = wp_spa_pod_iterator_new (value);
        GValue array_next = G_VALUE_INIT;
        guint j = 0;
        while (wp_iterator_next (array_it, &array_next)) {
          guint32 *pos_id = (guint32 *)g_value_get_pointer (&array_next);
          raw[i].position[j] = *pos_id;
          g_value_unset (&array_next);
          j++;
        }
      }

      g_value_unset (&next);
    }

    /* figure out if this one is the best so far */
    if (raw[i].format != SPA_AUDIO_FORMAT_UNKNOWN &&
        raw[i].channels > most_channels ) {
      most_channels = raw[i].channels;
      *result = raw[i];
    }
  }

  /* if we picked a format, most_channels must be > 0 */
  return (most_channels > 0);
}
