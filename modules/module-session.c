/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <pipewire/extensions/session-manager.h>

struct module_data
{
  WpExportedSession *session;
  WpObjectManager *om;
};

static void
select_new_default_ep (struct module_data * data, WpDefaultEndpointType type,
    const gchar * media_class, guint32 blacklist_id)
{
  g_autoptr (GPtrArray) arr = wp_object_manager_get_objects (data->om, 0);
  guint32 max_priority = 0;
  guint32 best_id = 0, def_id;

  for (guint i = 0; i < arr->len; i++) {
    WpEndpoint *ep = g_ptr_array_index (arr, i);
    guint32 id = wp_exported_endpoint_get_global_id (WP_EXPORTED_ENDPOINT (ep));
    guint32 priority = 0;
    const gchar *priority_str;

    /* skip blacklisted */
    if (id == blacklist_id)
      continue;

    /* skip if the endpoint is of another type */
    if (g_strcmp0 (media_class, wp_endpoint_get_media_class (ep)) != 0)
      continue;

    g_autoptr (WpProperties) properties = wp_endpoint_get_properties (ep);

    priority_str = wp_properties_get (properties, "endpoint.priority");
    if (priority_str)
      priority = atoi (priority_str);

    if (priority >= max_priority)
      best_id = id;
  }

  def_id = wp_session_get_default_endpoint (WP_SESSION (data->session), type);
  if (def_id != best_id)
    wp_session_set_default_endpoint (WP_SESSION (data->session), type, best_id);
}

static void
on_endpoint_added (WpObjectManager * om, WpEndpoint * ep,
    struct module_data * data)
{
  WpDefaultEndpointType type;
  const gchar *media_class;

  media_class = wp_endpoint_get_media_class (ep);
  if (g_strcmp0 (media_class, "Audio/Source") == 0)
    type = WP_DEFAULT_ENDPOINT_TYPE_AUDIO_SOURCE;
  else if (g_strcmp0 (media_class, "Audio/Sink") == 0)
    type = WP_DEFAULT_ENDPOINT_TYPE_AUDIO_SINK;
  else if (g_strcmp0 (media_class, "Video/Source") == 0)
    type = WP_DEFAULT_ENDPOINT_TYPE_VIDEO_SOURCE;
  else
    return;

  select_new_default_ep (data, type, media_class, 0);
}

static void
on_endpoint_removed (WpObjectManager * om, WpEndpoint * ep,
    struct module_data * data)
{
  guint32 ep_id, def_id;
  WpDefaultEndpointType type;
  const gchar *media_class;

  media_class = wp_endpoint_get_media_class (ep);
  if (g_strcmp0 (media_class, "Audio/Source") == 0)
    type = WP_DEFAULT_ENDPOINT_TYPE_AUDIO_SOURCE;
  else if (g_strcmp0 (media_class, "Audio/Sink") == 0)
    type = WP_DEFAULT_ENDPOINT_TYPE_AUDIO_SINK;
  else if (g_strcmp0 (media_class, "Video/Source") == 0)
    type = WP_DEFAULT_ENDPOINT_TYPE_VIDEO_SOURCE;
  else
    return;

  ep_id = wp_exported_endpoint_get_global_id (WP_EXPORTED_ENDPOINT (ep));
  def_id = wp_session_get_default_endpoint (WP_SESSION (data->session), type);

  if (ep_id == def_id)
    select_new_default_ep (data, type, media_class, ep_id);
}

static void
module_destroy (gpointer d)
{
  struct module_data *data = d;

  g_clear_object (&data->om);

  wp_exported_unexport (WP_EXPORTED (data->session));
  g_clear_object (&data->session);

  g_slice_free (struct module_data, data);
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  struct module_data *data = g_slice_new0 (struct module_data);
  wp_module_set_destroy_callback (module, module_destroy, data);

  data->session = wp_exported_session_new (core);
  wp_exported_session_set_property (data->session,
      PW_KEY_SESSION_ID, "wireplumber");
  wp_exported_export (WP_EXPORTED (data->session), NULL, NULL, NULL);

  data->om = wp_object_manager_new ();
  g_signal_connect (data->om, "object-added",
      (GCallback) on_endpoint_added, data);
  g_signal_connect (data->om, "object-removed",
      (GCallback) on_endpoint_removed, data);
  wp_object_manager_add_object_interest (data->om,
      WP_TYPE_EXPORTED_ENDPOINT, NULL);
  wp_core_install_object_manager (core, data->om);
}
