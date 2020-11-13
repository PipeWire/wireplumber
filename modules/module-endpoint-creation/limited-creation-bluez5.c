/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

#include <pipewire/pipewire.h>

#include "limited-creation-bluez5.h"

struct _WpLimitedCreationBluez5
{
  WpLimitedCreation parent;

  gboolean avail_profiles[2][2];  /* sink/source a2dp/sco */
  WpSessionItem *endpoints[2];
};

G_DEFINE_TYPE (WpLimitedCreationBluez5, wp_limited_creation_bluez5,
    WP_TYPE_LIMITED_CREATION)

static void
endpoint_export_finish_cb (WpSessionItem * ep, GAsyncResult * res,
    WpLimitedCreationBluez5 * self)
{
  g_autoptr (GError) error = NULL;

  if (!wp_session_item_export_finish (ep, res, &error)) {
    wp_warning_object (self, "failed to export endpoint: %s", error->message);
    return;
  }

  wp_endpoint_creation_notify_endpoint_created (WP_LIMITED_CREATION (self), ep);
}

static void
endpoint_activate_finish_cb (WpSessionItem * ep, GAsyncResult * res,
    WpLimitedCreationBluez5 * self)
{
  g_autoptr (WpSession) session = NULL;
  g_autoptr (GError) error = NULL;

  /* Finish */
  gboolean activate_ret = wp_session_item_activate_finish (ep, res, &error);
  if (!activate_ret) {
    wp_warning_object (self, "failed to activate endpoint: %s", error->message);
    return;
  }

  /* Only export if not already exported */
  if (!(wp_session_item_get_flags (ep) == WP_SI_FLAG_EXPORTED)) {
    g_autoptr (WpSession) session = wp_limited_creation_lookup_session (
        WP_LIMITED_CREATION (self), WP_CONSTRAINT_TYPE_PW_PROPERTY,
        "session.name", "=s", "audio", NULL);
    if (!session) {
      wp_warning_object (self, "could not find audio session for endpoint");
      return;
    }
    wp_session_item_export (ep, session,
        (GAsyncReadyCallback) endpoint_export_finish_cb, self);
  }
}

static void
enable_endpoint (WpLimitedCreationBluez5 *self, WpNode *node,
    WpDirection direction, guint32 priority)
{
  g_autoptr (WpDevice) device = wp_limited_creation_get_device (
      WP_LIMITED_CREATION (self));
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (device));

  wp_info_object (self, "enabling endpoint %d", direction);

  /* Lookup node */
  node = wp_limited_creation_lookup_node (WP_LIMITED_CREATION (self),
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      PW_KEY_MEDIA_CLASS, "=s",
      direction == WP_DIRECTION_INPUT ? "Audio/Sink" : "Audio/Source",
      NULL);

  /* Create endpoint */
  if (!self->endpoints[direction])
    self->endpoints[direction] = wp_session_item_make (core, "si-bluez5-endpoint");
  g_return_if_fail (self->endpoints[direction]);

  /* Configure endpoint */
  {
    g_auto (GVariantBuilder) b =
        G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);

    g_variant_builder_add (&b, "{sv}", "device",
        g_variant_new_uint64 ((guint64) device));
    g_variant_builder_add (&b, "{sv}", "name",
        g_variant_new_printf ("Bluez5.%s.%s",
            wp_pipewire_object_get_property (WP_PIPEWIRE_OBJECT (device),
                PW_KEY_DEVICE_NAME),
            direction == WP_DIRECTION_INPUT ? "Sink" : "Source"));
    g_variant_builder_add (&b, "{sv}", "direction",
        g_variant_new_uint32 (direction));
    g_variant_builder_add (&b, "{sv}", "a2dp-stream",
        g_variant_new_boolean (self->avail_profiles[direction][0]));
    g_variant_builder_add (&b, "{sv}", "sco-stream",
        g_variant_new_boolean (self->avail_profiles[direction][1]));
    g_variant_builder_add (&b, "{sv}", "node",
        g_variant_new_uint64 ((guint64) node));
    g_variant_builder_add (&b, "{sv}", "priority",
        g_variant_new_uint32 (priority));

    g_return_if_fail (wp_session_item_configure (self->endpoints[direction],
        g_variant_builder_end (&b)));
  }

  /* Activate endpoint */
  wp_session_item_activate (self->endpoints[direction],
      (GAsyncReadyCallback) endpoint_activate_finish_cb, self);
}

