/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/keys.h>

enum {
  STEP_VERIFY_CONFIG = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_ENSURE_ADAPTER_FEATURES,
  STEP_ENSURE_CONVERT_FEATURES,
};

struct _WpSiAudioSoftdspEndpoint
{
  WpSessionBin parent;

  /* configuration */
  WpSessionItem *adapter;
  guint num_streams;

  guint activated_streams;
};

static void si_audio_softdsp_endpoint_endpoint_init (WpSiEndpointInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiAudioSoftdspEndpoint, si_audio_softdsp_endpoint,
                     WP, SI_AUDIO_SOFTDSP_ENDPOINT, WpSessionBin)
G_DEFINE_TYPE_WITH_CODE (WpSiAudioSoftdspEndpoint, si_audio_softdsp_endpoint, WP_TYPE_SESSION_BIN,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_ENDPOINT, si_audio_softdsp_endpoint_endpoint_init))

static GVariant *
si_audio_softdsp_endpoint_get_registration_info (WpSiEndpoint * item)
{
  WpSiAudioSoftdspEndpoint *self = WP_SI_AUDIO_SOFTDSP_ENDPOINT (item);

  return wp_si_endpoint_get_registration_info (WP_SI_ENDPOINT (self->adapter));
}

static WpProperties *
si_audio_softdsp_endpoint_get_properties (WpSiEndpoint * item)
{
  WpSiAudioSoftdspEndpoint *self = WP_SI_AUDIO_SOFTDSP_ENDPOINT (item);

  return wp_si_endpoint_get_properties (WP_SI_ENDPOINT (self->adapter));
}

static guint
si_audio_softdsp_endpoint_get_n_streams (WpSiEndpoint * item)
{
  guint n_streams = wp_session_bin_get_n_children (WP_SESSION_BIN (item));
  /* n_streams includes the adapter; remove it, unless it's the only one */
  return (n_streams > 1) ? (n_streams - 1) : 1;
}

static WpSiStream *
si_audio_softdsp_endpoint_get_stream (WpSiEndpoint * item, guint index)
{
  WpSiAudioSoftdspEndpoint *self = WP_SI_AUDIO_SOFTDSP_ENDPOINT (item);
  g_autoptr (WpIterator) it = wp_session_bin_iterate (WP_SESSION_BIN (self));
  g_auto (GValue) val = G_VALUE_INIT;

  if (wp_session_bin_get_n_children (WP_SESSION_BIN (item)) == 1)
    return WP_SI_STREAM (self->adapter);

  /* TODO: do not asume the items are always sorted */
  guint i = 0;
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    if (index + 1 == i)
      return g_value_get_object (&val);
    i++;
  }

  return NULL;
}

static void
si_audio_softdsp_endpoint_endpoint_init (WpSiEndpointInterface * iface)
{
  iface->get_registration_info = si_audio_softdsp_endpoint_get_registration_info;
  iface->get_properties = si_audio_softdsp_endpoint_get_properties;
  iface->get_n_streams = si_audio_softdsp_endpoint_get_n_streams;
  iface->get_stream = si_audio_softdsp_endpoint_get_stream;
}

static void
si_audio_softdsp_endpoint_init (WpSiAudioSoftdspEndpoint * self)
{
  self->activated_streams = 0;
}

static void
si_audio_softdsp_endpoint_reset (WpSessionItem * item)
{
  WpSiAudioSoftdspEndpoint *self = WP_SI_AUDIO_SOFTDSP_ENDPOINT (item);

  /* unexport & deactivate first */
  WP_SESSION_ITEM_CLASS (si_audio_softdsp_endpoint_parent_class)->reset (item);

  g_clear_object (&self->adapter);
  self->num_streams = 0;
  self->activated_streams = 0;

  wp_session_item_clear_flag (item, WP_SI_FLAG_CONFIGURED);
}

