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

WP_DEFINE_LOCAL_LOG_TOPIC ("m-si-standard-link")

#define SI_FACTORY_NAME "si-standard-link"

struct _WpSiStandardLink
{
  WpSessionItem parent;

  /* configuration */
  GWeakRef out_item;
  GWeakRef in_item;
  const gchar *out_item_port_context;
  const gchar *in_item_port_context;
  gboolean passthrough;

  /* activate */
  GPtrArray *node_links;
  guint n_active_links;
  guint n_failed_links;
  guint n_async_ops_wait;
};

enum {
  SIGNAL_LINK_ERROR,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

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
si_standard_link_reset (WpSessionItem * item)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);

  /* deactivate first */
  wp_object_deactivate (WP_OBJECT (self),
      WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED);

  /* reset */
  g_weak_ref_set (&self->out_item, NULL);
  g_weak_ref_set (&self->in_item, NULL);
  self->out_item_port_context = NULL;
  self->in_item_port_context = NULL;
  self->passthrough = FALSE;

  WP_SESSION_ITEM_CLASS (si_standard_link_parent_class)->reset (item);
}

static WpSessionItem *
get_and_validate_item (WpProperties * props, const gchar *key)
{
  WpSessionItem *res = NULL;
  const gchar *str = NULL;

  str = wp_properties_get (props, key);
  if (!str || sscanf(str, "%p", &res) != 1 || !WP_IS_SI_LINKABLE (res) ||
      !(wp_object_test_active_features (WP_OBJECT (res),
          WP_SESSION_ITEM_FEATURE_ACTIVE)))
    return NULL;

  return res;
}

static gboolean
si_standard_link_configure (WpSessionItem * item, WpProperties * p)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);
  g_autoptr (WpProperties) si_props = wp_properties_ensure_unique_owner (p);
  WpSessionItem *out_item, *in_item;
  const gchar *str;

  /* reset previous config */
  si_standard_link_reset (item);

  out_item = get_and_validate_item (si_props, "out.item");
  if (!out_item)
    return FALSE;
  wp_properties_setf (si_props, "out.item.id", "%u",
      wp_object_get_id (WP_OBJECT (out_item)));

  in_item = get_and_validate_item (si_props, "in.item");
  if (!in_item)
    return FALSE;
  wp_properties_setf (si_props, "in.item.id", "%u",
      wp_object_get_id (WP_OBJECT (in_item)));

  self->out_item_port_context = wp_properties_get (si_props,
      "out.item.port.context");

  self->in_item_port_context = wp_properties_get (si_props,
      "in.item.port.context");

  str = wp_properties_get (si_props, "passthrough");
  self->passthrough = str && pw_properties_parse_bool (str);

  g_weak_ref_set(&self->out_item, out_item);
  g_weak_ref_set(&self->in_item, in_item);

  wp_properties_set (si_props, "item.factory.name", SI_FACTORY_NAME);
  wp_session_item_set_properties (WP_SESSION_ITEM (self),
      g_steal_pointer (&si_props));
  return TRUE;
}

static gpointer
si_standard_link_get_associated_proxy (WpSessionItem * item, GType proxy_type)
{
  return NULL;
}

static void
request_destroy_link (gpointer data, gpointer user_data)
{
  WpLink *link = WP_LINK (data);

  wp_global_proxy_request_destroy (WP_GLOBAL_PROXY (link));
}

static void
clear_node_links (GPtrArray **node_links_p)
{
  /*
   * Something else (eg. object managers) may be keeping the WpLink
   * objects alive. Deactivate the links now, to destroy the PW objects.
   */
  if (*node_links_p)
    g_ptr_array_foreach (*node_links_p, request_destroy_link, NULL);

  g_clear_pointer (node_links_p, g_ptr_array_unref);
}

