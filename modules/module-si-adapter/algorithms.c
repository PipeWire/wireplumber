/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

#include <spa/param/format-utils.h>
#include <spa/param/audio/format.h>

#include "algorithms.h"

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

  guint32 choice_type =
      wp_spa_id_value_number (wp_spa_pod_get_choice_type (value));

  /* None */
  if (choice_type == SPA_CHOICE_None) {
    g_autoptr (WpSpaPod) child = wp_spa_pod_get_choice_child (value);
    wp_spa_pod_get_id (child, &ret);
  }

  /* Enum */
  else if (choice_type == SPA_CHOICE_Enum) {
    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (value);
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

  guint32 choice_type =
      wp_spa_id_value_number (wp_spa_pod_get_choice_type (value));

  /* None */
  if (choice_type == SPA_CHOICE_None) {
    g_autoptr (WpSpaPod) child = wp_spa_pod_get_choice_child (value);
    wp_spa_pod_get_int (child, &ret);
  }

  /* Enum */
  else if (choice_type == SPA_CHOICE_Enum) {
    /* pick the one closest to 48Khz */
    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (value);
    GValue next = G_VALUE_INIT;
    while (wp_iterator_next (it, &next)) {
      gint *rate = (gint *) g_value_get_pointer (&next);
      if (abs (*rate - 48000) < abs (ret - 48000))
        ret = *rate;
      g_value_unset (&next);
    }
  }

  /* Range */
  else if (choice_type == SPA_CHOICE_Range) {
    /* a range is typically 3 items: default, min, max;
       however, sometimes ALSA drivers give bad min & max values
       and pipewire picks a bad default... try to fix that here;
       the default should be the one closest to 48K */
    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (value);
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
select_channels (WpSpaPod *value, gint preference)
{
  gint ret = 0;

  /* Just return the value if it is not a choice value */
  if (!wp_spa_pod_is_choice (value)) {
    wp_spa_pod_get_int (value, &ret);
    return ret;
  }

  guint32 choice_type =
      wp_spa_id_value_number (wp_spa_pod_get_choice_type (value));

  /* None */
  if (choice_type == SPA_CHOICE_None) {
    g_autoptr (WpSpaPod) child = wp_spa_pod_get_choice_child (value);
    wp_spa_pod_get_int (child, &ret);
  }

  /* Enum */
  else if (choice_type == SPA_CHOICE_Enum) {
    /* choose the most channels */
    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (value);
    GValue next = G_VALUE_INIT;
    gint diff = SPA_AUDIO_MAX_CHANNELS;
    while (wp_iterator_next (it, &next)) {
      gint *channel = (gint *) g_value_get_pointer (&next);
      if (abs (*channel - preference) < diff) {
        diff = abs (*channel - preference);
        ret = *channel;
      }
      g_value_unset (&next);
    }
  }

  /* Range */
  else if (choice_type == SPA_CHOICE_Range) {
    /* a range is typically 3 items: default, min, max;
       we want the most channels, but let's not trust max
       to really be the max... ALSA drivers can be broken */
    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (value);
    GValue next = G_VALUE_INIT;
    gint vals[3];
    gint i = 0;
    while (wp_iterator_next (it, &next) && i < 3) {
      vals[i] = *(gint *) g_value_get_pointer (&next);
      g_value_unset (&next);
      i++;
    }
    ret = SPA_MAX (vals[1], preference);
    ret = SPA_MIN (ret, vals[2]);
  }

  return ret;
}

gboolean
choose_sensible_raw_audio_format (WpIterator *formats,
    gint channels_preference, struct spa_audio_info_raw *result)
{
  guint most_channels = 0;
  struct spa_audio_info_raw raw;
  g_auto (GValue) item = G_VALUE_INIT;

  for (; wp_iterator_next (formats, &item); g_value_unset (&item)) {
    WpSpaPod *pod = g_value_get_boxed (&item);
    uint32_t mtype, mstype;

    /* initialize all fields to zero (SPA_AUDIO_FORMAT_UNKNOWN etc) and set
       the unpositioned flag, which means there is no channel position array */
    spa_memzero (&raw, sizeof(struct spa_audio_info_raw));
    SPA_FLAG_SET(raw.flags, SPA_AUDIO_FLAG_UNPOSITIONED);

    if (!wp_spa_pod_is_object (pod)) {
      g_warning ("non-object POD appeared on formats list; this node is buggy");
      continue;
    }

    if (!wp_spa_pod_get_object (pod, NULL,
        "mediaType", "I", &mtype,
        "mediaSubtype", "I", &mstype,
        NULL)) {
      g_warning ("format does not have media type / subtype");
      continue;
    }

    if (!(mtype == SPA_MEDIA_TYPE_audio && mstype == SPA_MEDIA_SUBTYPE_raw))
      continue;

    /* go through the fields and populate raw */
    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (pod);
    GValue next = G_VALUE_INIT;
    while (wp_iterator_next (it, &next)) {
      WpSpaPod *p = g_value_get_boxed (&next);
      const gchar *key = NULL;
      g_autoptr (WpSpaPod) value = NULL;
      wp_spa_pod_get_property (p, &key, &value);

      /* format */
      if (g_strcmp0 (key, "format") == 0) {
        raw.format = select_format (value);
      }

      /* rate */
      else if (g_strcmp0 (key, "rate") == 0) {
        raw.rate = select_rate (value);
      }

      /* channels */
      else if (g_strcmp0 (key, "channels") == 0) {
        raw.channels = select_channels (value, channels_preference);
      }

      /* position */
      else if (g_strcmp0 (key, "position") == 0) {
        /* just copy the array, there is no choice here */
        g_return_val_if_fail (wp_spa_pod_is_array (value), FALSE);
        SPA_FLAG_CLEAR (raw.flags, SPA_AUDIO_FLAG_UNPOSITIONED);
        g_autoptr (WpIterator) array_it = wp_spa_pod_new_iterator (value);
        GValue array_next = G_VALUE_INIT;
        guint j = 0;
        while (wp_iterator_next (array_it, &array_next)) {
          guint32 *pos_id = (guint32 *)g_value_get_pointer (&array_next);
          raw.position[j] = *pos_id;
          g_value_unset (&array_next);
          j++;
        }
      }

      g_value_unset (&next);
    }

    /* figure out if this one is the best so far */
    if (raw.format != SPA_AUDIO_FORMAT_UNKNOWN &&
        raw.channels > most_channels ) {
      most_channels = raw.channels;
      *result = raw;
    }
  }

  /* if we picked a format, most_channels must be > 0 */
  return (most_channels > 0);
}
