/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

struct _WpSimplePolicy
{
  WpPolicy parent;
};

G_DECLARE_FINAL_TYPE (WpSimplePolicy, simple_policy, WP, SIMPLE_POLICY, WpPolicy)
G_DEFINE_TYPE (WpSimplePolicy, simple_policy, WP_TYPE_POLICY)

static void
simple_policy_init (WpSimplePolicy *self)
{
}

static gboolean
simple_policy_handle_endpoint (WpPolicy *policy, WpEndpoint *ep)
{
  const char *media_class = NULL;
  GVariantDict d;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpEndpoint) target = NULL;
  g_autoptr (GError) error = NULL;
  guint32 stream_id;

  /* TODO: For now we only accept audio output clients */
  media_class = wp_endpoint_get_media_class(ep);
  if (!g_str_equal (media_class, "Stream/Output/Audio"))
    return FALSE;

  /* Locate the target endpoint */
  g_variant_dict_init (&d, NULL);
  g_variant_dict_insert (&d, "action", "s", "link");
  g_variant_dict_insert (&d, "media.class", "s", "Audio/Sink");
  /* TODO: more properties are needed here */

  core = wp_policy_get_core (policy);
  target = wp_policy_find_endpoint (core, g_variant_dict_end (&d), &stream_id);
  if (!target) {
    g_warning ("Could not find an Audio/Sink target endpoint\n");
    /* TODO: we should kill the client, otherwise it's going to hang waiting */
    return FALSE;
  }

  /* Link the client with the target */
  if (!wp_endpoint_link_new (core, ep, 0, target, stream_id, &error)) {
    g_warning ("Could not link endpoints: %s\n", error->message);
  } else {
    g_info ("Sucessfully linked '%s' to '%s'\n", wp_endpoint_get_name (ep),
        wp_endpoint_get_name (target));
  }

  return TRUE;
}

static WpEndpoint *
simple_policy_find_endpoint (WpPolicy *policy, GVariant *props,
    guint32 *stream_id)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (GPtrArray) ptr_array = NULL;
  const char *media_class = NULL;
  WpEndpoint *ep;
  int i;

  core = wp_policy_get_core (policy);

  /* Get all the endpoints with the specific media class*/
  g_variant_lookup (props, "media.class", "&s", &media_class);
  ptr_array = wp_endpoint_find (core, media_class);
  if (!ptr_array)
    return NULL;

  /* TODO: for now we statically return the first stream
   * we should be looking into the media.role eventually */
  *stream_id = 0;

  /* Find and return the "selected" endpoint */
  /* FIXME: fix the endpoint API, this is terrible */
  for (i = 0; i < ptr_array->len; i++) {
    ep = g_ptr_array_index (ptr_array, i);
    GVariantIter iter;
    g_autoptr (GVariant) controls = NULL;
    g_autoptr (GVariant) value = NULL;
    const gchar *name;
    guint id;

    controls = wp_endpoint_list_controls (ep);
    g_variant_iter_init (&iter, controls);
    while ((value = g_variant_iter_next_value (&iter))) {
      if (!g_variant_lookup (value, "name", "&s", &name)
          || !g_str_equal (name, "selected")) {
        g_variant_unref (value);
        continue;
      }
      g_variant_lookup (value, "id", "u", &id);
      g_variant_unref (value);
    }

    value = wp_endpoint_get_control_value (ep, id);
    if (value && g_variant_get_boolean (value))
      return g_object_ref (ep);
  }

  /* If not found, return the first endpoint */
  ep = (ptr_array->len > 1) ? g_ptr_array_index (ptr_array, 0) : NULL;
  return g_object_ref (ep);
}

static void
simple_policy_class_init (WpSimplePolicyClass *klass)
{
  WpPolicyClass *policy_class = (WpPolicyClass *) klass;

  policy_class->handle_endpoint = simple_policy_handle_endpoint;
  policy_class->find_endpoint = simple_policy_find_endpoint;
}

void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  WpPolicy *p = g_object_new (simple_policy_get_type (),
      "rank", WP_POLICY_RANK_UPSTREAM,
      NULL);
  wp_policy_register (p, core);
}
