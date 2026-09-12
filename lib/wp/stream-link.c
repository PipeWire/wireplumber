/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <errno.h>

#include <spa/param/audio/type-info.h>
#include <spa/utils/string.h>
#include <pipewire/keys.h>

#include "private/stream-link.h"
#include "proxy-interfaces.h"
#include "object-manager.h"
#include "core.h"
#include "error.h"
#include "log.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-stream-link")

/* WpStreamLink */

/*! \defgroup wpstreamlink WpStreamLink */
/*!
 * \struct WpStreamLink
 *
 * WpStreamLink is an abstract base class that manages a set of PipeWire links
 * between an output stream and an input stream. It automatically creates and
 * destroys the individual port-level links as needed based on the port
 * configuration of both streams.
 *
 * This class provides a higher-level abstraction over individual PipeWire links,
 * allowing applications to establish connections between entire streams rather
 * than managing individual ports. It handles the complexity of matching ports
 * with compatible configurations and managing their lifecycle.
 *
 * WpStreamLink is an abstract class with two concrete implementations:
 * - WpAudioStreamLink: links two audio streams with channel-based port matching
 * - WpVideoStreamLink: links two video streams
 *
 * When activated, a stream link will establish all necessary port-level links
 * between the output and input streams. If the streams change their port
 * configuration, the stream link will automatically recover and update the
 * underlying PipeWire links accordingly. When deactivated or destroyed, all
 * associated links are cleaned up.
 *
 * A stream link emits signals when individual PipeWire links are added or removed,
 * allowing applications to monitor the linking status.
 */

enum {
  STREAM_LINK_PROP_0,
  STREAM_LINK_PROP_OUTPUT_STREAM,
  STREAM_LINK_PROP_INPUT_STREAM,
};

enum
{
  STREAM_LINK_SIGNAL_LINK_ADDED,
  STREAM_LINK_SIGNAL_LINK_REMOVED,
  STREAM_LINK_LAST_SIGNAL,
};

static guint stream_link_signals[STREAM_LINK_LAST_SIGNAL] = { 0 };

typedef struct _WpStreamLinkPrivate WpStreamLinkPrivate;
struct _WpStreamLinkPrivate
{
  /* Props */
  GWeakRef output_stream;
  GWeakRef input_stream;
  gboolean is_output_stream_monitor;

  /* Activation info */
  gboolean encoded_only;
  gboolean has_ownership;
  WpObjectManager *links_om;
  GPtrArray *global_links;

  /* Ownership info */
  guint n_pending_managed_links;
  GPtrArray *managed_links;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpStreamLink, wp_stream_link,
    WP_TYPE_OBJECT)

static void
wp_stream_link_init (WpStreamLink * self)
{
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);

  /* Props */
  g_weak_ref_init (&priv->output_stream, NULL);
  g_weak_ref_init (&priv->input_stream, NULL);

  /* Activation info */
  priv->global_links = g_ptr_array_new_with_free_func (g_object_unref);

  /* Ownership info */
  priv->managed_links = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
wp_stream_link_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  WpStreamLink *self = WP_STREAM_LINK (object);
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);

  switch (prop_id) {
    case STREAM_LINK_PROP_OUTPUT_STREAM:
      g_value_take_object (value, g_weak_ref_get (&priv->output_stream));
      break;
    case STREAM_LINK_PROP_INPUT_STREAM:
      g_value_take_object (value, g_weak_ref_get (&priv->input_stream));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
wp_stream_link_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  WpStreamLink *self = WP_STREAM_LINK (object);
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);

  switch (prop_id) {
    case STREAM_LINK_PROP_OUTPUT_STREAM: {
      WpStream *output_stream;

      output_stream = g_value_get_object (value);
      g_weak_ref_set (&priv->output_stream, output_stream);

      /* Mark output stream as monitor if its direction is input */
      if (wp_stream_get_direction (output_stream) == WP_DIRECTION_INPUT)
        priv->is_output_stream_monitor = TRUE;
      else
        priv->is_output_stream_monitor = FALSE;

      break;
    }
    case STREAM_LINK_PROP_INPUT_STREAM:
      g_weak_ref_set (&priv->input_stream, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

enum {
  STEP_ACTIVATE = WP_TRANSITION_STEP_CUSTOM_START,
};

static WpObjectFeatures
wp_stream_link_get_supported_features (WpObject * self)
{
  return WP_STREAM_LINK_FEATURE_ACTIVE;
}

static guint
wp_stream_link_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  g_return_val_if_fail (missing == WP_STREAM_LINK_FEATURE_ACTIVE,
      WP_TRANSITION_STEP_ERROR);

  return STEP_ACTIVATE;
}

static void
on_stream_port_removed (WpStream *stream, WpPort *port, WpStreamLink *self)
{
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);
  g_autoptr (GPtrArray) invalid_links = NULL;
  guint32 port_id;

  invalid_links = g_ptr_array_new_with_free_func (g_object_unref);
  port_id = wp_proxy_get_bound_id (WP_PROXY (port));
  g_return_if_fail (port_id != SPA_ID_INVALID);

  /* Find what links are not valid anymore */
  for (guint i = 0; i < priv->managed_links->len; ) {
    WpLink *link = g_ptr_array_index (priv->managed_links, i);
    guint32 out_port_id = SPA_ID_INVALID, in_port_id = SPA_ID_INVALID;

    /* Link info can already be gone if link-removed was processed first */
    if (!wp_object_test_active_features (WP_OBJECT (link),
            WP_PIPEWIRE_OBJECT_FEATURE_INFO)) {
      g_ptr_array_remove_index_fast (priv->managed_links, i);
      continue;
    }

    wp_link_get_linked_object_ids (link, NULL, &out_port_id, NULL, &in_port_id);
    if (port_id == in_port_id || port_id == out_port_id)
      g_ptr_array_add (invalid_links, g_object_ref (link));

    i++;
  }

  /* Deactivate and remove invalid links from the links arary */
  for (guint i = 0; i < invalid_links->len; i++) {
    WpLink *link = g_ptr_array_index (invalid_links, i);
    wp_object_deactivate (WP_OBJECT (link), WP_OBJECT_FEATURES_ALL);
    g_ptr_array_remove (priv->managed_links, link);
  }
}

