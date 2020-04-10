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

enum {
  STEP_ACQUIRE = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_LINK,
};

struct _WpSiStandardLink
{
  WpSessionItem parent;

  WpSiStream *out_stream;
  WpSiStream *in_stream;

  GPtrArray *node_links;
  guint n_async_ops_wait;
};

static void si_standard_link_link_init (WpSiLinkInterface * iface);

G_DECLARE_FINAL_TYPE (WpSiStandardLink, si_standard_link, WP, SI_STANDARD_LINK, WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiStandardLink, si_standard_link, WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_LINK, si_standard_link_link_init))

static void
on_stream_destroyed (gpointer data, GObject * stream)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (data);

  if ((gpointer) self->out_stream == (gpointer) stream)
    self->out_stream = NULL;
  else if ((gpointer) self->in_stream == (gpointer) stream)
    self->in_stream = NULL;

  wp_session_item_reset (WP_SESSION_ITEM (self));
}

static void
on_stream_flags_changed (WpSessionItem * stream, WpSiFlags flags,
    WpSiStandardLink *self)
{
  /* stream was deactivated; treat it as destroyed and reset */
  if (!(flags & WP_SI_FLAG_ACTIVE))
    wp_session_item_reset (WP_SESSION_ITEM (self));
}

static inline void
disconnect_stream (WpSiStandardLink *self, WpSiStream * stream)
{
  if (stream) {
    g_signal_handlers_disconnect_by_data (stream, self);
    g_object_weak_unref (G_OBJECT (stream), on_stream_destroyed, self);
  }
}

static void
si_standard_link_init (WpSiStandardLink * self)
{
}

static void
si_standard_link_reset (WpSessionItem * item)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);

  WP_SESSION_ITEM_CLASS (si_standard_link_parent_class)->reset (item);

  disconnect_stream (self, self->out_stream);
  disconnect_stream (self, self->in_stream);
  self->out_stream = NULL;
  self->in_stream = NULL;

  wp_session_item_clear_flag (item, WP_SI_FLAG_CONFIGURED);
}

static GVariant *
si_standard_link_get_configuration (WpSessionItem * item)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);
  GVariantBuilder b;

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "out-stream", g_variant_new_uint64 ((guint64) self->out_stream));
  g_variant_builder_add (&b, "{sv}",
      "in-stream", g_variant_new_uint64 ((guint64) self->in_stream));
  return g_variant_builder_end (&b);
}

static gboolean
si_standard_link_configure (WpSessionItem * item, GVariant * args)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);
  guint64 out_stream_i, in_stream_i;
  WpSessionItem *out_stream, *in_stream;

  if (wp_session_item_get_flags (item) &
          (WP_SI_FLAG_ACTIVATING | WP_SI_FLAG_ACTIVE |
           WP_SI_FLAG_EXPORTING | WP_SI_FLAG_EXPORTED))
    return FALSE;

  if (!g_variant_lookup (args, "out-stream", "t", &out_stream_i) ||
      !g_variant_lookup (args, "in-stream", "t", &in_stream_i))
    return FALSE;

  out_stream = GUINT_TO_POINTER (out_stream_i);
  in_stream = GUINT_TO_POINTER (in_stream_i);

  if (!WP_IS_SI_STREAM (out_stream) || !WP_IS_SI_STREAM (in_stream) ||
      !WP_IS_SI_PORT_INFO (out_stream) || !WP_IS_SI_PORT_INFO (in_stream) ||
      !(wp_session_item_get_flags (out_stream) & WP_SI_FLAG_ACTIVE) ||
      !(wp_session_item_get_flags (in_stream) & WP_SI_FLAG_ACTIVE))
    return FALSE;

  disconnect_stream (self, self->out_stream);
  disconnect_stream (self, self->in_stream);

  self->out_stream = WP_SI_STREAM (out_stream);
  self->in_stream = WP_SI_STREAM (in_stream);

  g_signal_connect_object (self->out_stream, "flags-changed",
      G_CALLBACK (on_stream_flags_changed), self, 0);
  g_signal_connect_object (self->in_stream, "flags-changed",
      G_CALLBACK (on_stream_flags_changed), self, 0);
  g_object_weak_ref (G_OBJECT (self->out_stream), on_stream_destroyed, self);
  g_object_weak_ref (G_OBJECT (self->in_stream), on_stream_destroyed, self);

  wp_session_item_set_flag (item, WP_SI_FLAG_CONFIGURED);

  return TRUE;
}

