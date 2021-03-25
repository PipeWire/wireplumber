/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <spa/debug/types.h>
#include <spa/param/audio/type-info.h>

#define SI_FACTORY_NAME "si-standard-link"

struct _WpSiStandardLink
{
  WpSessionItem parent;

  /* configuration */
  GWeakRef out_item;
  GWeakRef in_item;
  const gchar *out_item_port_context;
  const gchar *in_item_port_context;
  WpSession *session;
  gboolean manage_lifetime;
  gboolean passive;

  /* activate */
  GPtrArray *node_links;
  guint n_async_ops_wait;

  /* export */
  WpImplEndpointLink *impl_endpoint_link;
};

static void si_standard_link_link_init (WpSiLinkInterface * iface);

G_DECLARE_FINAL_TYPE (WpSiStandardLink, si_standard_link, WP, SI_STANDARD_LINK,
    WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiStandardLink, si_standard_link,
    WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_LINK, si_standard_link_link_init))

static void
si_standard_link_init (WpSiStandardLink * self)
{
  g_weak_ref_init (&self->out_item, NULL);
  g_weak_ref_init (&self->in_item, NULL);
}

static void
on_item_features_changed (WpObject * item, GParamSpec * param,
    WpSessionItem * link)
{
  guint features = wp_object_get_active_features (item);

  /* item was deactivated; destroy the associated link */
  if (!(features & WP_SESSION_ITEM_FEATURE_ACTIVE)) {
    wp_trace_object (link, "destroying because item " WP_OBJECT_FORMAT
        " was deactivated", WP_OBJECT_ARGS (item));
    wp_session_item_reset (link);
    g_object_unref (link);
  }
}

static void
on_link_features_changed (WpObject * link, GParamSpec * param, gpointer data)
{
  guint features = wp_object_get_active_features (link);

  if (!(features & WP_SESSION_ITEM_FEATURE_EXPORTED)) {
    wp_trace_object (link, "destroying because impl proxy was destroyed");
    wp_session_item_reset (WP_SESSION_ITEM (link));
    g_object_unref (link);
  }
}

static void
si_standard_link_reset (WpSessionItem * item)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);

  /* disconnect all signals */
  if (self->manage_lifetime) {
    g_autoptr (WpSessionItem) si_out = g_weak_ref_get (&self->out_item);
    g_autoptr (WpSessionItem) si_in = g_weak_ref_get (&self->in_item);
    if (si_out) {
      g_signal_handlers_disconnect_by_func (si_out,
          G_CALLBACK (on_item_features_changed), self);
    }
    if (si_in) {
      g_signal_handlers_disconnect_by_func (si_in,
          G_CALLBACK (on_item_features_changed), self);
    }
    g_signal_handlers_disconnect_by_func (self,
        G_CALLBACK (on_link_features_changed), NULL);
  }

  /* deactivate first */
  wp_object_deactivate (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED);

  /* reset */
  g_weak_ref_set (&self->out_item, NULL);
  g_weak_ref_set (&self->in_item, NULL);
  self->out_item_port_context = NULL;
  self->in_item_port_context = NULL;
  g_clear_object (&self->session);
  self->manage_lifetime = FALSE;
  self->passive = FALSE;

  WP_SESSION_ITEM_CLASS (si_standard_link_parent_class)->reset (item);
}

static WpSessionItem *
get_and_validate_item (WpProperties * props, const gchar *key)
{
  WpSessionItem *res = NULL;
  const gchar *str = NULL;

  str = wp_properties_get (props, key);
  if (!str || sscanf(str, "%p", &res) != 1 || !WP_IS_SI_PORT_INFO (res) ||
      !(wp_object_get_active_features (WP_OBJECT (res)) &
          WP_SESSION_ITEM_FEATURE_ACTIVE))
    return NULL;

  return res;
}