struct port
{
  guint32 node_id;
  guint32 port_id;
  guint32 channel_id;
  gboolean visited;
};

static gboolean
channel_is_aux (guint32 channel_id)
{
  return channel_id >= SPA_AUDIO_CHANNEL_START_Aux &&
      channel_id <= SPA_AUDIO_CHANNEL_LAST_Aux;
}

static inline int
score_ports (struct port *out, struct port *in)
{
  int score = 0;

  if (out->channel_id == in->channel_id)
    score += 100;
  else if ((out->channel_id == SPA_AUDIO_CHANNEL_SL && in->channel_id == SPA_AUDIO_CHANNEL_RL) ||
      (out->channel_id == SPA_AUDIO_CHANNEL_RL && in->channel_id == SPA_AUDIO_CHANNEL_SL) ||
      (out->channel_id == SPA_AUDIO_CHANNEL_SR && in->channel_id == SPA_AUDIO_CHANNEL_RR) ||
      (out->channel_id == SPA_AUDIO_CHANNEL_RR && in->channel_id == SPA_AUDIO_CHANNEL_SR))
    score += 60;
  else if ((out->channel_id == SPA_AUDIO_CHANNEL_FC && in->channel_id == SPA_AUDIO_CHANNEL_MONO) ||
      (out->channel_id == SPA_AUDIO_CHANNEL_MONO && in->channel_id == SPA_AUDIO_CHANNEL_FC))
    score += 50;
  else if (in->channel_id == SPA_AUDIO_CHANNEL_UNKNOWN ||
      in->channel_id == SPA_AUDIO_CHANNEL_MONO ||
      out->channel_id == SPA_AUDIO_CHANNEL_UNKNOWN ||
      out->channel_id == SPA_AUDIO_CHANNEL_MONO)
    score += 10;
  else if (channel_is_aux (in->channel_id) != channel_is_aux (out->channel_id))
    score += 7;
  if (score > 0 && !in->visited)
    score += 5;
  if (score <= 10)
    score = 0;

  return score;
}

static void
on_link_activated (WpLink * link, GAsyncResult * res, WpStreamLink * self)
{
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);
  g_autoptr (GError) error = NULL;

  /* Make sure activation was successfull */
  if (!wp_object_activate_finish (WP_OBJECT (link), res, &error)) {
    wp_warning_object (self, "failed to activate link %p: %s", link,
        error->message);
    return;
  }

  wp_info_object (self, "activated link %p successfully", link);
  g_ptr_array_add (priv->managed_links, g_object_ref (link));
}

static void
on_link_activated_transition (WpLink * link, GAsyncResult * res,
    WpTransition * transition)
{
  WpStreamLink *self = wp_transition_get_source_object (transition);
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);
  g_autoptr (GError) error = NULL;

  priv->n_pending_managed_links--;

  /* Make sure activation was successfull */
  if (!wp_object_activate_finish (WP_OBJECT (link), res, &error)) {
    wp_warning_object (self, "failed to activate link %p: %s", link,
        error->message);
  } else {
    wp_info_object (self, "activated link %p successfully (pending %u)", link,
        priv->n_pending_managed_links);
    g_ptr_array_add (priv->managed_links, g_object_ref (link));
  }

  /* Check if we are done activating links */
  if (priv->n_pending_managed_links == 0) {
    if (priv->managed_links->len == 0) {
      wp_transition_return_error (transition,
          g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
          "could not activate any links"));
    } else {
      wp_info_object (self, "created %u links successfully",
          priv->managed_links->len);
      wp_object_update_features (WP_OBJECT (self),
          WP_STREAM_LINK_FEATURE_ACTIVE, 0);
    }
  }
}

/* Aborts an activation step. Every early return on the activation path must go
 * through this: a transition that is neither completed nor failed leaves the
 * caller's callback hanging and this object registered in the core forever.
 * Outside of an activation (transition == NULL) it just warns.
 * Takes ownership of error. */
static void
stream_link_abort (WpStreamLink * self, WpTransition * transition,
    GError * error)
{
  if (transition) {
    wp_transition_return_error (transition, error);
  } else {
    wp_warning_object (self, "%s", error->message);
    g_error_free (error);
  }
}

