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
select_format (uint32_t *vals, uint32_t n_vals, uint32_t choice)
{
  enum spa_audio_format fmt_order[] = {
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
  uint32_t i, j, best = SPA_N_ELEMENTS(fmt_order);

  switch (choice) {
  case SPA_CHOICE_None:
    g_return_val_if_fail (n_vals != 0, SPA_AUDIO_FORMAT_UNKNOWN);
    return vals[0];

  case SPA_CHOICE_Enum:
    for (i = 0; i < n_vals; ++i, ++vals) {
      for (j = 0; j < SPA_N_ELEMENTS(fmt_order); j++) {
        if (*vals == fmt_order[j] && best > j) {
          best = j;
          break;
        }
      }
    }
    return (best < SPA_N_ELEMENTS(fmt_order)) ?
        fmt_order[best] : SPA_AUDIO_FORMAT_UNKNOWN;

  default:
    g_return_val_if_reached (SPA_AUDIO_FORMAT_UNKNOWN);
  }
}

static int32_t
select_rate (int32_t *vals, uint32_t n_vals, uint32_t choice)
{
  uint32_t i;
  int32_t result = 0, min, max;

  switch (choice) {
  case SPA_CHOICE_None:
    g_return_val_if_fail (n_vals != 0, 0);
    return vals[0];

  case SPA_CHOICE_Enum:
    /* pick the one closest to 48Khz */
    g_return_val_if_fail (n_vals != 0, 0);
    result = vals[0];
    for (i = 1, ++vals; i < n_vals; ++i, ++vals) {
      if (abs(*vals - 48000) < abs(result - 48000))
        result = *vals;
    }
    return result;

  case SPA_CHOICE_Range:
    /* a range is typically 3 items: default, min, max;
       however, sometimes ALSA drivers give bad min & max values
       and pipewire picks a bad default... try to fix that here;
       the default should be the one closest to 48K */
    g_return_val_if_fail (n_vals == 3, 0);
    min = SPA_MIN (vals[1], vals[2]);
    max = SPA_MAX (vals[1], vals[2]);
    return SPA_CLAMP(48000, min, max);

  default:
    g_return_val_if_reached (0);
  }
}

static uint32_t
select_channels (uint32_t *vals, uint32_t n_vals, uint32_t choice)
{
  uint32_t i;
  uint32_t result = 0;

  switch (choice) {
  case SPA_CHOICE_None:
    g_return_val_if_fail (n_vals != 0, 0);
    return vals[0];

  case SPA_CHOICE_Enum:
    /* choose the most channels */
    g_return_val_if_fail (n_vals != 0, 0);
    result = vals[0];
    for (i = 1, ++vals; i < n_vals; ++i, ++vals) {
      if (*vals > result)
        result = *vals;
    }
    return result;

  case SPA_CHOICE_Range:
    /* a range is typically 3 items: default, min, max;
       we want the most channels, but let's not trust max
       to really be the max... ALSA drivers can be broken */
    g_return_val_if_fail (n_vals == 3, 0);
    return SPA_MAX (vals[1], vals[2]);

  default:
    g_return_val_if_reached (0);
  }
}

gboolean
choose_sensible_raw_audio_format (GPtrArray *formats,
    struct spa_audio_info_raw *result)
{
  guint i, most_channels = 0;
  struct spa_audio_info_raw *raw;

  raw = g_alloca (sizeof (struct spa_audio_info_raw) * formats->len);

  for (i = 0; i < formats->len; i++) {
    struct spa_pod *pod;
    struct spa_pod_prop *prop;
    uint32_t mtype, mstype;

    /* initialize all fields to zero (SPA_AUDIO_FORMAT_UNKNOWN etc) and set
       the unpositioned flag, which means there is no channel position array */
    spa_memzero (&raw[i], sizeof(struct spa_audio_info_raw));
    SPA_FLAG_SET(raw[i].flags, SPA_AUDIO_FLAG_UNPOSITIONED);

    pod = g_ptr_array_index (formats, i);

    if (!spa_pod_is_object (pod)) {
      g_warning ("non-object POD appeared on formats list; this node is buggy");
      continue;
    }

    if (spa_format_parse (pod, &mtype, &mstype) < 0) {
      g_warning ("format does not have media type / subtype");
      continue;
    }

    if (!(mtype == SPA_MEDIA_TYPE_audio && mstype == SPA_MEDIA_SUBTYPE_raw))
      continue;

    /* go through the fields and populate raw[i] */
    SPA_POD_OBJECT_FOREACH ((struct spa_pod_object *) pod, prop) {
      uint32_t type, size, n_vals, choice;
      const struct spa_pod *val;
      void *vals;

      if (prop->key == SPA_FORMAT_mediaType ||
          prop->key == SPA_FORMAT_mediaSubtype)
        continue;

      val = spa_pod_get_values (&prop->value, &n_vals, &choice);
      type = val->type;
      size = val->size;
      vals = SPA_POD_BODY(val);

#define test_invariant(x) \
  G_STMT_START { \
    if (G_LIKELY (x)) ; \
    else { \
      g_warn_message (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, #x); \
      raw[i].format = SPA_AUDIO_FORMAT_UNKNOWN; \
      goto next; \
    } \
  } G_STMT_END

      switch (prop->key) {
        case SPA_FORMAT_AUDIO_format:
          test_invariant (type == SPA_TYPE_Id);
          test_invariant (size == sizeof(uint32_t));
          raw[i].format = select_format ((uint32_t *) vals, n_vals, choice);
          break;
        case SPA_FORMAT_AUDIO_rate:
          test_invariant (type == SPA_TYPE_Int);
          test_invariant (size == sizeof(int32_t));
          raw[i].rate = select_rate ((int32_t *) vals, n_vals, choice);
          break;
        case SPA_FORMAT_AUDIO_channels:
          test_invariant (type == SPA_TYPE_Int);
          test_invariant (size == sizeof(uint32_t));
          raw[i].channels = select_channels ((uint32_t *) vals, n_vals, choice);
          break;
        case SPA_FORMAT_AUDIO_position:
          /* just copy the array, there is no choice here */
          SPA_FLAG_CLEAR (raw[i].flags, SPA_AUDIO_FLAG_UNPOSITIONED);
          spa_pod_copy_array (val, SPA_TYPE_Id, raw[i].position,
              SPA_AUDIO_MAX_CHANNELS);
          break;
        default:
          if (prop->value.type == SPA_TYPE_Choice)
            SPA_POD_CHOICE_TYPE(&prop->value) = SPA_CHOICE_None;
          break;
      }
    }

next:
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
