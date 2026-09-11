/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <spa/utils/string.h>
#include <pipewire/pipewire.h>

#include "private/stream-link.h"
#include "stream-link-manager.h"
#include "object-manager.h"
#include "log.h"
#include "core.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-stream-link-manager")


/* WpStreamLinkManager */

/*! \defgroup wpstreamlinkmanager WpStreamLinkManager */
/*!
 * \struct WpStreamLinkManager
 *
 * WpStreamLinkManager is a utility class that tracks and manages WpStreamLink
 * objects. It maintains a registry of active stream links and automatically
 * cleans up links when their associated streams are removed.
 *
 * This class provides mechanisms to create, find, and remove stream links,
 * ensuring proper lifecycle management and preventing dangling references.
 * It also provides the ability to enumerate peer streams (streams linked to
 * a given stream) and to find all links associated with a stream.
 *
 * The manager can also be used to discover links by their associated streams
 * using wp_stream_link_manager_find_link(), and to enumerate related streams
 * using wp_stream_link_manager_new_peers_iterator().
 *
 * When activated with the WP_STREAM_LINK_MANAGER_FEATURE_LOADED feature, the
 * stream link manager monitors the PipeWire graph and automatically creates
 * corresponding WpStreamLink objects for any PipeWire links it discovers.
 * It also tracks stream additions and removals, cleaning up obsolete links.
 */

enum {
  PROP_0,
  PROP_NAME,
  N_PROPS,
};

static GParamSpec *properties[N_PROPS] = { NULL, };

struct _WpStreamLinkManager
{
  WpObject parent;

  /* Props */
  gchar *name;

  WpObjectManager *om;
  GPtrArray *stream_links;
  guint pending_stream_links;
};

G_DEFINE_TYPE (WpStreamLinkManager, wp_stream_link_manager, WP_TYPE_OBJECT)

static void
wp_stream_link_manager_init (WpStreamLinkManager * self)
{
  self->stream_links = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
wp_stream_link_manager_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpStreamLinkManager *self = WP_STREAM_LINK_MANAGER (object);

  switch (property_id) {
  case PROP_NAME:
    g_clear_pointer (&self->name, g_free);
    self->name = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_stream_link_manager_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpStreamLinkManager *self = WP_STREAM_LINK_MANAGER (object);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, self->name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

enum {
  STEP_LOAD = WP_TRANSITION_STEP_CUSTOM_START,
};

static WpObjectFeatures
wp_stream_link_manager_get_supported_features (WpObject * self)
{
  return WP_STREAM_LINK_MANAGER_FEATURE_LOADED;
}

static guint
wp_stream_link_manager_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  g_return_val_if_fail (missing == WP_STREAM_LINK_MANAGER_FEATURE_LOADED,
      WP_TRANSITION_STEP_ERROR);

  return STEP_LOAD;
}

static void
evaluate_stream_links_on_stream_removed (WpStreamLinkManager *self,
    WpStream *stream)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (GPtrArray) invalid_links = NULL;

  /* Get the core */
  core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  /* Find all stream links related to the removed stream */
  invalid_links = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < self->stream_links->len; i++) {
    WpStreamLink *link = g_ptr_array_index (self->stream_links, i);
    g_autoptr (WpStream) l_out = wp_stream_link_get_output_stream (link, NULL);
    g_autoptr (WpStream) l_in = wp_stream_link_get_input_stream (link);
    if (l_out == stream || l_in == stream)
      g_ptr_array_add (invalid_links, g_object_ref (link));
  }

  /* Deactivate and remove all related stream links */
  for (guint i = 0; i < invalid_links->len; i++) {
    WpStreamLink *link = g_ptr_array_index (invalid_links, i);
    wp_object_deactivate (WP_OBJECT (link), WP_OBJECT_FEATURES_ALL);
    g_ptr_array_remove_fast (self->stream_links, link);
    wp_core_remove_object (core, link);
  }
}

