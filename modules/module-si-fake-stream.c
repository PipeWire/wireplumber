/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

enum {
  STEP_VERIFY_CONFIG = WP_TRANSITION_STEP_CUSTOM_START,
};

struct _WpSiFakeStream
{
  WpSessionItem parent;

  /* configuration */
  gchar name[96];
};

static void si_fake_stream_stream_init (WpSiStreamInterface * iface);
static void si_fake_stream_port_info_init (WpSiPortInfoInterface * iface);

G_DECLARE_FINAL_TYPE(WpSiFakeStream, si_fake_stream, WP, SI_FAKE_STREAM, WpSessionItem)
G_DEFINE_TYPE_WITH_CODE (WpSiFakeStream, si_fake_stream, WP_TYPE_SESSION_ITEM,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_STREAM, si_fake_stream_stream_init)
    G_IMPLEMENT_INTERFACE (WP_TYPE_SI_PORT_INFO, si_fake_stream_port_info_init))

static void
si_fake_stream_init (WpSiFakeStream * self)
{
}

static void
si_fake_stream_reset (WpSessionItem * item)
{
  WpSiFakeStream *self = WP_SI_FAKE_STREAM (item);

  /* unexport & deactivate first */
  WP_SESSION_ITEM_CLASS (si_fake_stream_parent_class)->reset (item);

  self->name[0] = '\0';

  wp_session_item_clear_flag (item, WP_SI_FLAG_CONFIGURED);
}

static gboolean
si_fake_stream_configure (WpSessionItem * item, GVariant * args)
{
  WpSiFakeStream *self = WP_SI_FAKE_STREAM (item);
  const gchar *name;

  if (wp_session_item_get_flags (item) & (WP_SI_FLAG_ACTIVATING | WP_SI_FLAG_ACTIVE))
    return FALSE;

  /* reset previous config */
  si_fake_stream_reset (item);

  /* get name */
  if (!g_variant_lookup (args, "name", "&s", &name))
    return FALSE;
  strncpy (self->name, name, sizeof (self->name) - 1);

  wp_session_item_set_flag (item, WP_SI_FLAG_CONFIGURED);
  return TRUE;
}

static GVariant *
si_fake_stream_get_configuration (WpSessionItem * item)
{
  WpSiFakeStream *self = WP_SI_FAKE_STREAM (item);
  GVariantBuilder b;

  /* Set the properties */
  g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&b, "{sv}",
      "name", g_variant_new_string (self->name));
  return g_variant_builder_end (&b);
}

static guint
si_fake_stream_activate_get_next_step (WpSessionItem * item,
    WpTransition * transition, guint step)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      return STEP_VERIFY_CONFIG;

    case STEP_VERIFY_CONFIG:
      return WP_TRANSITION_STEP_NONE;

    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static void
si_fake_stream_activate_execute_step (WpSessionItem * item,
    WpTransition * transition, guint step)
{
  switch (step) {
    case STEP_VERIFY_CONFIG:
      if (G_UNLIKELY (!(wp_session_item_get_flags (item) & WP_SI_FLAG_CONFIGURED))) {
        wp_transition_return_error (transition,
            g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
                "si-fake-stream: cannot activate item without it "
                "being configured first"));
      }
      wp_transition_advance (transition);
      break;

    default:
      g_return_if_reached ();
  }
}

static GVariant *
si_fake_stream_get_stream_registration_info (WpSiStream * item)
{
  WpSiFakeStream *self = WP_SI_FAKE_STREAM (item);
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(sa{ss})"));
  g_variant_builder_add (&b, "s", self->name);
  g_variant_builder_add (&b, "a{ss}", NULL);

  return g_variant_builder_end (&b);
}

static WpProperties *
si_fake_stream_get_stream_properties (WpSiStream * self)
{
  return NULL;
}

static WpSiEndpoint *
si_fake_stream_get_stream_parent_endpoint (WpSiStream * self)
{
  return WP_SI_ENDPOINT (wp_session_item_get_parent (WP_SESSION_ITEM (self)));
}

static void
si_fake_stream_stream_init (WpSiStreamInterface * iface)
{
  iface->get_registration_info = si_fake_stream_get_stream_registration_info;
  iface->get_properties = si_fake_stream_get_stream_properties;
  iface->get_parent_endpoint = si_fake_stream_get_stream_parent_endpoint;
}

static void
si_fake_stream_class_init (WpSiFakeStreamClass * klass)
{
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  si_class->reset = si_fake_stream_reset;
  si_class->configure = si_fake_stream_configure;
  si_class->get_configuration = si_fake_stream_get_configuration;
  si_class->activate_get_next_step = si_fake_stream_activate_get_next_step;
  si_class->activate_execute_step = si_fake_stream_activate_execute_step;
}

static GVariant *
si_fake_stream_get_ports (WpSiPortInfo * item, const gchar * context)
{
  return NULL;
}

static void
si_fake_stream_port_info_init (WpSiPortInfoInterface * iface)
{
  iface->get_ports = si_fake_stream_get_ports;
}

WP_PLUGIN_EXPORT void
wireplumber__module_init (WpModule * module, WpCore * core, GVariant * args)
{
  GVariantBuilder b;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a(ssymv)"));
  g_variant_builder_add (&b, "(ssymv)", "name", "s",
      WP_SI_CONFIG_OPTION_WRITEABLE | WP_SI_CONFIG_OPTION_REQUIRED, NULL);

  wp_si_factory_register (core, wp_si_factory_new_simple (
      "si-fake-stream", si_fake_stream_get_type (), g_variant_builder_end (&b)));
}