static gpointer
si_audio_softdsp_endpoint_get_associated_proxy (WpSessionItem * item,
    GType proxy_type)
{
  WpSiAudioSoftdspEndpoint *self = WP_SI_AUDIO_SOFTDSP_ENDPOINT (item);

  if (proxy_type == WP_TYPE_NODE)
    return wp_session_item_get_associated_proxy (self->adapter, proxy_type);

  return WP_SESSION_ITEM_CLASS (
      si_audio_softdsp_endpoint_parent_class)->get_associated_proxy (
          item, proxy_type);
}

static gboolean
si_audio_softdsp_endpoint_configure (WpSessionItem * item, GVariant * args)
{
  WpSiAudioSoftdspEndpoint *self = WP_SI_AUDIO_SOFTDSP_ENDPOINT (item);
  guint64 adapter_i;
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpSessionItem) adapter = NULL;
  GVariantBuilder b;

  if (wp_session_item_get_flags (item) & (WP_SI_FLAG_ACTIVATING | WP_SI_FLAG_ACTIVE))
    return FALSE;

  /* reset previous config */
  si_audio_softdsp_endpoint_reset (WP_SESSION_ITEM (self));

  /* get the adapter and its core */
  if (!g_variant_lookup (args, "adapter", "t", &adapter_i))
    return FALSE;
  g_return_val_if_fail (WP_IS_SI_ENDPOINT (GUINT_TO_POINTER (adapter_i)), FALSE);
  self->adapter = g_object_ref (GUINT_TO_POINTER (adapter_i));
  node = wp_session_item_get_associated_proxy (self->adapter, WP_TYPE_NODE);
  core = wp_proxy_get_core (WP_PROXY (node));

  /* add the adapter into the bin */
  wp_session_bin_add (WP_SESSION_BIN (self), g_object_ref (self->adapter));

  /* get the number of streams */
  g_variant_lookup (args, "num-streams", "u", &self->num_streams);

  /* create, configure and add the convert items into the bin */
  for (guint i = 0; i < self->num_streams; i++) {
    g_autoptr (WpSessionItem) convert =
        wp_session_item_make (core, "si-convert");
    g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&b, "{sv}",
        "target", g_variant_new_uint64 ((guint64) self->adapter));
    wp_session_item_configure (convert, g_variant_builder_end (&b));
    wp_session_bin_add (WP_SESSION_BIN (self), g_steal_pointer (&convert));
  }

  wp_session_item_set_flag (item, WP_SI_FLAG_CONFIGURED);

  return TRUE;
}

static GVariant *
si_audio_softdsp_endpoint_get_configuration (WpSessionItem * item)
{
  WpSiAudioSoftdspEndpoint *self = WP_SI_AUDIO_SOFTDSP_ENDPOINT (item);
  GVariantBuilder b;

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "adapter", g_variant_new_uint64 ((guint64) self->adapter));
  g_variant_builder_add (&b, "{sv}",
      "num-streams", g_variant_new_uint32 (self->num_streams));
  return g_variant_builder_end (&b);
}

static guint
si_audio_softdsp_endpoint_activate_get_next_step (WpSessionItem * item,
     WpTransition * transition, guint step)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      return STEP_VERIFY_CONFIG;

    case STEP_VERIFY_CONFIG:
      return STEP_ENSURE_ADAPTER_FEATURES;

    case STEP_ENSURE_ADAPTER_FEATURES:
      return STEP_ENSURE_CONVERT_FEATURES;

    case STEP_ENSURE_CONVERT_FEATURES:
      return WP_TRANSITION_STEP_NONE;

    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static void
on_adapter_activated (WpSessionItem * item, GAsyncResult * res,
    WpTransition *transition)
{
  g_autoptr (GError) error = NULL;
  gboolean activate_ret = wp_session_item_activate_finish (item, res, &error);
  g_return_if_fail (error == NULL);
  g_return_if_fail (activate_ret);
  wp_transition_advance (transition);
}