static gboolean
find_streams_from_link (WpStreamLinkManager *self, WpLink *link,
    WpStream **output_stream, WpStream **input_stream)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  guint32 out_port_id = SPA_ID_INVALID, in_port_id = SPA_ID_INVALID;
  WpStream *output = NULL, *input = NULL;

  wp_link_get_linked_object_ids (link, NULL, &out_port_id, NULL, &in_port_id);
  if (out_port_id == SPA_ID_INVALID || in_port_id == SPA_ID_INVALID)
    return FALSE;

  it = wp_object_manager_new_iterator (self->om);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpObject *o = g_value_get_object (&val);
    if (WP_IS_STREAM (o)) {
      WpStream *stream = WP_STREAM (o);
      if (wp_stream_has_port (stream, in_port_id))
        input = stream;
      if (wp_stream_has_port (stream, out_port_id) ||
          wp_stream_has_monitor_port (stream, out_port_id))
        output = stream;
    }
  }

  if (output_stream && output)
    *output_stream = g_object_ref (output);
  if (input_stream && input)
    *input_stream = g_object_ref (input);

  /* Only return TRUE if both streams have been found */
  return output != NULL && input != NULL;
}

static void
on_stream_link_activated (WpObject * sl, GAsyncResult * res,
    WpStreamLinkManager * self)
{
  g_autoptr (GError) error = NULL;

  self->pending_stream_links--;

  /* Make sure there were no errors when activating the stream */
  if (!wp_object_activate_finish (sl, res, &error)) {
    wp_warning_object (self, "failed to activate stream link %p: %s", sl,
        error->message);
    /* Remove the failed stream link from the manager and the core */
    wp_stream_link_manager_remove_link (self, WP_STREAM_LINK (sl));
  } else {
    wp_info_object (self, "activated stream link %p successfully", sl);
  }

  /* Finish activation if no more pending stream links */
  if (self->pending_stream_links == 0) {
    wp_info_object (self, "finishing activation with stream links...");
    wp_object_update_features (WP_OBJECT (self),
        WP_STREAM_LINK_MANAGER_FEATURE_LOADED, 0);
  }
}

static void
evaluate_stream_link_on_link_added (WpStreamLinkManager *self, WpLink *link)
{
  g_autoptr (WpStream) output = NULL;
  g_autoptr (WpStream) input = NULL;
  g_autoptr (WpStreamLink) new_stream_link = NULL;

  /* Find the streams for this link */
  if (!find_streams_from_link (self, link, &output, &input))
    return;

  /* Don't create new stream link if it already exists */
  new_stream_link = wp_stream_link_manager_find_link (self, output, input);
  if (new_stream_link)
    return;

  /* Create the new stream link */
  new_stream_link = wp_stream_link_manager_create_link (self, output, input);
  if (!new_stream_link) {
    wp_warning_object (self, "could not create stream link for link %p", link);
    return;
  }

  /* Activate the stream link */
  self->pending_stream_links++;
  wp_info_object (self, "activating stream link %p...", new_stream_link);
  wp_object_activate_closure (WP_OBJECT (new_stream_link),
        WP_OBJECT_FEATURES_ALL, NULL, g_cclosure_new_object (
            (GCallback) on_stream_link_activated, G_OBJECT (self)));
}

static void
on_object_added (WpObjectManager *om, WpObject *object, gpointer d)
{
  WpStreamLinkManager * self = WP_STREAM_LINK_MANAGER (d);

  /* If stream was added, no need to do anything
   * If link was added, create associated stream link if it does not exist */
  if (WP_IS_STREAM (object)) {
    /* Nothing */
  } else if (WP_IS_LINK (object)) {
    evaluate_stream_link_on_link_added (self, WP_LINK (object));
  }
}