static guint
si_standard_link_get_next_step (WpSessionItem * item,
     WpTransition * transition, guint step)
{
  WpSiStandardLink *self = wp_transition_get_source_object (transition);

  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      return STEP_ACQUIRE;

    case STEP_ACQUIRE:
      if (self->n_async_ops_wait == 0)
        return STEP_LINK;
      else
        return step;

    case STEP_LINK:
      if (self->n_async_ops_wait == 0)
        return WP_TRANSITION_STEP_NONE;
      else
        return step;

    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static void
on_stream_acquired (WpSiStreamAcquisition * acq, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiStandardLink *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_si_stream_acquisition_acquire_finish (acq, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  self->n_async_ops_wait--;
  wp_transition_advance (transition);
}

static void
on_link_augmented (WpProxy * proxy, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiStandardLink *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  if (!wp_proxy_augment_finish (proxy, res, &error)) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  self->n_async_ops_wait--;
  wp_transition_advance (transition);
}

static WpCore *
find_core (WpSiStandardLink * self)
{
  /* session items are not associated with a core, but surely when linking
    we should be able to find a WpImplEndpointLink associated, or at the very
    least a WpEndpoint associated with one of the streams... */
  g_autoptr (WpProxy) proxy = wp_session_item_get_associated_proxy (
      WP_SESSION_ITEM (self), WP_TYPE_ENDPOINT_LINK);
  if (!proxy) {
      proxy = wp_session_item_get_associated_proxy (
          WP_SESSION_ITEM (self->out_stream), WP_TYPE_ENDPOINT);
  }
  return proxy ? wp_proxy_get_core (proxy) : NULL;
}

static gboolean
create_links (WpSiStandardLink * self, GVariant * out_ports, GVariant * in_ports)
{
  g_autoptr (GPtrArray) in_ports_arr = NULL;
  g_autoptr (WpCore) core = NULL;
  WpLink *link;
  GVariantIter *iter;
  GVariant *child;
  guint32 out_node_id, in_node_id;
  guint32 out_port_id, in_port_id;
  guint32 out_channel, in_channel;
  gboolean link_all = FALSE;
  guint i;

  /* tuple format:
      uint32 node_id;
      uint32 port_id;
      uint32 channel;  // enum spa_audio_channel
   */
  if (!g_variant_is_of_type (out_ports, G_VARIANT_TYPE("a(uuu)")))
    return FALSE;
  if (!g_variant_is_of_type (in_ports, G_VARIANT_TYPE("a(uuu)")))
    return FALSE;

  core = find_core (self);
  g_return_val_if_fail (core, FALSE);

  self->n_async_ops_wait = 0;
  self->node_links = g_ptr_array_new_with_free_func (g_object_unref);

  /* transfer the in ports to an array so that we can
     delete them when they are linked */
  i = g_variant_n_children (in_ports);
  in_ports_arr = g_ptr_array_new_full (i, (GDestroyNotify) g_variant_unref);
  g_ptr_array_set_size (in_ports_arr, i);

  g_variant_get (in_ports, "a(uuu)", &iter);
  while ((child = g_variant_iter_next_value (iter)))
    g_ptr_array_insert (in_ports_arr, --i, child);
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
          in_channel == SPA_AUDIO_CHANNEL_UNKNOWN)
      {
        g_autoptr (WpProperties) props = NULL;

        /* Create the properties */
        props = wp_properties_new_empty ();
        wp_properties_setf (props, PW_KEY_LINK_OUTPUT_NODE, "%u", out_node_id);
        wp_properties_setf (props, PW_KEY_LINK_OUTPUT_PORT, "%u", out_port_id);
        wp_properties_setf (props, PW_KEY_LINK_INPUT_NODE, "%u", in_node_id);
        wp_properties_setf (props, PW_KEY_LINK_INPUT_PORT, "%u", in_port_id);

        g_debug ("Create pw link: %u:%u (%s) -> %u:%u (%s)",
            out_node_id, out_port_id,
            spa_debug_type_find_name (spa_type_audio_channel, out_channel),
            in_node_id, in_port_id,
            spa_debug_type_find_name (spa_type_audio_channel, in_channel));

        /* create the link */
        link = wp_link_new_from_factory (core, "link-factory",
            g_steal_pointer (&props));
        g_ptr_array_add (self->node_links, link);

        /* augment to ensure it is created without errors */
        self->n_async_ops_wait++;
        wp_proxy_augment (WP_PROXY (link), WP_PROXY_FEATURES_STANDARD, NULL,
            (GAsyncReadyCallback) on_link_augmented, self);

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
  return TRUE;
}

static void
si_standard_link_execute_step (WpSessionItem * item, WpTransition * transition,
    guint step)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);

  switch (step) {
  case STEP_ACQUIRE: {
    WpSiEndpoint *out_endpoint, *in_endpoint;
    WpSiStreamAcquisition *out_acquisition, *in_acquisition;

    out_endpoint = wp_si_stream_get_parent_endpoint (self->out_stream);
    in_endpoint = wp_si_stream_get_parent_endpoint (self->in_stream);
    out_acquisition = wp_si_endpoint_get_stream_acquisition (out_endpoint);
    in_acquisition = wp_si_endpoint_get_stream_acquisition (in_endpoint);

    if (out_acquisition && in_acquisition)
      self->n_async_ops_wait = 2;
    else if (out_acquisition || in_acquisition)
      self->n_async_ops_wait = 1;
    else {
      self->n_async_ops_wait = 0;
      wp_transition_advance (transition);
      return;
    }

    if (out_acquisition) {
      wp_si_stream_acquisition_acquire (out_acquisition, WP_SI_LINK (self),
          self->out_stream, (GAsyncReadyCallback) on_stream_acquired,
          transition);
    }
    if (in_acquisition) {
      wp_si_stream_acquisition_acquire (in_acquisition, WP_SI_LINK (self),
          self->in_stream, (GAsyncReadyCallback) on_stream_acquired,
          transition);
    }
    break;
  }
  case STEP_LINK: {
    g_autoptr (GVariant) out_ports = NULL;
    g_autoptr (GVariant) in_ports = NULL;

    out_ports = wp_si_port_info_get_ports (WP_SI_PORT_INFO (self->out_stream),
        NULL);
    in_ports = wp_si_port_info_get_ports (WP_SI_PORT_INFO (self->in_stream),
        NULL);

    if (!create_links (self, out_ports, in_ports)) {
      wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
              WP_LIBRARY_ERROR_INVARIANT,
              "Bad port info returned from one of the streams"));
    }
    break;
  }
  default:
    WP_SESSION_ITEM_GET_CLASS (si_standard_link_parent_class)->execute_step (
          item, transition, step);
      break;
  }
}

