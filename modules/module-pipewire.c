/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * module-pipewire provides basic integration between wireplumber and pipewire.
 * It provides the pipewire core and remote, connects to pipewire and provides
 * the most primitive implementations of WpBaseEndpoint and WpBaseEndpointLink
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

void simple_endpoint_link_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data);
void
audio_softdsp_endpoint_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data);
void
wp_video_endpoint_factory (WpFactory * factory, GType type,
    GVariant * properties, GAsyncReadyCallback ready, gpointer user_data);

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  /* Register simple-endpoint-link and audio-softdsp-endpoint */
  wp_factory_new (core, "pipewire-simple-endpoint-link",
      simple_endpoint_link_factory);
  wp_factory_new (core, "pw-audio-softdsp-endpoint",
      audio_softdsp_endpoint_factory);
  wp_factory_new (core, "video-endpoint", wp_video_endpoint_factory);
}