static void
create_links (WpStreamLink *self, WpTransition * transition)
{
  WpStreamLinkPrivate *priv;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpStream) input_stream = NULL;
  g_autoptr (WpStream) output_stream = NULL;
  guint32 out_node_id, in_node_id;
  guint n_out_ports, n_in_ports;
  g_autoptr (WpIterator) out_ports = NULL;
  g_autoptr (WpIterator) in_ports = NULL;
  g_autoptr (GArray) in_ports_arr = NULL;
  g_auto (GValue) out_item = G_VALUE_INIT;
  g_auto (GValue) in_item = G_VALUE_INIT;
  struct port out_port = { 0, };
  struct port *in_port;
  gint i = 0;
  guint n_created = 0;

  priv = wp_stream_link_get_instance_private (self);

  /* Get core */
  core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  /* Get input/output node and port IDs */
  output_stream = g_weak_ref_get (&priv->output_stream);
  input_stream = g_weak_ref_get (&priv->input_stream);
  if (!output_stream || !input_stream) {
    stream_link_abort (self, transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT,
        "cannot create links without valid streams"));
    return;
  }
  out_node_id = wp_stream_get_node_bound_id (output_stream);
  in_node_id = wp_stream_get_node_bound_id (input_stream);
  if (out_node_id == SPA_ID_INVALID || in_node_id == SPA_ID_INVALID) {
    stream_link_abort (self, transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT,
        "cannot create links with unbound stream nodes: out=%u in=%u",
        out_node_id, in_node_id));
    return;
  }

  /* Make sure both streams have at least 1 port */
  n_out_ports = priv->is_output_stream_monitor ?
      wp_stream_get_monitor_ports_count (output_stream) :
      wp_stream_get_ports_count (output_stream);
  n_in_ports = wp_stream_get_ports_count (input_stream);
  if (n_out_ports == 0 || n_in_ports == 0) {
    stream_link_abort (self, transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT,
        "cannot link streams without ports: n_out_ports=%u n_in_ports=%u",
        n_out_ports, n_in_ports));
    return;
  }

  /* Get input/output ports iterator */
  out_ports = priv->is_output_stream_monitor ?
      wp_stream_new_monitor_ports_iterator (output_stream) :
      wp_stream_new_ports_iterator (output_stream);
  in_ports = wp_stream_new_ports_iterator (input_stream);

  /* Transfer the in ports to an array so we can mark them as already visited */
  in_ports_arr = g_array_sized_new (FALSE, TRUE, sizeof (struct port), n_in_ports + 1);
  g_array_set_size (in_ports_arr, n_in_ports + 1);
  for (; wp_iterator_next (in_ports, &in_item); g_value_unset (&in_item)) {
    WpPort *p = g_value_get_object (&in_item);
    g_autoptr (WpProperties) props = wp_pipewire_object_get_properties (
        WP_PIPEWIRE_OBJECT (p));
    guint32 channel_id = 0;
    const gchar *channel;

    /* Find the audio channel ID */
    channel = wp_properties_get (props, PW_KEY_AUDIO_CHANNEL);
    if (channel) {
      WpSpaIdValue idval = wp_spa_id_value_from_short_name (
          "Spa:Enum:AudioChannel", channel);
      if (idval)
        channel_id = wp_spa_id_value_number (idval);
    }

    in_port = &g_array_index (in_ports_arr, struct port, i++);
    in_port->node_id = in_node_id;
    in_port->port_id = wp_proxy_get_bound_id (WP_PROXY (p));
    in_port->channel_id = channel_id;
    in_port->visited = FALSE;
  }

  /* Now loop over the out ports and figure out where they should be linked */
  priv->n_pending_managed_links = 0;
  for (; wp_iterator_next (out_ports, &out_item); g_value_unset (&out_item)) {
    WpPort *p = g_value_get_object (&out_item);
    g_autoptr (WpProperties) props = wp_pipewire_object_get_properties (
        WP_PIPEWIRE_OBJECT (p));
    g_autoptr (WpProperties) link_props = NULL;
    guint32 channel_id = 0;
    const gchar *channel;
    gint best_score = 0;
    struct port *best_port = NULL;
    g_autoptr (WpLink) link = NULL;

    /* Find the audio channel ID */
    channel = wp_properties_get (props, PW_KEY_AUDIO_CHANNEL);
    if (channel) {
      WpSpaIdValue idval = wp_spa_id_value_from_short_name (
          "Spa:Enum:AudioChannel", channel);
      if (idval)
        channel_id = wp_spa_id_value_number (idval);
    }

    out_port.node_id = out_node_id;
    out_port.port_id = wp_proxy_get_bound_id (WP_PROXY (p));
    out_port.channel_id = channel_id;
    out_port.visited = FALSE;

    for (guint j = 0; j < in_ports_arr->len - 1; j++) {
      in_port = &g_array_index (in_ports_arr, struct port, j);
      gint score = score_ports (&out_port, in_port);
      if (score > best_score) {
        best_score = score;
        best_port = in_port;
      }
    }

    /* Not all output ports have to be linked */
    if (!best_port || best_port->visited)
      continue;

    best_port->visited = TRUE;

    /* Create the link properties */
    link_props = wp_properties_new_empty ();
    wp_properties_setf (link_props, PW_KEY_LINK_OUTPUT_NODE, "%u", out_port.node_id);
    wp_properties_setf (link_props, PW_KEY_LINK_OUTPUT_PORT, "%u", out_port.port_id);
    wp_properties_setf (link_props, PW_KEY_LINK_INPUT_NODE, "%u", best_port->node_id);
    wp_properties_setf (link_props, PW_KEY_LINK_INPUT_PORT, "%u", best_port->port_id);

    /* Create the link */
    link = wp_link_new_from_factory (core, "link-factory",
        wp_properties_ref (link_props));

    /* Activate link, tracking pending activations only during a transition */
    if (transition) {
      priv->n_pending_managed_links++;
      wp_info_object (self, "activating link %p (pending %u)", link,
          priv->n_pending_managed_links);
      wp_object_activate_closure (WP_OBJECT (link),
          WP_OBJECT_FEATURES_ALL, NULL,
          g_cclosure_new_object (
              (GCallback) on_link_activated_transition, G_OBJECT (transition)));
    } else {
      wp_info_object (self, "activating link %p", link);
      wp_object_activate_closure (WP_OBJECT (link),
          WP_OBJECT_FEATURES_ALL, NULL,
          g_cclosure_new_object (
              (GCallback) on_link_activated, G_OBJECT (self)));
    }
    n_created++;
  }

  /* The port matching above is not guaranteed to produce any link. Without one,
   * nothing would ever complete the transition */
  if (n_created == 0) {
    stream_link_abort (self, transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT,
        "could not link any ports between the streams"));
    return;
  }
}

