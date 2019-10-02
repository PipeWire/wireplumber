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

#include "multiport-link-algorithm.h"

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
      if (out_channel == in_channel ||
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
