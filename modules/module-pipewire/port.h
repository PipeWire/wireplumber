/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

struct _WpPort
{
  struct spa_list l;

  /* Port proxy and listener */
  struct pw_proxy *proxy;
  struct spa_hook listener;

  /* Port info */
  uint32_t id;
  uint32_t parent_id;
  enum pw_direction direction;
  uint32_t media_type;
  uint32_t media_subtype;
  struct pw_port_info *info;
  struct spa_audio_info_raw format;
};
typedef struct _WpPort WpPort;