static gboolean
link_belongs_to_stream_link (WpStreamLink *self, WpLink *link)
{
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);
  g_autoptr (WpStream) out_stream = g_weak_ref_get (&priv->output_stream);
  g_autoptr (WpStream) in_stream = g_weak_ref_get (&priv->input_stream);
  guint32 out_port_id = SPA_ID_INVALID, in_port_id = SPA_ID_INVALID;

  if (!out_stream || !in_stream)
    return FALSE;

  if (!wp_object_test_active_features (WP_OBJECT (link),
          WP_PIPEWIRE_OBJECT_FEATURE_INFO))
    return FALSE;

  wp_link_get_linked_object_ids (link, NULL, &out_port_id, NULL, &in_port_id);
  if (out_port_id == SPA_ID_INVALID || in_port_id == SPA_ID_INVALID)
    return FALSE;

  if (!wp_stream_has_port (in_stream, in_port_id))
    return FALSE;

  return (priv->is_output_stream_monitor &&
      wp_stream_has_monitor_port (out_stream, out_port_id)) ||
      (!priv->is_output_stream_monitor &&
      wp_stream_has_port (out_stream, out_port_id));
}

static void
on_link_added (WpObjectManager *om, WpLink *link, WpStreamLink *self)
{
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);

  if (link_belongs_to_stream_link (self, link)) {
    g_ptr_array_add (priv->global_links, g_object_ref (link));
    g_signal_emit (self, stream_link_signals[STREAM_LINK_SIGNAL_LINK_ADDED],
        0, link);
  }
}

static void
on_link_removed (WpObjectManager *om, WpLink *link, WpStreamLink *self)
{
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);

  if (link_belongs_to_stream_link (self, link)) {
    g_ptr_array_remove_fast (priv->global_links, link);
    g_signal_emit (self, stream_link_signals[STREAM_LINK_SIGNAL_LINK_REMOVED],
        0, link);
  }
}

static void
on_links_om_installed (WpObjectManager *om, gpointer d)
{
  WpTransition *transition = WP_TRANSITION (d);
  WpStreamLink *self = wp_transition_get_source_object (transition);
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  g_autoptr (WpStream) output_stream = NULL;
  g_autoptr (WpStream) input_stream = NULL;

  /* Handle future added and removed objects */
  g_signal_connect_object (priv->links_om, "object-added",
      G_CALLBACK (on_link_added), self, 0);
  g_signal_connect_object (priv->links_om, "object-removed",
      G_CALLBACK (on_link_removed), self, 0);

  /* Get the list of links belonging to this stream link if any */
  it = wp_object_manager_new_iterator (priv->links_om);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpLink *link = g_value_get_object (&val);
    if (link_belongs_to_stream_link (self, link))
      g_ptr_array_add (priv->global_links, g_object_ref (link));
  }

  /* Finish activation if global links already exist for this stream. Otherwise
   * create the links ourselves with ownership */
  if (priv->global_links->len > 0) {
    priv->has_ownership = FALSE;
    wp_info_object (self,
        "%u global links exist, finishing activation as not owned",
        priv->global_links->len);
      wp_object_update_features (WP_OBJECT (self),
          WP_STREAM_LINK_FEATURE_ACTIVE, 0);
    return;
  }
  priv->has_ownership = TRUE;

  /* Handle port-removed signals to remove any managed links */
  output_stream = g_weak_ref_get (&priv->output_stream);
  input_stream = g_weak_ref_get (&priv->input_stream);
  if (!output_stream || !input_stream) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "streams disappeared while installing the links object manager"));
    return;
  }
  g_signal_connect_object (output_stream, "port-removed",
      G_CALLBACK (on_stream_port_removed), self, 0);
  g_signal_connect_object (input_stream, "port-removed",
      G_CALLBACK (on_stream_port_removed), self, 0);

  /* If ports need to be configured, let subclass finish the activation.
   * Otherwise just proceed and create the links */
  if (WP_STREAM_LINK_GET_CLASS (self)->setup_ports)
    WP_STREAM_LINK_GET_CLASS (self)->setup_ports (self, transition);
  else
    create_links (self, transition);
}