static void
disable_endpoint (WpLimitedCreationBluez5 *self, WpDirection direction)
{
  wp_info_object (self, "disabling endpoint %d", direction);

  if (self->endpoints[direction]) {
    wp_session_item_deactivate (self->endpoints[direction]);
    wp_session_item_reset (self->endpoints[direction]);
 }
}

static WpNode *
lookup_node (WpLimitedCreationBluez5 *self, WpDirection direction)
{
  return wp_limited_creation_lookup_node (WP_LIMITED_CREATION (self),
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      PW_KEY_MEDIA_CLASS, "=s",
      direction == WP_DIRECTION_INPUT ? "Audio/Sink" : "Audio/Source",
      NULL);
}

static void
setup_endpoint (WpLimitedCreationBluez5 *self, WpNode *node,
    WpDirection direction, guint32 priority)
{
  /* Enable endpoit if at least 1 profile is available, otherwise disable */
  if (self->avail_profiles[direction][0] || self->avail_profiles[direction][1])
    enable_endpoint (self, node, direction, priority);
  else
    disable_endpoint (self, direction);
}

void
wp_limited_creation_bluez5_nodes_changed (WpLimitedCreation * ctx)
{
  WpLimitedCreationBluez5 *self = WP_LIMITED_CREATION_BLUEZ5 (ctx);
  g_autoptr (WpNode) sink = NULL;
  g_autoptr (WpNode) source = NULL;

  /* Lookup nodes */
  sink = lookup_node (self, WP_DIRECTION_INPUT);
  source = lookup_node (self, WP_DIRECTION_OUTPUT);

  /* The nodes-changed event is also triggered when the nodes are removed, so
   * the event is actually triggered twice when switching profiles. When both
   * nodes are removed, we always make sure both endpoints are disabled and
   * just return. The endpoints will be enabled in the next event */
  if (!sink && !source) {
    disable_endpoint (self, WP_DIRECTION_INPUT);
    disable_endpoint (self, WP_DIRECTION_OUTPUT);
    return;
  }

  /* Setup endpoints (at list one node must exist) */
  g_return_if_fail (sink || source);
  setup_endpoint (self, sink, WP_DIRECTION_INPUT, 20);
  setup_endpoint (self, source, WP_DIRECTION_OUTPUT, 20);
}