static void
on_object_removed (WpObjectManager *om, WpObject *object, gpointer d)
{
  WpStreamLinkManager * self = WP_STREAM_LINK_MANAGER (d);

  /* If stream was removed, remove all associated stream links
   * If link was removed, no need to do anything */
  if (WP_IS_STREAM (object)) {
    evaluate_stream_links_on_stream_removed (self, WP_STREAM (object));
  } else if (WP_IS_LINK (object)) {
    /* Nothing to do as the link-removed signal already handles removing
     * of the stream link when creating the stream link */
  }
}

static void
on_om_installed (WpObjectManager *om, gpointer d)
{
  WpTransition *transition = WP_TRANSITION (d);
  WpStreamLinkManager *self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  /* Handle future added and removed objects */
  g_signal_connect_object (self->om, "object-added",
      G_CALLBACK (on_object_added), self, 0);
  g_signal_connect_object (self->om, "object-removed",
      G_CALLBACK (on_object_removed), self, 0);

  /* Check if we need to create stream links */
  it = wp_object_manager_new_iterator (self->om);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpObject *o = g_value_get_object (&val);
    if (WP_IS_LINK (o))
      evaluate_stream_link_on_link_added (self, WP_LINK (o));
  }

  /* Just finish activation now if no pending links */
  if (self->pending_stream_links == 0) {
    wp_info_object (self, "finishing activation without stream links...");
    wp_object_update_features (WP_OBJECT (self),
        WP_STREAM_LINK_MANAGER_FEATURE_LOADED, 0);
  }
}

static void
wp_stream_link_manager_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpStreamLinkManager * self = WP_STREAM_LINK_MANAGER (object);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  switch (step) {
  case STEP_LOAD: {
    g_return_if_fail (self->pending_stream_links == 0);

    g_return_if_fail (self->om == NULL);
    self->om = wp_object_manager_new ();

    wp_object_manager_add_interest (self->om, WP_TYPE_STREAM, NULL);
    wp_object_manager_add_interest (self->om, WP_TYPE_LINK, NULL);
    wp_object_manager_request_object_features (self->om,
        WP_TYPE_STREAM, WP_OBJECT_FEATURES_ALL);
    wp_object_manager_request_object_features (self->om,
        WP_TYPE_LINK, WP_OBJECT_FEATURES_ALL);
    g_signal_connect_object (self->om, "installed",
      G_CALLBACK (on_om_installed), transition, 0);

    wp_core_install_object_manager (core, self->om);
    break;
  }
  case WP_TRANSITION_STEP_ERROR:
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_stream_link_manager_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpStreamLinkManager *self = WP_STREAM_LINK_MANAGER (object);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  /* Deactivate and unregister all stream links from the core */
  if (core) {
    for (guint i = 0; i < self->stream_links->len; i++) {
      WpStreamLink *link = g_ptr_array_index (self->stream_links, i);
      wp_object_deactivate (WP_OBJECT (link), WP_OBJECT_FEATURES_ALL);
      wp_core_remove_object (core, link);
    }
  }

  g_clear_object (&self->om);
  g_ptr_array_set_size (self->stream_links, 0);
  self->pending_stream_links = 0;

  wp_object_update_features (WP_OBJECT (self), 0, WP_OBJECT_FEATURES_ALL);
}

static void
wp_stream_link_manager_finalize (GObject * object)
{
  WpStreamLinkManager * self = WP_STREAM_LINK_MANAGER (object);

  g_clear_pointer (&self->stream_links, g_ptr_array_unref);

  /* Props */
  g_clear_pointer (&self->name, g_free);

  G_OBJECT_CLASS (wp_stream_link_manager_parent_class)->finalize (object);
}