static gboolean
si_standard_link_configure (WpSessionItem * item, WpProperties * p)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);
  g_autoptr (WpProperties) si_props = wp_properties_ensure_unique_owner (p);
  WpSessionItem *out_item, *in_item;
  WpSession *session = NULL;
  const gchar *str;

  /* reset previous config */
  si_standard_link_reset (item);

  out_item = get_and_validate_item (si_props, "out-item");
  if (!out_item)
    return FALSE;
  wp_properties_setf (si_props, "out-item-id", "%u",
      wp_session_item_get_id (out_item));

  in_item = get_and_validate_item (si_props, "in-item");
  if (!in_item)
    return FALSE;
  wp_properties_setf (si_props, "in-item-id", "%u",
      wp_session_item_get_id (in_item));

  self->out_item_port_context = wp_properties_get (si_props,
      "out-item-port-context");

  self->out_item_port_context = wp_properties_get (si_props,
      "in-item-port-context");

  str = wp_properties_get (si_props, "manage-lifetime");
  if (str && sscanf(str, "%u", &self->manage_lifetime) != 1)
    return FALSE;
  if (!str)
    wp_properties_setf (si_props, "manage-lifetime", "%u",
        self->manage_lifetime);

  str = wp_properties_get (si_props, "passive");
  if (str && sscanf(str, "%u", &self->passive) != 1)
    return FALSE;
  if (!str)
    wp_properties_setf (si_props, "passive", "%u",
        self->passive);

  /* session is optional (only needed if we want to export) */
  str = wp_properties_get (si_props, "session");
  if (str && (sscanf(str, "%p", &session) != 1 || !WP_IS_SESSION (session)))
    return FALSE;
  if (!str)
    wp_properties_setf (si_props, "session", "%p", session);

  if (self->manage_lifetime) {
    g_signal_connect_object (out_item, "notify::active-features",
        G_CALLBACK (on_item_features_changed), self, 0);
    g_signal_connect_object (in_item, "notify::active-features",
        G_CALLBACK (on_item_features_changed), self, 0);
    g_signal_connect (self, "notify::active-features",
        G_CALLBACK (on_link_features_changed), NULL);
  }

  g_weak_ref_set(&self->out_item, out_item);
  g_weak_ref_set(&self->in_item, in_item);
  if (session)
    self->session = g_object_ref (session);

  wp_properties_set (si_props, "si-factory-name", SI_FACTORY_NAME);
  wp_session_item_set_properties (WP_SESSION_ITEM (self),
      g_steal_pointer (&si_props));
  return TRUE;
}

static gpointer
si_standard_link_get_associated_proxy (WpSessionItem * item, GType proxy_type)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);

  if (proxy_type == WP_TYPE_SESSION)
    return self->session ? g_object_ref (self->session) : NULL;
  else if (proxy_type == WP_TYPE_ENDPOINT_LINK)
    return self->impl_endpoint_link ?
        g_object_ref (self->impl_endpoint_link) : NULL;

  return NULL;
}

static void
si_standard_link_disable_active (WpSessionItem *si)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (si);
  g_autoptr (WpSessionItem) si_out = g_weak_ref_get (&self->out_item);
  g_autoptr (WpSessionItem) si_in = g_weak_ref_get (&self->in_item);
  WpSiAcquisition *out_acquisition, *in_acquisition;

  if (si_out) {
    out_acquisition = wp_si_port_info_get_acquisition (
        WP_SI_PORT_INFO (si_out));
    if (out_acquisition)
      wp_si_acquisition_release (out_acquisition, WP_SI_LINK (self),
          WP_SI_PORT_INFO (si_out));
  }
  if (si_in) {
    in_acquisition = wp_si_port_info_get_acquisition (WP_SI_PORT_INFO (si_in));
    if (in_acquisition)
      wp_si_acquisition_release (in_acquisition, WP_SI_LINK (self),
          WP_SI_PORT_INFO (si_in));
  }

  g_clear_pointer (&self->node_links, g_ptr_array_unref);
  self->n_async_ops_wait = 0;

  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
}

static void
si_standard_link_disable_exported (WpSessionItem *si)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (si);

  g_clear_object (&self->impl_endpoint_link);
  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_EXPORTED);
}