static void
si_standard_link_disable_active (WpSessionItem *si)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (si);
  g_autoptr (WpSessionItem) si_out = g_weak_ref_get (&self->out_item);
  g_autoptr (WpSessionItem) si_in = g_weak_ref_get (&self->in_item);
  WpSiAcquisition *out_acquisition, *in_acquisition;

  if (si_out) {
    out_acquisition = wp_si_linkable_get_acquisition (
        WP_SI_LINKABLE (si_out));
    if (out_acquisition)
      wp_si_acquisition_release (out_acquisition, WP_SI_LINK (self),
          WP_SI_LINKABLE (si_out));
  }
  if (si_in) {
    in_acquisition = wp_si_linkable_get_acquisition (WP_SI_LINKABLE (si_in));
    if (in_acquisition)
      wp_si_acquisition_release (in_acquisition, WP_SI_LINK (self),
          WP_SI_LINKABLE (si_in));
  }

  clear_node_links (&self->node_links);

  self->n_active_links = 0;
  self->n_failed_links = 0;
  self->n_async_ops_wait = 0;

  wp_object_update_features (WP_OBJECT (self), 0,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
}

static void
on_link_activated (WpObject * proxy, GAsyncResult * res,
    WpTransition * transition)
{
  WpSiStandardLink *self = wp_transition_get_source_object (transition);
  guint len = self->node_links ? self->node_links->len : 0;

  /* Count the number of failed and active links */
  if (wp_object_activate_finish (proxy, res, NULL))
    self->n_active_links++;
  else
    self->n_failed_links++;

  /* Wait for all links to finish activation */
  if (self->n_failed_links + self->n_active_links != len)
    return;

  /* We only active feature if all links activated successfully */
  if (self->n_failed_links > 0) {
    clear_node_links (&self->node_links);
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "%d of %d PipeWire links failed to activate",
        self->n_failed_links, len));
  } else {
    wp_object_update_features (WP_OBJECT (self),
        WP_SESSION_ITEM_FEATURE_ACTIVE, 0);
  }
}

static void
on_link_state_changed (WpLink *link, WpLinkState old_state,
  WpLinkState new_state, WpSiStandardLink * self)
{
  if (new_state == WP_LINK_STATE_ERROR) {
    const gchar *error_msg;
    wp_link_get_state (link, &error_msg);
    g_signal_emit_by_name (self, "link-error", error_msg);
  }
}

struct port
{
  guint32 node_id;
  guint32 port_id;
  guint32 channel;
  gboolean visited;
};

static inline bool
channel_is_aux(guint32 channel)
{
  return channel >= SPA_AUDIO_CHANNEL_START_Aux &&
    channel <= SPA_AUDIO_CHANNEL_LAST_Aux;
}

static inline int
score_ports(struct port *out, struct port *in)
{
  int score = 0;

  if (out->channel == in->channel)
    score += 100;
  else if ((out->channel == SPA_AUDIO_CHANNEL_SL && in->channel == SPA_AUDIO_CHANNEL_RL) ||
            (out->channel == SPA_AUDIO_CHANNEL_RL && in->channel == SPA_AUDIO_CHANNEL_SL) ||
            (out->channel == SPA_AUDIO_CHANNEL_SR && in->channel == SPA_AUDIO_CHANNEL_RR) ||
            (out->channel == SPA_AUDIO_CHANNEL_RR && in->channel == SPA_AUDIO_CHANNEL_SR))
    score += 60;
  else if ((out->channel == SPA_AUDIO_CHANNEL_FC && in->channel == SPA_AUDIO_CHANNEL_MONO) ||
            (out->channel == SPA_AUDIO_CHANNEL_MONO && in->channel == SPA_AUDIO_CHANNEL_FC))
    score += 50;
  else if (in->channel == SPA_AUDIO_CHANNEL_UNKNOWN ||
            in->channel == SPA_AUDIO_CHANNEL_MONO ||
            out->channel == SPA_AUDIO_CHANNEL_UNKNOWN ||
            out->channel == SPA_AUDIO_CHANNEL_MONO)
    score += 10;
  else if (channel_is_aux(in->channel) != channel_is_aux(out->channel))
    score += 7;
  if (score > 0 && !in->visited)
    score += 5;
  if (score <= 10)
    score = 0;
  return score;
}