static void
wp_stream_link_activate (WpStreamLink *self, WpTransition *transition)
{
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpStream) output_stream = NULL;
  g_autoptr (WpStream) input_stream = NULL;
  guint32 out_node_id = SPA_ID_INVALID, in_node_id = SPA_ID_INVALID;
  gboolean encoded_only = FALSE;

  /* Get the core */
  core = wp_object_get_core (WP_OBJECT (self));

  /* Make sure output stream is valid */
  output_stream = g_weak_ref_get (&priv->output_stream);
  if (!output_stream) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "cannot activate link without valid output stream"));
    return;
  }

  /* Make sure input stream is valid */
  input_stream = g_weak_ref_get (&priv->input_stream);
  if (!input_stream) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "cannot activate link without valid input stream"));
    return;
  }

  /* Make sure both streams are not the same stream */
  if (output_stream == input_stream) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "cannot activate link with input and output being the same stream"));
    return;
  }

  /* Make sure both stream are compatible */
  if (!wp_stream_is_compatible (output_stream, input_stream, &encoded_only)) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "cannot activate link with incompatible stream formats"));
    return;
  }

  /* The input stream must always have the direction as input */
  if (wp_stream_get_direction (input_stream) != WP_DIRECTION_INPUT) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "cannot activate link with input stream not having input direction"));
    return;
  }

  /* Mark link as encoded_only if compatibility is only encoded */
  if (encoded_only)
    priv->encoded_only = TRUE;

  /* Get stream node Ids */
  out_node_id = wp_stream_get_node_bound_id (output_stream);
  in_node_id = wp_stream_get_node_bound_id (input_stream);
  if (out_node_id == SPA_ID_INVALID || in_node_id == SPA_ID_INVALID) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "cannot activate link with unbound stream nodes: out=%u in=%u",
        out_node_id, in_node_id));
    return;
  }

  /* Create and install links object manager */
  g_return_if_fail (priv->links_om == NULL);
  priv->links_om = wp_object_manager_new ();
  wp_object_manager_add_interest (priv->links_om, WP_TYPE_LINK,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      PW_KEY_LINK_OUTPUT_NODE, "=u", out_node_id,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY,
      PW_KEY_LINK_INPUT_NODE, "=u", in_node_id,
      NULL);
  wp_object_manager_request_object_features (priv->links_om,
      WP_TYPE_LINK, WP_OBJECT_FEATURES_ALL);
  g_signal_connect_object (priv->links_om, "installed",
      G_CALLBACK (on_links_om_installed), transition, 0);
  wp_core_install_object_manager (core, priv->links_om);
}

/* Undoes everything the activation set up */
static void
stream_link_teardown (WpStreamLink * self)
{
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);
  g_autoptr (WpStream) output_stream = NULL;
  g_autoptr (WpStream) input_stream = NULL;

  /* Teardown ports configuration if needed */
  if (WP_STREAM_LINK_GET_CLASS (self)->teardown_ports)
    WP_STREAM_LINK_GET_CLASS (self)->teardown_ports (self);

  /* Ownership info */
  output_stream = g_weak_ref_get (&priv->output_stream);
  if (output_stream)
    g_signal_handlers_disconnect_by_data (output_stream, self);
  input_stream = g_weak_ref_get (&priv->input_stream);
  if (input_stream)
    g_signal_handlers_disconnect_by_data (input_stream, self);
  priv->n_pending_managed_links = 0;
  g_ptr_array_set_size (priv->managed_links, 0);

  /* Activation info */
  priv->encoded_only = FALSE;
  priv->has_ownership = FALSE;
  g_clear_object (&priv->links_om);
  g_ptr_array_set_size (priv->global_links, 0);
}