static void
wp_stream_link_manager_class_init (WpStreamLinkManagerClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass * wpobject_class = (WpObjectClass *) klass;

  object_class->finalize = wp_stream_link_manager_finalize;
  object_class->set_property = wp_stream_link_manager_set_property;
  object_class->get_property = wp_stream_link_manager_get_property;

  wpobject_class->get_supported_features =
      wp_stream_link_manager_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_stream_link_manager_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_stream_link_manager_activate_execute_step;
  wpobject_class->deactivate = wp_stream_link_manager_deactivate;

  properties[PROP_NAME] = g_param_spec_string ("name", "name",
      "The name of the stream link manager", NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

/*!
 * \brief Constructs a new stream link manager object
 * \ingroup wpstreamlinkmanager
 * \param core the core associated with the stream link manager
 * \param name the name of stream link manager
 * \returns (transfer full): the new WpStreamLinkManager
 */
WpStreamLinkManager *
wp_stream_link_manager_new (WpCore *core, const char *name)
{
  g_return_val_if_fail (core, NULL);
  return g_object_new (WP_TYPE_STREAM_LINK_MANAGER,
      "core", core,
      "name", name ? name : "default",
      NULL);
}

static gboolean
find_stream_link_manager_func (gpointer g_object, gpointer name)
{
  WpStreamLinkManager *slm;

  if (!WP_IS_STREAM_LINK_MANAGER (g_object))
    return FALSE;

  if (!name)
    return TRUE;

  slm = WP_STREAM_LINK_MANAGER (g_object);
  return g_str_equal (wp_stream_link_manager_get_name (slm), (gchar *) name);
}

/*!
 * \brief Finds a registered WpStreamLinkManager object by name
 *
 * \ingroup wpstreamlinkmanager
 * \param core the WpCore
 * \param name (nullable): the name of stream link manager; NULL returns the
 *    first settings object that is found.
 * \returns (transfer full) (nullable): the WpStreamLinkManager object, or NULL
 *    if not found.
 */
WpStreamLinkManager *
wp_stream_link_manager_find (WpCore * core, const gchar * name)
{
  GObject *slm;

  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  slm = wp_core_find_object (core, (GEqualFunc) find_stream_link_manager_func,
      name);
  return slm ? WP_STREAM_LINK_MANAGER (slm) : NULL;
}

/*!
 * \brief Gets the name of the stream link manager
 * \ingroup wpstreamlinkmanager
 * \param self the stream link manager
 * \returns the name of the stream link manager
 */
const gchar *
wp_stream_link_manager_get_name (WpStreamLinkManager *self)
{
  g_return_val_if_fail (WP_IS_STREAM_LINK_MANAGER (self), NULL);

  return self->name;
}

/*!
 * \brief Finds the WpStreamLink for the given output and input streams.
 *
 * \ingroup wpstreamlinkmanager
 * \param self the stream link manager
 * \param output (transfer none): the output stream
 * \param input (transfer none): the input stream
 * \returns (transfer full) (nullable): the WpStreamLink for the given streams,
 *   or NULL if not found.
 */
WpStreamLink *
wp_stream_link_manager_find_link (WpStreamLinkManager *self, WpStream *output,
    WpStream *input)
{
  g_return_val_if_fail (WP_IS_STREAM_LINK_MANAGER (self), NULL);

  for (guint i = 0; i < self->stream_links->len; i++) {
    WpStreamLink *link = g_ptr_array_index (self->stream_links, i);
    g_autoptr (WpStream) l_out = wp_stream_link_get_output_stream (link, NULL);
    g_autoptr (WpStream) l_in = wp_stream_link_get_input_stream (link);
    if (l_out == output && l_in == input)
      return g_object_ref (link);
  }

  return NULL;
}

static void
on_stream_link_link_removed (WpStreamLink *stream_link, WpLink *link,
    WpStreamLinkManager *self)
{
  /* Remove the stream link if this was the last link removed */
  if (wp_stream_link_get_links_count (stream_link) == 0) {
    g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

    /* Remove */
    wp_object_deactivate (WP_OBJECT (stream_link), WP_OBJECT_FEATURES_ALL);
    g_ptr_array_remove_fast (self->stream_links, stream_link);
    wp_core_remove_object (core, stream_link);
  }
}

/*!
 * \brief Creates and registers a WpStreamLink object to link the given output
 * stream with the given input stream.
 *
 * You need to activate the returned WpStreamLink so that both streams are
 * linked. You can also deactivate the WpStreamLink to unlink both streams.
 *
 * \note The returned WpStreamLink will automatically be deactivated and
 * removed from the manager if one of its streams was destroyed, so users don't
 * really need to do anything with the returned WpStreamLink other than
 * activating it if they want to start processing right away.
 *
 * \ingroup wpstreamlinkmanager
 * \param self the stream link manager
 * \param output (transfer none): the output stream
 * \param input (transfer none): the input stream
 * \returns (transfer full): the WpStreamLink to link the given output stream
 *   with the given input stream.
 */
WpStreamLink *
wp_stream_link_manager_create_link (WpStreamLinkManager *self, WpStream *output,
    WpStream *input)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpStreamLink) link = NULL;

  g_return_val_if_fail (WP_IS_STREAM_LINK_MANAGER (self), NULL);

  /* Return the current link if it already exists for the given streams */
  link = wp_stream_link_manager_find_link (self, output, input);
  if (link)
    return g_steal_pointer (&link);

  /* Get the core */
  core = wp_object_get_core (WP_OBJECT (self));
  g_return_val_if_fail (core, NULL);

  /* Create the link using the matching media type */
  if (WP_IS_AUDIO_STREAM (output) && WP_IS_AUDIO_STREAM (input)) {
    link = WP_STREAM_LINK (wp_audio_stream_link_new (core,
        WP_AUDIO_STREAM (output), WP_AUDIO_STREAM (input)));
  } else if (WP_IS_VIDEO_STREAM (output) && WP_IS_VIDEO_STREAM (input)) {
    link = WP_STREAM_LINK (wp_video_stream_link_new (core,
        WP_VIDEO_STREAM (output), WP_VIDEO_STREAM (input)));
  } else {
    g_return_val_if_reached (NULL);
  }

  /* Handle link-removed signal */
  g_signal_connect_object (link, "link-removed",
      G_CALLBACK (on_stream_link_link_removed), self, 0);

  /* add it to the list */
  g_ptr_array_add (self->stream_links, g_object_ref (link));

  /* Register the link so that it appears on object managers */
  wp_core_register_object (core, g_object_ref (link));

  return g_steal_pointer (&link);
}