static void
on_item_acquired (WpSiAcquisition * acq, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiStandardLink *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_si_acquisition_acquire_finish (acq, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  self->n_async_ops_wait--;
  if (self->n_async_ops_wait == 0)
    wp_object_update_features (WP_OBJECT (self),
        WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
}

static void
on_link_activated (WpObject * proxy, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiStandardLink *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (proxy, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  self->n_async_ops_wait--;
  if (self->n_async_ops_wait == 0)
    wp_object_update_features (WP_OBJECT (self),
        WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
}

static gboolean
create_links (WpSiStandardLink * self, WpTransition * transition,
    GVariant * out_ports, GVariant * in_ports)
{
  g_autoptr (GPtrArray) in_ports_arr = NULL;
  g_autoptr (WpCore) core = NULL;
  GVariantIter *iter = NULL;
  GVariant *child;
  guint32 out_node_id, in_node_id;
  guint32 out_port_id, in_port_id;
  guint32 out_channel, in_channel;
  gboolean link_all = FALSE;
  guint i;
  guint32 eplink_id;

  /* tuple format:
      uint32 node_id;
      uint32 port_id;
      uint32 channel;  // enum spa_audio_channel
   */
  if (!out_ports || !g_variant_is_of_type (out_ports, G_VARIANT_TYPE("a(uuu)")))
    return FALSE;
  if (!in_ports || !g_variant_is_of_type (in_ports, G_VARIANT_TYPE("a(uuu)")))
    return FALSE;

  core = wp_object_get_core (WP_OBJECT (self));
  g_return_val_if_fail (core, FALSE);

  eplink_id = wp_session_item_get_associated_proxy_id (WP_SESSION_ITEM (self),
      WP_TYPE_ENDPOINT_LINK);

  self->n_async_ops_wait = 0;
  self->node_links = g_ptr_array_new_with_free_func (g_object_unref);

  /* transfer the in ports to an array so that we can
     delete them when they are linked */
  i = g_variant_n_children (in_ports);
  in_ports_arr = g_ptr_array_new_full (i, (GDestroyNotify) g_variant_unref);
  g_ptr_array_set_size (in_ports_arr, i);

  g_variant_get (in_ports, "a(uuu)", &iter);
  while ((child = g_variant_iter_next_value (iter))) {
    g_ptr_array_index (in_ports_arr, --i) = child;
  }
  g_variant_iter_free (iter);

  /* now loop over the out ports and figure out where they should be linked */
  g_variant_get (out_ports, "a(uuu)", &iter);

  /* special case for mono inputs: link to all outputs,
     since we don't support proper channel mapping yet */
  if (g_variant_iter_n_children (iter) == 1)
    link_all = TRUE;

  while (g_variant_iter_loop (iter, "(uuu)", &out_node_id, &out_port_id,
              &out_channel))
  {
    for (i = in_ports_arr->len; i > 0; i--) {
      child = g_ptr_array_index (in_ports_arr, i - 1);
      g_variant_get (child, "(uuu)", &in_node_id, &in_port_id, &in_channel);

      /* the channel has to match, unless we don't have any information
         on channel ordering on either side */
      if (link_all ||
          out_channel == in_channel ||
          out_channel == SPA_AUDIO_CHANNEL_UNKNOWN ||
          in_channel == SPA_AUDIO_CHANNEL_UNKNOWN ||
          in_channel == SPA_AUDIO_CHANNEL_MONO)
      {
        g_autoptr (WpProperties) props = NULL;
        WpLink *link;

        /* Create the properties */
        props = wp_properties_new_empty ();
        wp_properties_setf (props, PW_KEY_LINK_OUTPUT_NODE, "%u", out_node_id);
        wp_properties_setf (props, PW_KEY_LINK_OUTPUT_PORT, "%u", out_port_id);
        wp_properties_setf (props, PW_KEY_LINK_INPUT_NODE, "%u", in_node_id);
        wp_properties_setf (props, PW_KEY_LINK_INPUT_PORT, "%u", in_port_id);
        if (eplink_id != SPA_ID_INVALID)
          wp_properties_setf (props, "endpoint-link.id", "%u", eplink_id);
        if (self->passive)
          wp_properties_set (props, PW_KEY_LINK_PASSIVE, "true");

        wp_debug_object (self, "create pw link: %u:%u (%s) -> %u:%u (%s)",
            out_node_id, out_port_id,
            spa_debug_type_find_name (spa_type_audio_channel, out_channel),
            in_node_id, in_port_id,
            spa_debug_type_find_name (spa_type_audio_channel, in_channel));

        /* create the link */
        link = wp_link_new_from_factory (core, "link-factory",
            g_steal_pointer (&props));
        g_ptr_array_add (self->node_links, link);

        /* activate to ensure it is created without errors */
        self->n_async_ops_wait++;
        wp_object_activate (WP_OBJECT (link),
            WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL, NULL,
            (GAsyncReadyCallback) on_link_activated, transition);

        /* continue to link all input ports, if requested */
        if (link_all)
          continue;

        /* remove the linked input port from the array */
        g_ptr_array_remove_index (in_ports_arr, i - 1);

        /* break out of the for loop; go for the next out port */
        break;
      }
    }
  }
  g_variant_iter_free (iter);
  return TRUE;
}

static void
si_standard_link_do_link (WpSiStandardLink *self, WpTransition *transition)
{
  g_autoptr (WpSessionItem) si_out = g_weak_ref_get (&self->out_item);
  g_autoptr (WpSessionItem) si_in = g_weak_ref_get (&self->in_item);
  g_autoptr (GVariant) out_ports = NULL;
  g_autoptr (GVariant) in_ports = NULL;

  out_ports = wp_si_port_info_get_ports (WP_SI_PORT_INFO (si_out),
      self->out_item_port_context);
  in_ports = wp_si_port_info_get_ports (WP_SI_PORT_INFO (si_in),
      self->in_item_port_context);

  if (!create_links (self, transition, out_ports, in_ports))
      wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
          WP_LIBRARY_ERROR_INVARIANT,
          "Bad port info returned from one of the items"));
}

static void
si_standard_link_enable_active (WpSessionItem *si, WpTransition *transition)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (si);
  g_autoptr (WpSessionItem) si_out = NULL;
  g_autoptr (WpSessionItem) si_in = NULL;
  WpSiAcquisition *out_acquisition, *in_acquisition;

  if (!wp_session_item_is_configured (si)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "si-standard-link: item is not configured"));
    return;
  }

  /* acquire */
  si_out = g_weak_ref_get (&self->out_item);
  si_in = g_weak_ref_get (&self->in_item);
  out_acquisition = wp_si_port_info_get_acquisition (WP_SI_PORT_INFO (si_out));
  in_acquisition = wp_si_port_info_get_acquisition (WP_SI_PORT_INFO (si_in));

  if (out_acquisition && in_acquisition)
    self->n_async_ops_wait = 2;
  else if (out_acquisition || in_acquisition)
    self->n_async_ops_wait = 1;
  else {
    self->n_async_ops_wait = 0;
    si_standard_link_do_link (self, transition);
    return;
  }

  if (out_acquisition) {
    wp_si_acquisition_acquire (out_acquisition, WP_SI_LINK (self),
        WP_SI_PORT_INFO (si_out), (GAsyncReadyCallback) on_item_acquired,
        transition);
  }
  if (in_acquisition) {
    wp_si_acquisition_acquire (in_acquisition, WP_SI_LINK (self),
        WP_SI_PORT_INFO (si_in), (GAsyncReadyCallback) on_item_acquired,
        transition);
  }
}