static void
on_device_enum_profile_done (WpPipewireObject *proxy, GAsyncResult *res,
    WpLimitedCreationBluez5 *self)
{
  g_autoptr (WpIterator) profiles = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  g_autoptr (GError) error = NULL;
  guint32 n_profiles = 0;

  /* Finish */
  profiles = wp_pipewire_object_enum_params_finish (proxy, res, &error);
  if (error) {
    wp_warning_object (self, "failed to enum profiles in bluetooth device");
    return;
  }

  /* Iterate all profiles */
  for (; wp_iterator_next (profiles, &item); g_value_unset (&item)) {
    WpSpaPod *pod = g_value_get_boxed (&item);
    g_autoptr (WpSpaPodParser) pp = NULL;
    gint index = 0;
    const gchar *name = NULL;
    const gchar *desc = NULL;
    g_autoptr (WpSpaPod) classes = NULL;

    g_return_if_fail (pod);
    g_return_if_fail (wp_spa_pod_is_object (pod));

    /* Parse profile */
    {
      g_autoptr (WpSpaPodParser) pp = wp_spa_pod_parser_new_object (pod,
          "Profile", NULL);
      g_return_if_fail (pp);
      g_return_if_fail (wp_spa_pod_parser_get (pp, "index", "i", &index, NULL));
      if (index == 0) {
        /* Skip profile 0 (Off) */
        wp_spa_pod_parser_end (pp);
        continue;
      }
      g_return_if_fail (wp_spa_pod_parser_get (pp, "name", "s", &name, NULL));
      g_return_if_fail (wp_spa_pod_parser_get (pp, "description", "s", &desc, NULL));
      g_return_if_fail (wp_spa_pod_parser_get (pp, "classes", "P", &classes, NULL));
      wp_spa_pod_parser_end (pp);
    }

    /* Parse profile classes */
    {
      g_autoptr (WpIterator) it = wp_spa_pod_iterate (classes);
      g_auto (GValue) v = G_VALUE_INIT;
      g_return_if_fail (it);
      g_return_if_fail (index > 0);
      for (; wp_iterator_next (it, &v); g_value_unset (&v)) {
        WpSpaPod *entry = g_value_get_boxed (&v);
        g_autoptr (WpSpaPodParser) pp = NULL;
        const gchar *media_class = NULL;
        gint n_nodes = 0;
        g_return_if_fail (entry);
        pp = wp_spa_pod_parser_new_struct (entry);
        g_return_if_fail (pp);
        g_return_if_fail (wp_spa_pod_parser_get_string (pp, &media_class));
        g_return_if_fail (wp_spa_pod_parser_get_int (pp, &n_nodes));
        wp_spa_pod_parser_end (pp);

        /* Set available profiles matrix */
        if (g_strcmp0 ("Audio/Sink", media_class) == 0 && n_nodes > 0)
          self->avail_profiles[WP_DIRECTION_INPUT][index - 1] = TRUE;
        else if (g_strcmp0 ("Audio/Source", media_class) == 0 && n_nodes > 0)
          self->avail_profiles[WP_DIRECTION_OUTPUT][index - 1] = TRUE;;
      }
    }

    n_profiles++;
  }

  if (n_profiles == 0)
    wp_warning_object (self, "bluetooth device does not support any profiles");
}

static void
wp_limited_creation_bluez5_constructed (GObject *object)
{
  WpLimitedCreationBluez5 *self = WP_LIMITED_CREATION_BLUEZ5 (object);

  g_autoptr (WpDevice) device = wp_limited_creation_get_device (
      WP_LIMITED_CREATION (self));
  g_return_if_fail (device);
  wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (device),
      "EnumProfile", NULL, NULL,
      (GAsyncReadyCallback) on_device_enum_profile_done, self);

  G_OBJECT_CLASS (wp_limited_creation_bluez5_parent_class)->constructed (object);
}

static void
wp_limited_creation_bluez5_finalize (GObject * object)
{
  WpLimitedCreationBluez5 *self = WP_LIMITED_CREATION_BLUEZ5 (object);

  g_clear_object (&self->endpoints[WP_DIRECTION_INPUT]);
  g_clear_object (&self->endpoints[WP_DIRECTION_OUTPUT]);

  G_OBJECT_CLASS (wp_limited_creation_bluez5_parent_class)->finalize (object);
}

static void
wp_limited_creation_bluez5_init (WpLimitedCreationBluez5 *self)
{
}

static void
wp_limited_creation_bluez5_class_init (WpLimitedCreationBluez5Class *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpLimitedCreationClass *parent_class = (WpLimitedCreationClass *) klass;

  object_class->constructed = wp_limited_creation_bluez5_constructed;
  object_class->finalize = wp_limited_creation_bluez5_finalize;

  parent_class->nodes_changed = wp_limited_creation_bluez5_nodes_changed;
}

WpLimitedCreation *
wp_limited_creation_bluez5_new (WpDevice *device)
{
  return g_object_new (wp_limited_creation_bluez5_get_type (),
      "device", device,
      NULL);
}