static gboolean
create_links (WpSiStandardLink * self, WpTransition * transition,
    GVariant * out_ports, GVariant * in_ports)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (GArray) in_ports_arr = NULL;
  struct port out_port = {0};
  struct port *in_port;
  GVariantIter *iter = NULL;
  guint i;

  /* Clear old links if any */
  self->n_active_links = 0;
  self->n_failed_links = 0;
  clear_node_links (&self->node_links);

  /* tuple format:
      uint32 node_id;
      uint32 port_id;
      uint32 channel;  // enum spa_audio_channel
   */
  if (!g_variant_is_of_type (out_ports, G_VARIANT_TYPE("a(uuu)")))
    return FALSE;
  if (!g_variant_is_of_type (in_ports, G_VARIANT_TYPE("a(uuu)")))
    return FALSE;

  i = g_variant_n_children (in_ports);
  if (i == 0)
    return FALSE;

  self->node_links = g_ptr_array_new_with_free_func (g_object_unref);

  /* transfer the in ports to an array so that we can
     mark them when they are linked */
  in_ports_arr = g_array_sized_new (FALSE, TRUE, sizeof (struct port), i + 1);
  g_array_set_size (in_ports_arr, i + 1);
  g_variant_get (in_ports, "a(uuu)", &iter);
  i = 0;
  do {
    in_port = &g_array_index (in_ports_arr, struct port, i++);
  } while (g_variant_iter_loop (iter, "(uuu)", &in_port->node_id,
              &in_port->port_id, &in_port->channel));
  g_variant_iter_free (iter);

  /* now loop over the out ports and figure out where they should be linked */
  g_variant_get (out_ports, "a(uuu)", &iter);
  while (g_variant_iter_loop (iter, "(uuu)", &out_port.node_id,
              &out_port.port_id, &out_port.channel))
  {
    int best_score = 0;
    struct port *best_port = NULL;
    WpProperties *props = NULL;
    WpLink *link;

    for (i = 0; i < in_ports_arr->len - 1; i++) {
      in_port = &g_array_index (in_ports_arr, struct port, i);
      int score = score_ports (&out_port, in_port);
      if (score > best_score) {
        best_score = score;
        best_port = in_port;
      }
    }

    /* not all output ports have to be linked ... */
    if (!best_port || best_port->visited)
      continue;

    best_port->visited = TRUE;

    /* Create the properties */
    props = wp_properties_new_empty ();
    wp_properties_setf (props, PW_KEY_LINK_OUTPUT_NODE, "%u", out_port.node_id);
    wp_properties_setf (props, PW_KEY_LINK_OUTPUT_PORT, "%u", out_port.port_id);
    wp_properties_setf (props, PW_KEY_LINK_INPUT_NODE, "%u", best_port->node_id);
    wp_properties_setf (props, PW_KEY_LINK_INPUT_PORT, "%u", best_port->port_id);

    wp_debug_object (self, "create pw link: %u:%u (%s) -> %u:%u (%s)",
        out_port.node_id, out_port.port_id,
        spa_debug_type_find_name (spa_type_audio_channel, out_port.channel),
        best_port->node_id, best_port->port_id,
        spa_debug_type_find_name (spa_type_audio_channel, best_port->channel));

    /* create the link */
    link = wp_link_new_from_factory (core, "link-factory", props);
    g_ptr_array_add (self->node_links, link);

    /* activate to ensure it is created without errors */
    wp_object_activate_closure (WP_OBJECT (link),
        WP_OBJECT_FEATURES_ALL, NULL,
        g_cclosure_new_object (
            (GCallback) on_link_activated, G_OBJECT (transition)));

    g_signal_connect_object (link, "state-changed",
      G_CALLBACK (on_link_state_changed), self, 0);
  }
  g_variant_iter_free (iter);
  return self->node_links->len > 0;
}

static void
get_ports_and_create_links (WpSiStandardLink *self, WpTransition *transition)
{
  g_autoptr (WpSiLinkable) si_out = NULL;
  g_autoptr (WpSiLinkable) si_in = NULL;
  g_autoptr (GVariant) out_ports = NULL;
  g_autoptr (GVariant) in_ports = NULL;

  si_out = WP_SI_LINKABLE (g_weak_ref_get (&self->out_item));
  si_in = WP_SI_LINKABLE (g_weak_ref_get (&self->in_item));

  if (!si_out || !si_in ||
      !wp_object_test_active_features (WP_OBJECT (si_out), WP_SESSION_ITEM_FEATURE_ACTIVE) ||
      !wp_object_test_active_features (WP_OBJECT (si_in), WP_SESSION_ITEM_FEATURE_ACTIVE)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
            "some node was destroyed before the link was created"));
    return;
  }

  out_ports = wp_si_linkable_get_ports (si_out, self->out_item_port_context);
  in_ports = wp_si_linkable_get_ports (si_in, self->in_item_port_context);
  if (!out_ports || !in_ports) {
    wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
          WP_LIBRARY_ERROR_INVARIANT,
          "Failed to create links because one of the nodes has no ports"));
    return;
  }

  if (!create_links (self, transition, out_ports, in_ports))
      wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
          WP_LIBRARY_ERROR_INVARIANT,
          "Failed to create links because of wrong ports"));
}

