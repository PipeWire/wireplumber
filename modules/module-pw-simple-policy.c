/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * module-pw-simple-policy connects the first audio output client endpoint with
 * the first audio sink remote endpoint
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

typedef void (*WpDoneCallback)(gpointer);

struct impl {
  WpCore *wp_core;

  /* Remote */
  struct pw_remote *remote;
  struct spa_hook remote_listener;

  /* Core */
  struct pw_core_proxy *core_proxy;
  struct spa_hook core_listener;
  int core_seq;
  WpDoneCallback done_cb;
  gpointer done_cb_data;

  /* Endpoints */
  WpEndpoint *ep_client;
  WpEndpoint *ep_remote;
};

static void sync_core_with_callabck(struct impl* impl, WpDoneCallback callback,
    gpointer data) {
  /* Set the callback and data */
  impl->done_cb = callback;
  impl->done_cb_data = data;

  /* Sync the core */
  impl->core_seq = pw_core_proxy_sync(impl->core_proxy, 0, impl->core_seq);
}

static void endpoint_first_foreach(WpEndpoint *ep, WpEndpoint **first)
{
  /* Just return if first is already set */
  if (*first)
    return;

  /* Set first to the current endpoint */
  *first = g_object_ref(ep);
}

static WpEndpoint *endpoint_get_first(WpCore *core,
    const char *media_class)
{
  WpEndpoint *first = NULL;
  GPtrArray *ptr_array = NULL;

  /* Get all the endpoints with the specific media lcass*/
  ptr_array = wp_endpoint_find (core, media_class);
  if (!ptr_array)
    return NULL;

  /* Get the first endpoint of the list */
  g_ptr_array_foreach(ptr_array, (GFunc)endpoint_first_foreach, &first);

  /* Return the first endpoint */
  return first;
}

static void link_endpoints(gpointer data) {
  struct impl *impl = data;
  WpEndpointLink *ep_link = NULL;

  /* Make sure the endpoints are valid */
  if (!impl->ep_client || !impl->ep_remote) {
    g_warning ("Endpoints not valid to link. Skipping...\n");
    return;
  }

  /* Link the client with the remote */
  ep_link = wp_endpoint_link_new(impl->wp_core, impl->ep_client, 0,
      impl->ep_remote, 0, NULL);
  if (!ep_link) {
    g_warning ("Could not link endpoints. Skipping...\n");
    return;
  }

  g_info ("Endpoints linked successfully\n");
}

static void
endpoint_added (WpCore *core, GQuark key, WpEndpoint *ep, struct impl * impl)
{
  const char *media_class = NULL;

  /* Reset endpoints */
  impl->ep_remote = NULL;
  impl->ep_client = NULL;

  /* Make sure an endpoint has been added */
  g_return_if_fail (key == WP_GLOBAL_ENDPOINT);

  /* Get the media class */
  media_class = wp_endpoint_get_media_class(ep);

  /* Only process client endpoints */
  if (!g_str_has_prefix(media_class, "Stream"))
    return;

  /* TODO: For now we only accept audio output clients */
  if (!g_str_has_prefix(media_class, "Stream/Output/Audio"))
    return;
  impl->ep_client = ep;

  /* Get the first endpoint with media class Audio/Sink */
  impl->ep_remote = endpoint_get_first(core, "Audio/Sink");
  if (!impl->ep_remote) {
    g_warning ("Could not get an Audio/Sink remote endpoint\n");
    return;
  }

  /* Do the linking when core is done */
  sync_core_with_callabck (impl, link_endpoints, impl);
}

static void core_done(void *data, uint32_t id, int seq)
{
  struct impl * impl = data;

  /* Call the done callback if it exists */
  if (impl->done_cb)
    impl->done_cb(impl->done_cb_data);

  impl->done_cb = NULL;
  impl->done_cb_data = NULL;
}

static const struct pw_core_proxy_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .done = core_done
};

static void on_state_changed(void *_data, enum pw_remote_state old,
    enum pw_remote_state state, const char *error)
{
  struct impl *impl = _data;

  switch (state) {
  case PW_REMOTE_STATE_ERROR:
          break;

  case PW_REMOTE_STATE_CONNECTED:
          /* Register the core event callbacks */
          impl->core_proxy = pw_remote_get_core_proxy(impl->remote);
          pw_core_proxy_add_listener(impl->core_proxy, &impl->core_listener,
              &core_events, impl);
          break;

  case PW_REMOTE_STATE_UNCONNECTED:
          break;

  default:
          break;
  }
}

static const struct pw_remote_events remote_events = {
  PW_VERSION_REMOTE_EVENTS,
  .state_changed = on_state_changed,
};

static void
module_destroy (gpointer data)
{
  struct impl *impl = data;
  g_slice_free (struct impl, impl);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  struct impl *impl = g_new0(struct impl, 1);

  /* Set destroy callback for impl */
  wp_module_set_destroy_callback (module, module_destroy, impl);

  /* Set the core */
  impl->wp_core = core;

  /* Set the core remote */
  impl->remote = wp_core_get_global(core, WP_GLOBAL_PW_REMOTE);

  /* Add a state changed listener */
  pw_remote_add_listener(impl->remote, &impl->remote_listener, &remote_events,
      impl);

  /* Register the endpoint added and removed callbacks */
  g_signal_connect (core, "global-added::endpoint",
    (GCallback) endpoint_added, impl);
}