static void
wp_stream_link_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * trans, guint step,
    WpObjectFeatures missing)
{
  WpStreamLink *self = WP_STREAM_LINK (object);

  switch (step) {
  case STEP_ACTIVATE: {
    wp_stream_link_activate (self, WP_TRANSITION (trans));
    break;
  }
  case WP_TRANSITION_STEP_ERROR:
    /* The activation installs the links object manager and creates links
     * before it can fail, so roll all of that back here */
    stream_link_teardown (self);
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_stream_link_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpStreamLink *self = WP_STREAM_LINK (object);
  guint current = wp_object_get_active_features (object);

  if (features & current & WP_STREAM_LINK_FEATURE_ACTIVE)
    stream_link_teardown (self);

  wp_object_update_features (WP_OBJECT (self), 0, WP_OBJECT_FEATURES_ALL);
}

static void
wp_stream_link_finalize (GObject * object)
{
  WpStreamLink *self = WP_STREAM_LINK (object);
  WpStreamLinkPrivate *priv = wp_stream_link_get_instance_private (self);

  /* Ownership info */
  g_clear_pointer (&priv->managed_links, g_ptr_array_unref);

  /* Activation info */
  g_clear_pointer (&priv->global_links, g_ptr_array_unref);

  /* Props */
  g_weak_ref_clear (&priv->output_stream);
  g_weak_ref_clear (&priv->input_stream);

  G_OBJECT_CLASS (wp_stream_link_parent_class)->finalize (object);
}

static void
wp_stream_link_class_init (WpStreamLinkClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass * wpobject_class = (WpObjectClass *) klass;

  object_class->finalize = wp_stream_link_finalize;
  object_class->set_property = wp_stream_link_set_property;
  object_class->get_property = wp_stream_link_get_property;

  wpobject_class->get_supported_features =
      wp_stream_link_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_stream_link_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_stream_link_activate_execute_step;
  wpobject_class->deactivate = wp_stream_link_deactivate;

  g_object_class_install_property (object_class, STREAM_LINK_PROP_OUTPUT_STREAM,
      g_param_spec_object ("output-stream", "output-stream", "The output stream",
          WP_TYPE_STREAM,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, STREAM_LINK_PROP_INPUT_STREAM,
      g_param_spec_object ("input-stream", "input-stream", "The input stream",
          WP_TYPE_STREAM,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  stream_link_signals[STREAM_LINK_SIGNAL_LINK_ADDED] = g_signal_new (
      "link-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpStreamLinkClass, link_added), NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_LINK);

  stream_link_signals[STREAM_LINK_SIGNAL_LINK_REMOVED] = g_signal_new (
      "link-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpStreamLinkClass, link_removed), NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_LINK);
}

/*!
 * \brief Gets the output stream of the stream link
 * \ingroup wpstreamlink
 * \param self the stream link
 * \param is_monitor (out) (optional): if the output stream is a monitor or not
 * \returns (transfer full): the output stream
 */
WpStream *
wp_stream_link_get_output_stream (WpStreamLink *self, gboolean *is_monitor)
{
  WpStreamLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_STREAM_LINK (self), NULL);

  priv = wp_stream_link_get_instance_private (self);

  if (is_monitor)
    *is_monitor = priv->is_output_stream_monitor;

  return g_weak_ref_get (&priv->output_stream);
}

/*!
 * \brief Gets the input stream of the stream link
 * \ingroup wpstreamlink
 * \param self the stream link
 * \returns (transfer full): the input stream
 */
WpStream *
wp_stream_link_get_input_stream (WpStreamLink *self)
{
  WpStreamLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_STREAM_LINK (self), NULL);

  priv = wp_stream_link_get_instance_private (self);

  return g_weak_ref_get (&priv->input_stream);
}

/*!
 * \brief Gets the total number of pipewire links
 * \ingroup wpstreamlink
 * \param self the stream link
 * \returns the total number of pipewire links
 */
guint
wp_stream_link_get_links_count (WpStreamLink *self)
{
  WpStreamLinkPrivate *priv;

  g_return_val_if_fail (WP_IS_STREAM_LINK (self), 0);

  priv = wp_stream_link_get_instance_private (self);

  return priv->has_ownership ?
      priv->managed_links->len : priv->global_links->len;
}

/*!
 * \brief Iterates through all the pipewire link objects that link both streams.
 * \ingroup wpstreamlink
 * \param self the stream link
 * \returns (transfer full): a WpIterator that iterates over all the pipewire
 *   link objects that link both streams.
 */
WpIterator *
wp_stream_link_new_links_iterator (WpStreamLink *self)
{
  WpStreamLinkPrivate *priv;
  g_autoptr (GPtrArray) links = NULL;

  g_return_val_if_fail (WP_IS_STREAM_LINK (self), NULL);

  priv = wp_stream_link_get_instance_private (self);

  links = g_ptr_array_copy (
      priv->has_ownership ? priv->managed_links : priv->global_links,
      (GCopyFunc)g_object_ref, NULL);
  return wp_iterator_new_ptr_array (g_steal_pointer (&links), WP_TYPE_LINK);
}


/* Audio Stream Link */

struct _WpAudioStreamLink
{
  WpStreamLink parent;

  GWeakRef main_stream;
  GWeakRef other_stream;
  WpTransition *pending_transition;
};

G_DEFINE_TYPE (WpAudioStreamLink, wp_audio_stream_link, WP_TYPE_STREAM_LINK);

static void
wp_audio_stream_link_init (WpAudioStreamLink * self)
{
  g_weak_ref_init (&self->main_stream, NULL);
  g_weak_ref_init (&self->other_stream, NULL);
}

static void
reconfigure_stream_link (WpAudioStreamLink *self,
    WpAudioStream *configured_stream)
{
  WpStreamLinkPrivate *priv;
  g_autoptr (WpAudioStream) other = NULL;

  priv = wp_stream_link_get_instance_private (WP_STREAM_LINK (self));

  /* Always remove links on reconfiguration */
  while (priv->managed_links->len > 0) {
    guint index = priv->managed_links->len - 1;
    WpLink *link = g_ptr_array_index (priv->managed_links, index);

    wp_object_deactivate (WP_OBJECT (link), WP_OBJECT_FEATURES_ALL);
    g_ptr_array_remove_index_fast (priv->managed_links, index);
  }
  wp_info_object (self, "cleared managed links on reconfiguration");

  /* Get the stream that was not reconfigured */
  other = WP_AUDIO_STREAM (g_weak_ref_get (
      &self->other_stream));
  if (other == configured_stream)
    other = WP_AUDIO_STREAM (g_weak_ref_get (&self->main_stream));
  if (!other)
    return;
  wp_info_object (self, "configuring ports on stream %p", other);

  /* Configure other with same ports as the reconfigured stream. If no
   * reconfiguration is needed, recreate the links right away */
  if (!wp_audio_stream_configure_ports_with_stream (other, configured_stream)) {
    wp_info_object (self, "creating links");
    create_links (WP_STREAM_LINK (self), NULL);
  }
}

static void
on_main_ports_configured (WpAudioStream *stream, WpAudioStreamLink *self)
{
  WpStreamLinkPrivate *priv;
  g_autoptr (WpTransition) transition = NULL;

  priv = wp_stream_link_get_instance_private (WP_STREAM_LINK (self));

  wp_info_object (self, "ports configured on main stream");

  /* Configure other stream if pending transition */
  transition = g_steal_pointer (&self->pending_transition);
  if (transition) {
    g_autoptr (WpAudioStream) other = NULL;

    other = WP_AUDIO_STREAM (g_weak_ref_get (&self->other_stream));
    if (!other) {
      wp_transition_return_error (transition,
          g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
          "other stream not valid after configuring main stream"));
      return;
    }

    if (!wp_audio_stream_configure_ports_with_stream (other, stream)) {
      create_links (WP_STREAM_LINK (self), transition);
      return;
    }

    g_return_if_fail (!self->pending_transition);
    self->pending_transition = g_object_ref (transition);
  } else {
    /* Otherwise reconfigure stream link if we have ownership.
     * This happens when the link is activated and we configured
     * the ports externally (Eg when configuring mono audio) */
    if (priv->has_ownership)
      reconfigure_stream_link (self, stream);
  }
}

static void
on_other_ports_configured (WpAudioStream *stream, WpAudioStreamLink *self)
{
  WpStreamLinkPrivate *priv;
  g_autoptr (WpTransition) transition = NULL;

  priv = wp_stream_link_get_instance_private (WP_STREAM_LINK (self));

  wp_info_object (self, "ports configured on other stream");

  /* Create links if pending transition */
  transition = g_steal_pointer (&self->pending_transition);
  if (transition) {
    create_links (WP_STREAM_LINK (self), transition);
  } else {
    /* Otherwise reconfigure stream link if we have ownership.
     * This happens when the link is activated and we configured
     * the ports externally (Eg when configuring mono audio) */
    if (priv->has_ownership)
      reconfigure_stream_link (self, stream);
  }
}

static void
on_main_ports_error (WpAudioStream *stream, const gchar *message,
    WpAudioStreamLink *self)
{
  g_autoptr (WpTransition) transition = NULL;

  /* Abort activation if any */
  transition = g_steal_pointer (&self->pending_transition);
  if (transition)
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "failed to configure ports on main stream: %s", message));
  else
    wp_warning_object (self, "failed to configure ports on main stream: %s",
        message);
}

static void
on_other_ports_error (WpAudioStream *stream, const gchar *message,
    WpAudioStreamLink *self)
{
  g_autoptr (WpTransition) transition = NULL;

  /* Abort activation if any */
  transition = g_steal_pointer (&self->pending_transition);
  if (transition)
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "failed to configure ports on other stream: %s", message));
  else
    wp_warning_object (self, "failed to configure ports on other stream: %s",
        message);
}