static void
on_adapters_ready (GObject *obj, GAsyncResult * res, gpointer p)
{
  WpTransition *transition = p;
  WpSiStandardLink *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;

  wp_si_adapter_set_ports_format_finish (WP_SI_ADAPTER (obj), res, &error);
  if (error) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  /* create links */
  get_ports_and_create_links (self, transition);
}

struct adapter
{
  WpSiAdapter *si;
  gboolean is_device;
  gboolean dont_remix;
  gboolean unpositioned;
  gboolean no_dsp;
  WpSpaPod *fmt;
  const gchar *mode;
};

static void
adapter_free (struct adapter *a)
{
  g_clear_object (&a->si);
  g_clear_pointer (&a->fmt, wp_spa_pod_unref);
  g_slice_free (struct adapter, a);
}

static void
configure_adapter (WpSiStandardLink *self, WpTransition *transition,
    struct adapter *main, struct adapter *other)
{
  /* configure other to have the same format with main, if necessary */
  if (!main->no_dsp && !other->dont_remix && !other->unpositioned && !main->unpositioned) {
    /* if formats are the same, no need to reconfigure */
    if (other->fmt && !g_strcmp0 (main->mode, other->mode)
        && wp_spa_pod_equal (main->fmt, other->fmt))
      get_ports_and_create_links (self, transition);
    else
      wp_si_adapter_set_ports_format (other->si, wp_spa_pod_ref (main->fmt),
          "dsp", on_adapters_ready, transition);
  } else if (main->no_dsp) {
    /* if formats are the same, no need to reconfigure */
    if (other->fmt && !g_strcmp0 (other->mode, "convert")
        && wp_spa_pod_equal (main->fmt, other->fmt))
      get_ports_and_create_links (self, transition);
    else
      wp_si_adapter_set_ports_format (other->si, wp_spa_pod_ref (main->fmt),
          "convert", on_adapters_ready, transition);
  } else {
    /* dont_remix or unpositioned case */
    if (other->fmt)
      get_ports_and_create_links (self, transition);
    else
      wp_si_adapter_set_ports_format (other->si, NULL,
          "dsp", on_adapters_ready, transition);
  }
}

static void
on_main_adapter_ready (GObject *obj, GAsyncResult * res, gpointer p)
{
  WpTransition *transition = p;
  WpSiStandardLink *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;
  struct adapter *main, *other;

  wp_si_adapter_set_ports_format_finish (WP_SI_ADAPTER (obj), res, &error);
  if (error) {
    wp_transition_return_error (transition, g_steal_pointer (&error));
    return;
  }

  main = g_object_get_data (G_OBJECT (transition), "adapter_main");
  other = g_object_get_data (G_OBJECT (transition), "adapter_other");

  if (!wp_object_test_active_features (WP_OBJECT (main->si), WP_SESSION_ITEM_FEATURE_ACTIVE) ||
      !wp_object_test_active_features (WP_OBJECT (other->si), WP_SESSION_ITEM_FEATURE_ACTIVE)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
            "some node was destroyed before the link was created"));
    return;
  }

  if (self->passthrough) {
    wp_si_adapter_set_ports_format (other->si, NULL, "passthrough",
        on_adapters_ready, transition);
  } else {
    /* get the up-to-date formats */
    g_clear_pointer (&main->fmt, wp_spa_pod_unref);
    g_clear_pointer (&other->fmt, wp_spa_pod_unref);
    main->fmt = wp_si_adapter_get_ports_format (main->si, &main->mode);
    other->fmt = wp_si_adapter_get_ports_format (other->si, &other->mode);

    /* now configure other based on main */
    configure_adapter (self, transition, main, other);
  }
}