static void
on_convert_activated (WpSessionItem * item, GAsyncResult * res,
    WpTransition *transition)
{
  WpSiAudioSoftdspEndpoint *self = wp_transition_get_data (transition);
  g_autoptr (GError) error = NULL;

  gboolean activate_ret = wp_session_item_activate_finish (item, res, &error);
  g_return_if_fail (error == NULL);
  g_return_if_fail (activate_ret);

  self->activated_streams++;
  if (self->activated_streams >= self->num_streams)
    wp_transition_advance (transition);
}

static void
si_audio_softdsp_endpoint_activate_execute_step (WpSessionItem * item,
    WpTransition * transition, guint step)
{
  WpSiAudioSoftdspEndpoint *self = WP_SI_AUDIO_SOFTDSP_ENDPOINT (item);

  wp_transition_set_data (transition, g_object_ref (self), g_object_unref);

  switch (step) {
    case STEP_VERIFY_CONFIG:
      if (G_UNLIKELY (!(wp_session_item_get_flags (item) & WP_SI_FLAG_CONFIGURED))) {
        wp_transition_return_error (transition,
            g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
                "si-audio-softdsp-endpoint: cannot activate item without it "
                "being configured first"));
      }
      wp_transition_advance (transition);
      break;

    case STEP_ENSURE_ADAPTER_FEATURES:
      g_return_if_fail (self->activated_streams == 0);
      wp_session_item_activate (self->adapter,
          (GAsyncReadyCallback)on_adapter_activated, transition);
      break;

    case STEP_ENSURE_CONVERT_FEATURES:
    {
      g_autoptr (WpIterator) it = wp_session_bin_iterate (WP_SESSION_BIN (self));
      g_auto (GValue) val = G_VALUE_INIT;
      for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
        WpSessionItem *item = g_value_get_object (&val);
        if (item == self->adapter)
          continue;
        wp_session_item_activate (item,
            (GAsyncReadyCallback)on_convert_activated, transition);
      }
      break;
    }

    default:
      g_return_if_reached ();
  }
}

static void
si_audio_softdsp_endpoint_activate_rollback (WpSessionItem * item)
{
  WpSiAudioSoftdspEndpoint *self = WP_SI_AUDIO_SOFTDSP_ENDPOINT (item);
  g_autoptr (WpIterator) it = wp_session_bin_iterate (WP_SESSION_BIN (self));
  g_auto (GValue) val = G_VALUE_INIT;

  /* deactivate all items */
  for (; wp_iterator_next (it, &val); g_value_unset (&val))
    wp_session_item_deactivate (g_value_get_object (&val));

  self->activated_streams = 0;
}

static void
si_audio_softdsp_endpoint_class_init (WpSiAudioSoftdspEndpointClass * klass)
{
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  si_class->reset = si_audio_softdsp_endpoint_reset;
  si_class->get_associated_proxy = si_audio_softdsp_endpoint_get_associated_proxy;
  si_class->configure = si_audio_softdsp_endpoint_configure;
  si_class->get_configuration = si_audio_softdsp_endpoint_get_configuration;
  si_class->activate_get_next_step =
      si_audio_softdsp_endpoint_activate_get_next_step;
  si_class->activate_execute_step =
      si_audio_softdsp_endpoint_activate_execute_step;
  si_class->activate_rollback = si_audio_softdsp_endpoint_activate_rollback;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a(ssymv)"));
  g_variant_builder_add (&b, "(ssymv)", "adapter", "t",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);
  g_variant_builder_add (&b, "(ssymv)", "num-streams", "u",
      WP_SI_CONFIG_OPTION_WRITEABLE, NULL);

  wp_si_factory_register (core, wp_si_factory_new_simple (
      "si-audio-softdsp-endpoint", si_audio_softdsp_endpoint_get_type (),
      g_variant_builder_end (&b)));
}