static void
wp_audio_stream_link_setup_ports (WpStreamLink *link, WpTransition *transition)
{
  WpAudioStreamLink *self = WP_AUDIO_STREAM_LINK (link);
  WpStreamLinkPrivate *priv;
  g_autoptr (WpStream) output = NULL;
  g_autoptr (WpStream) input = NULL;
  WpAudioStream *main, *other;
  gboolean out_is_target, in_is_target;
  gboolean pending_conf = FALSE;
  g_autoptr (WpProperties) conf_props = NULL;

  priv = wp_stream_link_get_instance_private (WP_STREAM_LINK (self));

  /* Get audio streams */
  output = wp_stream_link_get_output_stream (link, NULL);
  input = wp_stream_link_get_input_stream (link);
  if (!output || !input) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "cannot setup ports without valid streams"));
    return;
  }

  /* We always consider output stream as main, unless input stream is a target */
  out_is_target = wp_stream_get_kind (output) == WP_STREAM_KIND_TARGET;
  in_is_target = wp_stream_get_kind (input) == WP_STREAM_KIND_TARGET;
  if (!out_is_target && in_is_target) {
    main = WP_AUDIO_STREAM (input);
    other = WP_AUDIO_STREAM (output);
  } else {
    main = WP_AUDIO_STREAM (output);
    other = WP_AUDIO_STREAM (input);
  }
  g_weak_ref_set (&self->main_stream, main);
  g_weak_ref_set (&self->other_stream, other);

  /* Handle audio stream signals */
  g_signal_connect_object (other, "ports-configured",
      G_CALLBACK (on_other_ports_configured), self, 0);
  g_signal_connect_object (main, "ports-configured",
      G_CALLBACK (on_main_ports_configured), self, 0);
  g_signal_connect_object (other, "ports-error",
      G_CALLBACK (on_other_ports_error), self, 0);
  g_signal_connect_object (main, "ports-error",
      G_CALLBACK (on_main_ports_error), self, 0);

  /* Configure streams with encoded only if needed */
  wp_info_object (self, "configuring stream ports: encoded_only:%d",
      priv->encoded_only);
  conf_props = wp_properties_new_empty ();
  if (priv->encoded_only)
    wp_properties_set (conf_props, "stream.encoded-only", "true");

  /* Prioritize main's default configuration over other's configuration.
   * Also, always reconfigure the main stream whenever its current exclusive
   * state doesn't match what this stream link needs. This is because the main
   * stream could have been previously configured by another stream link that
   * is not present anymore. */
  if (!wp_audio_stream_has_ports_configured (main) ||
      wp_stream_is_exclusive (WP_STREAM (main)) != priv->encoded_only)
    pending_conf |= wp_audio_stream_configure_ports_with_props (main,
        g_steal_pointer (&conf_props));
  if (!pending_conf) {
    pending_conf |= wp_audio_stream_configure_ports_with_stream (other, main);
    if (!pending_conf) {
      create_links (link, transition);
      return;
    }
  }

  /* Set pending transition if configuration is pending.
   * Activation will finish when the ports-configured signal is triggered */
  g_return_if_fail (!self->pending_transition);
  self->pending_transition = g_object_ref (transition);
}

