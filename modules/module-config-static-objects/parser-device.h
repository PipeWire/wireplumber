/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PARSER_DEVICE_H__
#define __WIREPLUMBER_PARSER_DEVICE_H__

#include <wp/wp.h>

G_BEGIN_DECLS

#define WP_PARSER_DEVICE_EXTENSION "device"

struct WpParserDeviceData {
  char *filename;
  char *factory;
  WpProperties *props;
};

#define WP_TYPE_PARSER_DEVICE (wp_parser_device_get_type ())
G_DECLARE_FINAL_TYPE (WpParserDevice, wp_parser_device, WP, PARSER_DEVICE, GObject)

typedef gboolean (*WpParserDeviceForeachFunction) (
    const struct WpParserDeviceData *parser_data, gpointer data);
void wp_parser_device_foreach (WpParserDevice *self,
    WpParserDeviceForeachFunction f, gpointer data);

G_END_DECLS

#endif