static void
configure_and_link_adapters (WpSiStandardLink *self, WpTransition *transition)
{
  g_autoptr (WpSiAdapter) si_out =
      WP_SI_ADAPTER (g_weak_ref_get (&self->out_item));
  g_autoptr (WpSiAdapter) si_in =
      WP_SI_ADAPTER (g_weak_ref_get (&self->in_item));
  struct adapter *out, *in, *main, *other;
  const gchar *str = NULL;

  if (!si_out || !si_in ||
      !wp_object_test_active_features (WP_OBJECT (si_out), WP_SESSION_ITEM_FEATURE_ACTIVE) ||
      !wp_object_test_active_features (WP_OBJECT (si_in), WP_SESSION_ITEM_FEATURE_ACTIVE)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
            "some node was destroyed before the link was created"));
    return;
  }

  out = g_slice_new0 (struct adapter);
  in = g_slice_new0 (struct adapter);
  out->si = g_steal_pointer (&si_out);
  in->si = g_steal_pointer (&si_in);

  str = wp_session_item_get_property (WP_SESSION_ITEM (out->si), "item.node.type");
  out->is_device = !g_strcmp0 (str, "device");
  str = wp_session_item_get_property (WP_SESSION_ITEM (in->si), "item.node.type");
  in->is_device = !g_strcmp0 (str, "device");

  str = wp_session_item_get_property (WP_SESSION_ITEM (out->si), "stream.dont-remix");
  out->dont_remix = str && pw_properties_parse_bool (str);
  str = wp_session_item_get_property (WP_SESSION_ITEM (in->si), "stream.dont-remix");
  in->dont_remix = str && pw_properties_parse_bool (str);

  str = wp_session_item_get_property (WP_SESSION_ITEM (out->si), "item.node.unpositioned");
  out->unpositioned = str && pw_properties_parse_bool (str);
  str = wp_session_item_get_property (WP_SESSION_ITEM (in->si), "item.node.unpositioned");
  in->unpositioned = str && pw_properties_parse_bool (str);

  str = wp_session_item_get_property (WP_SESSION_ITEM (out->si), "item.features.no-dsp");
  out->no_dsp = str && pw_properties_parse_bool (str);
  str = wp_session_item_get_property (WP_SESSION_ITEM (in->si), "item.features.no-dsp");
  in->no_dsp = str && pw_properties_parse_bool (str);

  wp_debug_object (self, "out [device:%d, dont_remix %d, unpos %d], "
      "in: [device %d, dont_remix %d, unpos %d]",
      out->is_device, out->dont_remix, out->unpositioned,
      in->is_device, in->dont_remix, in->unpositioned);

  /* we always use out->si format, unless in->si is device */
  if (!out->is_device && in->is_device) {
    main = in;
    other = out;
  } else {
    main = out;
    other = in;
  }

  /* always configure both adapters in passthrough mode
     if this is a passthrough link */
  if (self->passthrough) {
    g_object_set_data_full (G_OBJECT (transition), "adapter_main", main,
        (GDestroyNotify) adapter_free);
    g_object_set_data_full (G_OBJECT (transition), "adapter_other", other,
        (GDestroyNotify) adapter_free);
    wp_si_adapter_set_ports_format (main->si, NULL, "passthrough",
        on_main_adapter_ready, transition);
    return;
  }

  main->fmt = wp_si_adapter_get_ports_format (main->si, &main->mode);
  other->fmt = wp_si_adapter_get_ports_format (other->si, &other->mode);

  if (main->fmt)
    /* ideally, configure other based on main */
    configure_adapter (self, transition, main, other);
  else if (other->fmt)
    /* if main is not configured but other is, do it the other way around */
    configure_adapter (self, transition, other, main);
  else {
    /* no adapter configured, let's configure main first */
    g_object_set_data_full (G_OBJECT (transition), "adapter_main", main,
        (GDestroyNotify) adapter_free);
    g_object_set_data_full (G_OBJECT (transition), "adapter_other", other,
        (GDestroyNotify) adapter_free);
    wp_si_adapter_set_ports_format (main->si, NULL,
        main->no_dsp ? "passthrough" : "dsp", on_main_adapter_ready, transition);
    return;
  }

  adapter_free (main);
  adapter_free (other);
}