static void
on_impl_endpoint_link_activated (WpObject * object, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiStandardLink *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (object, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  wp_object_update_features (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_EXPORTED, 0);
}

static void
si_standard_link_enable_exported (WpSessionItem *si, WpTransition *transition)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (si);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  self->impl_endpoint_link = wp_impl_endpoint_link_new (core,
      WP_SI_LINK (self));

  g_signal_connect_object (self->impl_endpoint_link, "pw-proxy-destroyed",
      G_CALLBACK (wp_session_item_handle_proxy_destroyed), self, 0);

  wp_object_activate (WP_OBJECT (self->impl_endpoint_link),
      WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_impl_endpoint_link_activated, transition);
}

static void
si_standard_link_finalize (GObject * object)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (object);

  g_weak_ref_clear (&self->out_item);
  g_weak_ref_clear (&self->in_item);
}

static void
si_standard_link_class_init (WpSiStandardLinkClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  object_class->finalize = si_standard_link_finalize;

  si_class->reset = si_standard_link_reset;
  si_class->configure = si_standard_link_configure;
  si_class->get_associated_proxy = si_standard_link_get_associated_proxy;
  si_class->disable_active = si_standard_link_disable_active;
  si_class->disable_exported = si_standard_link_disable_exported;
  si_class->enable_active = si_standard_link_enable_active;
  si_class->enable_exported = si_standard_link_enable_exported;
}

static GVariant *
si_standard_link_get_registration_info (WpSiLink * item)
{
  GVariantBuilder b;
  g_variant_builder_init (&b, G_VARIANT_TYPE ("a{ss}"));
  return g_variant_builder_end (&b);
}

static WpSiPortInfo *
si_standard_link_get_out_item (WpSiLink * item)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);
  return WP_SI_PORT_INFO (g_weak_ref_get (&self->out_item));
}

static WpSiPortInfo *
si_standard_link_get_in_item (WpSiLink * item)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);
  return WP_SI_PORT_INFO (g_weak_ref_get (&self->in_item));
}

static void
si_standard_link_link_init (WpSiLinkInterface * iface)
{
  iface->get_registration_info = si_standard_link_get_registration_info;
  iface->get_out_item = si_standard_link_get_out_item;
  iface->get_in_item = si_standard_link_get_in_item;
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_si_factory_register (core, wp_si_factory_new_simple (SI_FACTORY_NAME,
      si_standard_link_get_type ()));
  return TRUE;
}