static void
si_standard_link_deactivate (WpSessionItem * item)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);
  WpSiEndpoint *out_endpoint, *in_endpoint;
  WpSiStreamAcquisition *out_acquisition, *in_acquisition;

  out_endpoint = wp_si_stream_get_parent_endpoint (self->out_stream);
  in_endpoint = wp_si_stream_get_parent_endpoint (self->in_stream);
  out_acquisition = wp_si_endpoint_get_stream_acquisition (out_endpoint);
  in_acquisition = wp_si_endpoint_get_stream_acquisition (in_endpoint);

  if (out_acquisition) {
    wp_si_stream_acquisition_release (out_acquisition, WP_SI_LINK (self),
        self->out_stream);
  }
  if (in_acquisition) {
    wp_si_stream_acquisition_release (in_acquisition, WP_SI_LINK (self),
        self->in_stream);
  }

  g_clear_pointer (&self->node_links, g_ptr_array_unref);

  WP_SESSION_ITEM_CLASS (si_standard_link_parent_class)->deactivate (item);
}

static void
si_standard_link_class_init (WpSiStandardLinkClass * klass)
{
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  si_class->reset = si_standard_link_reset;
  si_class->configure = si_standard_link_configure;
  si_class->get_configuration = si_standard_link_get_configuration;
  si_class->get_next_step = si_standard_link_get_next_step;
  si_class->execute_step = si_standard_link_execute_step;
  si_class->deactivate = si_standard_link_deactivate;
}

static GVariant *
si_standard_link_get_registration_info (WpSiLink * item)
{
  GVariantBuilder b;
  g_variant_builder_init (&b, G_VARIANT_TYPE ("a{ss}"));
  return g_variant_builder_end (&b);
}

static WpProperties *
si_standard_link_get_properties (WpSiLink * item)
{
  return wp_properties_new_empty ();
}

static WpSiStream *
si_standard_link_get_out_stream (WpSiLink * item)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);
  return self->out_stream;
}

static WpSiStream *
si_standard_link_get_in_stream (WpSiLink * item)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);
  return self->in_stream;
}

static void
si_standard_link_link_init (WpSiLinkInterface * iface)
{
  iface->get_registration_info = si_standard_link_get_registration_info;
  iface->get_properties = si_standard_link_get_properties;
  iface->get_out_stream = si_standard_link_get_out_stream;
  iface->get_in_stream = si_standard_link_get_in_stream;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "(ssymv)", "out-stream", "t",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);
  g_variant_builder_add (&b, "(ssymv)", "in-stream", "t",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);

  wp_si_factory_register (core, wp_si_factory_new_simple (
          "si-standard-link",
          si_standard_link_get_type (),
          g_variant_builder_end (&b)));
}