static void
si_standard_link_do_link (WpSiStandardLink *self, WpTransition *transition)
{
  g_autoptr (WpSessionItem) si_out = g_weak_ref_get (&self->out_item);
  g_autoptr (WpSessionItem) si_in = g_weak_ref_get (&self->in_item);

  if (!si_out || !si_in ||
      !wp_object_test_active_features ((WP_OBJECT (si_out)), WP_SESSION_ITEM_FEATURE_ACTIVE) ||
      !wp_object_test_active_features ((WP_OBJECT (si_in)), WP_SESSION_ITEM_FEATURE_ACTIVE)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
            "some node was destroyed before the link was created"));
    return;
  }

  if (WP_IS_SI_ADAPTER (si_out) && WP_IS_SI_ADAPTER (si_in))
    configure_and_link_adapters (self, transition);
  else if (!WP_IS_SI_ADAPTER (si_out) && !WP_IS_SI_ADAPTER (si_in))
    get_ports_and_create_links (self, transition);
  else
    wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
          WP_LIBRARY_ERROR_INVARIANT,
          "Adapters cannot be linked with non-adapters"));
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
    si_standard_link_do_link (self, transition);
}

static void
si_standard_link_enable_active (WpSessionItem *si, WpTransition *transition)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (si);
  g_autoptr (WpSessionItem) si_out = NULL;
  g_autoptr (WpSessionItem) si_in = NULL;
  WpSiAcquisition *out_acquisition = NULL, *in_acquisition = NULL;

  if (!wp_session_item_is_configured (si)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "si-standard-link: item is not configured"));
    return;
  }

  /* make sure in/out items are valid */
  si_out = g_weak_ref_get (&self->out_item);
  si_in = g_weak_ref_get (&self->in_item);
  if (!si_out || !si_in ||
      !wp_object_test_active_features ((WP_OBJECT (si_out)), WP_SESSION_ITEM_FEATURE_ACTIVE) ||
      !wp_object_test_active_features ((WP_OBJECT (si_in)), WP_SESSION_ITEM_FEATURE_ACTIVE)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
            "some node was destroyed before the link was created"));
    return;
  }

  /* acquire */
  out_acquisition = wp_si_linkable_get_acquisition (WP_SI_LINKABLE (si_out));
  in_acquisition = wp_si_linkable_get_acquisition (WP_SI_LINKABLE (si_in));
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
        WP_SI_LINKABLE (si_out), (GAsyncReadyCallback) on_item_acquired,
        transition);
  }
  if (in_acquisition) {
    wp_si_acquisition_acquire (in_acquisition, WP_SI_LINK (self),
        WP_SI_LINKABLE (si_in), (GAsyncReadyCallback) on_item_acquired,
        transition);
  }
}

static void
si_standard_link_finalize (GObject * object)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (object);

  g_weak_ref_clear (&self->out_item);
  g_weak_ref_clear (&self->in_item);

  G_OBJECT_CLASS (si_standard_link_parent_class)->finalize (object);
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
  si_class->enable_active = si_standard_link_enable_active;

  signals[SIGNAL_LINK_ERROR] = g_signal_new (
      "link-error", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static GVariant *
si_standard_link_get_registration_info (WpSiLink * item)
{
  GVariantBuilder b;
  g_variant_builder_init (&b, G_VARIANT_TYPE ("a{ss}"));
  return g_variant_builder_end (&b);
}

static WpSiLinkable *
si_standard_link_get_out_item (WpSiLink * item)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);
  return WP_SI_LINKABLE (g_weak_ref_get (&self->out_item));
}

static WpSiLinkable *
si_standard_link_get_in_item (WpSiLink * item)
{
  WpSiStandardLink *self = WP_SI_STANDARD_LINK (item);
  return WP_SI_LINKABLE (g_weak_ref_get (&self->in_item));
}

static void
si_standard_link_link_init (WpSiLinkInterface * iface)
{
  iface->get_registration_info = si_standard_link_get_registration_info;
  iface->get_out_item = si_standard_link_get_out_item;
  iface->get_in_item = si_standard_link_get_in_item;
}

WP_PLUGIN_EXPORT GObject *
wireplumber__module_init (WpCore * core, WpSpaJson * args, GError ** error)
{
  return G_OBJECT (wp_si_factory_new_simple (SI_FACTORY_NAME,
      si_standard_link_get_type ()));
}