/*!
 * \brief Destroys a single WpStreamLink, removing it from the manager and
 * from the core.
 *
 * This is what callers that activate a link themselves must do when the
 * activation fails; a link left in the manager still counts as a peer of both
 * its streams and makes them look linked.
 *
 * \ingroup wpstreamlinkmanager
 * \param self the stream link manager
 * \param link (transfer none): the stream link to destroy
 */
void
wp_stream_link_manager_remove_link (WpStreamLinkManager *self,
    WpStreamLink *link)
{
  g_autoptr (WpCore) core = NULL;

  g_return_if_fail (WP_IS_STREAM_LINK_MANAGER (self));
  g_return_if_fail (WP_IS_STREAM_LINK (link));

  core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  if (!g_ptr_array_remove_fast (self->stream_links, link))
    return;

  wp_core_remove_object (core, link);
}

/*!
 * \brief Destroys all associated WpStreamLink objects to unlink the given
 * stream from all its linked streams.
 *
 * \ingroup wpstreamlinkmanager
 * \param self the stream link manager
 * \param stream (transfer none): the stream to be unlinked
 */
void
wp_stream_link_manager_unlink (WpStreamLinkManager *self, WpStream *stream)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (GPtrArray) stream_links = NULL;

  g_return_if_fail (WP_IS_STREAM_LINK_MANAGER (self));

  /* Get the core */
  core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  /* Find all the peers */
  stream_links = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < self->stream_links->len; i++) {
    WpStreamLink *link = g_ptr_array_index (self->stream_links, i);
    g_autoptr (WpStream) l_out = wp_stream_link_get_output_stream (link, NULL);
    g_autoptr (WpStream) l_in = wp_stream_link_get_input_stream (link);
    if (l_out == stream || l_in == stream)
      g_ptr_array_add (stream_links, g_object_ref (link));
  }

  /* Deactivate and remove all the stream links */
  for (guint i = 0; i < stream_links->len; i++) {
    WpStreamLink *link = g_ptr_array_index (stream_links, i);
    wp_object_deactivate (WP_OBJECT (link), WP_OBJECT_FEATURES_ALL);
    g_ptr_array_remove_fast (self->stream_links, link);
    wp_core_remove_object (core, link);
  }
}

