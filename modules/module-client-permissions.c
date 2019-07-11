/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

struct client_data
{
  union {
    struct pw_proxy *proxy;
    struct pw_client_proxy *client_proxy;
  };
  struct spa_hook proxy_listener;
  struct spa_hook client_listener;
  gboolean done;
};

static gboolean
do_free_client_data (gpointer data)
{
  g_rc_box_release (data);
  return G_SOURCE_REMOVE;
}

static void
proxy_destroy (void *data)
{
  struct client_data *d = data;
  d->proxy = NULL;
  /* destroy later because we can't free the memory of the proxy_listener
   * while we are running in one of its callbacks */
  g_idle_add (do_free_client_data, data);
}

static gboolean
do_destroy_proxy (gpointer data)
{
  struct client_data *d = data;
  if (d->proxy) {
    g_debug ("Destroying client proxy %p", d->proxy);
    pw_proxy_destroy (d->proxy);
  }
  return G_SOURCE_REMOVE;
}

static void
proxy_done (void *data, int seq)
{
  struct client_data *d = data;

  /* the proxy is not useful to keep around once we have changed permissions.
   * take an extra ref on the client data because the proxy may
   * disappear on its own if the client disconnects in the meantime */
  if (d->done)
    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, do_destroy_proxy,
        g_rc_box_acquire (d), g_rc_box_release);
}

static const struct pw_proxy_events proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_destroy,
  .done = proxy_done,
};

static void
client_info (void *object, const struct pw_client_info *info)
{
  struct client_data *d = object;
  const char *access;

  if (!(info->change_mask & PW_CLIENT_CHANGE_MASK_PROPS))
    return;

  g_return_if_fail (info->props);
  access = spa_dict_lookup (info->props, "pipewire.access");

  /* grant full permissions to restricted or security confined apps
     TODO: we should eventually build a system where we can use the role
     and the client's security label to grant access only to specific nodes
     and endpoints in the graph */
  if (!g_strcmp0 (access, "flatpak") || !g_strcmp0 (access, "restricted")) {
    const struct pw_permission perm = PW_PERMISSION_INIT(-1, PW_PERM_RWX);

    g_debug ("Granting full access to client %d (%p)", info->id, d->proxy);
    pw_client_proxy_update_permissions (d->client_proxy, 1, &perm);
  }

  d->done = TRUE;
  pw_proxy_sync (d->proxy, 123456);
}

static const struct pw_client_proxy_events client_events = {
  PW_VERSION_CLIENT_PROXY_EVENTS,
  .info = client_info,
};

static void
client_added (WpRemotePipewire * remote, guint32 id, guint32 parent_id,
    const struct spa_dict *properties, gpointer data)
{
  struct client_data *d;

  d = g_rc_box_new0 (struct client_data);

  d->proxy = wp_remote_pipewire_proxy_bind (remote, id,
      PW_TYPE_INTERFACE_Client);
  pw_proxy_add_listener (d->proxy, &d->proxy_listener, &proxy_events, d);
  pw_client_proxy_add_listener (d->client_proxy, &d->client_listener,
      &client_events, d);

  g_debug ("Bound to client %d (%p)", id, d->proxy);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  WpRemote *remote = wp_core_get_global (core, WP_GLOBAL_REMOTE_PIPEWIRE);
  g_return_if_fail (remote != NULL);

  g_signal_connect(remote, "global-added::client", (GCallback) client_added,
      NULL);
}