static void
wp_audio_stream_link_teardown_ports (WpStreamLink *link)
{
  WpAudioStreamLink *self = WP_AUDIO_STREAM_LINK (link);
  g_autoptr (WpAudioStream) output = NULL;
  g_autoptr (WpAudioStream) input = NULL;

  /* Drop the transition we were going to finish from ports-configured */
  g_clear_object (&self->pending_transition);

  /* Disconnect audio stream signals */
  output = WP_AUDIO_STREAM (wp_stream_link_get_output_stream (link, NULL));
  if (output)
    g_signal_handlers_disconnect_by_data (output, self);
  input = WP_AUDIO_STREAM (wp_stream_link_get_input_stream (link));
  if (input)
    g_signal_handlers_disconnect_by_data (input, self);
}

static void
wp_audio_stream_link_finalize (GObject * object)
{
  WpAudioStreamLink *self = WP_AUDIO_STREAM_LINK (object);

  g_weak_ref_clear (&self->main_stream);
  g_weak_ref_clear (&self->other_stream);
  g_clear_object (&self->pending_transition);

  G_OBJECT_CLASS (wp_audio_stream_link_parent_class)->finalize (object);
}

static void
wp_audio_stream_link_class_init (WpAudioStreamLinkClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpStreamLinkClass *stream_link_class = (WpStreamLinkClass *) klass;

  object_class->finalize = wp_audio_stream_link_finalize;

  stream_link_class->setup_ports = wp_audio_stream_link_setup_ports;
  stream_link_class->teardown_ports = wp_audio_stream_link_teardown_ports;
}

WpAudioStreamLink *
wp_audio_stream_link_new (WpCore *core, WpAudioStream *output,
    WpAudioStream *input)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);
  g_return_val_if_fail (WP_IS_AUDIO_STREAM (output), NULL);
  g_return_val_if_fail (WP_IS_AUDIO_STREAM (input), NULL);
  g_return_val_if_fail (output != input, NULL);

  return g_object_new (WP_TYPE_AUDIO_STREAM_LINK,
      "core", core,
      "output-stream", output,
      "input-stream", input,
      NULL);
}


/* Video Stream Link */

struct _WpVideoStreamLink
{
  WpStreamLink parent;
};

G_DEFINE_TYPE (WpVideoStreamLink, wp_video_stream_link, WP_TYPE_STREAM_LINK);

static void
wp_video_stream_link_init (WpVideoStreamLink * self)
{
}

static void
wp_video_stream_link_class_init (WpVideoStreamLinkClass * klass)
{
}

WpVideoStreamLink *
wp_video_stream_link_new (WpCore *core, WpVideoStream *output,
  WpVideoStream *input)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);
  g_return_val_if_fail (WP_IS_VIDEO_STREAM (output), NULL);
  g_return_val_if_fail (WP_IS_VIDEO_STREAM (input), NULL);
  g_return_val_if_fail (output != input, NULL);

  return g_object_new (WP_TYPE_VIDEO_STREAM_LINK,
    "core", core,
    "output-stream", output,
    "input-stream", input,
    NULL);
}