/*!
 * \brief Gets the total number of peer streams the given stream has
 * \ingroup wpstreamlinkmanager
 * \param self the stream link manager
 * \param stream (transfer none): the stream
 * \returns The total number of peer streams the given stream has
 */
guint
wp_stream_link_manager_get_peers_count (WpStreamLinkManager *self,
    WpStream *stream)
{
  g_autoptr (GPtrArray) peers = NULL;

  g_return_val_if_fail (WP_IS_STREAM_LINK_MANAGER (self), 0);

  peers = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < self->stream_links->len; i++) {
    WpStreamLink *link = g_ptr_array_index (self->stream_links, i);
    g_autoptr (WpStream) l_out = wp_stream_link_get_output_stream (link, NULL);
    g_autoptr (WpStream) l_in = wp_stream_link_get_input_stream (link);
    if (l_out == stream)
      g_ptr_array_add (peers, g_object_ref (l_in));
    else if (l_in == stream)
      g_ptr_array_add (peers, g_object_ref (l_out));
  }

  return peers->len;
}

/*!
 * \brief Iterates through all the peer streams the given stream has.
 * \ingroup wpstreamlinkmanager
 * \param self the stream link manager
 * \param stream (transfer none): the stream
 * \returns (transfer full): a WpIterator that iterates over all the peer
 *   streams the given stream has.
 */
WpIterator *
wp_stream_link_manager_new_peers_iterator (WpStreamLinkManager *self,
    WpStream *stream)
{
  g_autoptr (GPtrArray) peers = NULL;

  g_return_val_if_fail (WP_IS_STREAM_LINK_MANAGER (self), NULL);

  peers = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < self->stream_links->len; i++) {
    WpStreamLink *link = g_ptr_array_index (self->stream_links, i);
    g_autoptr (WpStream) l_out = wp_stream_link_get_output_stream (link, NULL);
    g_autoptr (WpStream) l_in = wp_stream_link_get_input_stream (link);
    if (l_out == stream)
      g_ptr_array_add (peers, g_object_ref (l_in));
    else if (l_in == stream)
      g_ptr_array_add (peers, g_object_ref (l_out));
  }

  return wp_iterator_new_ptr_array (g_steal_pointer (&peers), WP_TYPE_STREAM);
}

/*!
 * \brief Iterates through all the stream links the given stream belongs to.
 * \ingroup wpstreamlinkmanager
 * \param self the stream link manager
 * \param stream (transfer none): the stream
 * \returns (transfer full): a WpIterator that iterates over all the stream
 *   links the given stream belongs to.
 */
WpIterator *
wp_stream_link_manager_new_links_iterator (WpStreamLinkManager *self,
    WpStream *stream)
{
  g_autoptr (GPtrArray) links = NULL;

  g_return_val_if_fail (WP_IS_STREAM_LINK_MANAGER (self), NULL);

  links = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < self->stream_links->len; i++) {
    WpStreamLink *link = g_ptr_array_index (self->stream_links, i);
    g_autoptr (WpStream) l_out = wp_stream_link_get_output_stream (link, NULL);
    g_autoptr (WpStream) l_in = wp_stream_link_get_input_stream (link);
    if (l_out == stream || l_in == stream)
      g_ptr_array_add (links, g_object_ref (link));
  }

  return wp_iterator_new_ptr_array (g_steal_pointer (&links),
      WP_TYPE_STREAM_LINK);
}
