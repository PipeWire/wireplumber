/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>

#include "private/stream.h"
#include "stream-manager.h"
#include "object-manager.h"
#include "proxy-interfaces.h"
#include "log.h"
#include "core.h"
#include "node.h"
#include "settings.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-stream-manager")


/* WpStreamManager */

/*! \defgroup wpstreammanager WpStreamManager */
/*!
 * \struct WpStreamManager
 *
 * WpStreamManager is a utility class that automatically creates and manages
 * WpStream objects for all nodes in the PipeWire graph. It monitors node
 * additions and removals, creating appropriate stream objects (audio or video)
 * based on the node's media class, and unregistering them when nodes disappear.
 *
 * This class simplifies stream management by handling the tedious work of
 * tracking which nodes have streams and maintaining that state as the PipeWire
 * graph changes. Without this manager, applications would need to manually
 * listen for node changes and create streams themselves.
 *
 * The stream manager discovers each node's port configuration by enumerating
 * ports and their parameters, then creates the appropriate stream type
 * (WpAudioStream or WpVideoStream) and registers it with the WpCore for
 * other components to discover and use.
 *
 * When activated with the WP_STREAM_MANAGER_FEATURE_LOADED feature, the
 * stream manager enters its operational state where it automatically handles
 * node monitoring and stream creation/destruction.
 */

struct _WpStreamManager
{
  WpObject parent;

  WpObjectManager *nodes_om;
  guint pending_enum_port_config;
  guint pending_streams;
  GHashTable *node_streams;
};

G_DEFINE_TYPE (WpStreamManager, wp_stream_manager, WP_TYPE_OBJECT)

static void
wp_stream_manager_init (WpStreamManager * self)
{
  self->node_streams = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify)g_ptr_array_unref);
}

enum {
  STEP_LOAD = WP_TRANSITION_STEP_CUSTOM_START,
};

static WpObjectFeatures
wp_stream_manager_get_supported_features (WpObject * self)
{
  return WP_STREAM_MANAGER_FEATURE_LOADED;
}

static guint
wp_stream_manager_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  g_return_val_if_fail (missing == WP_STREAM_MANAGER_FEATURE_LOADED,
      WP_TRANSITION_STEP_ERROR);

  return STEP_LOAD;
}

static void
ensure_media_role (WpStreamManager * self, WpProperties * props)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpSettings) settings = NULL;
  g_autoptr (WpSpaJson) val = NULL;
  g_autofree gchar *role = NULL;

  if (wp_properties_get (props, PW_KEY_MEDIA_ROLE))
    return;

  core = wp_object_get_core (WP_OBJECT (self));
  settings = wp_settings_find (core, NULL);
  if (!settings)
    return;

  val = wp_settings_get (settings, "node.stream.default-media-role");
  if (!val || wp_spa_json_is_null (val))
    return;

  role = wp_spa_json_parse_string (val);
  if (role)
    wp_properties_set (props, PW_KEY_MEDIA_ROLE, role);
}

static GPtrArray *
get_node_streams (WpStreamManager * self, WpNode *node)
{
  GPtrArray *streams;

  streams = g_hash_table_lookup (self->node_streams, node);
  if (!streams) {
    streams = g_ptr_array_new_with_free_func (g_object_unref);
    g_hash_table_insert (self->node_streams, node, streams);
  }

  return streams;
}

static void
remove_all_node_streams (WpStreamManager * self, WpNode *node)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  GPtrArray *streams;

  /* Unregister all streams if any */
  streams = g_hash_table_lookup (self->node_streams, node);
  if (streams) {
    for (guint i = 0; i < streams->len; i++) {
      WpStream *s = g_ptr_array_index (streams, i);
      wp_core_remove_object (core, s);
    }
  }

  /* Remove all streams */
  g_hash_table_remove (self->node_streams, node);
}

/* Completes the activation transition once nothing is pending anymore. Every
 * path that decrements one of the counters must end up here, or the transition
 * is never completed and the manager never becomes LOADED. */
static void
check_activation_done (WpStreamManager * self, const gchar * reason)
{
  if (self->pending_enum_port_config == 0 && self->pending_streams == 0) {
    wp_info_object (self, "finishing activation %s...", reason);
    wp_object_update_features (WP_OBJECT (self),
        WP_STREAM_MANAGER_FEATURE_LOADED, 0);
  }
}

static void
on_stream_activated (WpObject * s, GAsyncResult * res, WpStreamManager * self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (WpCore) core = NULL;
  WpStream *stream = WP_STREAM (s);
  g_autoptr (WpNode) node = NULL;
  GPtrArray *streams;

  self->pending_streams--;

  /* Make sure there were no errors when activating the stream */
  if (!wp_object_activate_finish (s, res, &error)) {
    wp_warning_object (self, "failed to activate stream %p: %s", s,
        error->message);
  } else {
    wp_info_object (self, "activated stream %p successfully", s);

    /* The node can have been removed while the stream was activating; there
     * is nothing to register in that case, but the transition still needs to
     * be completed below */
    core = wp_object_get_core (WP_OBJECT (self));
    g_return_if_fail (core);

    node = wp_stream_get_node (stream);
    if (!node) {
      wp_info_object (self, "not registering stream %p: node is gone", s);
    } else {
      /* Add it to the node streams list */
      streams = get_node_streams (self, node);
      g_ptr_array_add (streams, g_object_ref (stream));

      /* Register the stream */
      wp_core_register_object (core, g_object_ref (stream));
    }
  }

  /* Finish activation if no pending enum port config and no pending streams */
  check_activation_done (self, "with streams");
}

static void
on_audio_node_enum_port_config_done (WpPipewireObject *po, GAsyncResult *res,
    WpStreamManager *self)
{
  WpNode *node = WP_NODE (po);
  g_autoptr (WpProperties) props = NULL;
  guint32 bound_id;
  g_autoptr (GError) err = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  g_autoptr (GHashTable) seen = g_hash_table_new_full (g_str_hash,
      g_str_equal, g_free, NULL);

  self->pending_enum_port_config--;

  /* Get properties and bound-id */
  props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (node));
  bound_id = wp_proxy_get_bound_id (WP_PROXY (node));

  it = wp_pipewire_object_enum_params_finish (po, res, &err);
  if (!it) {
    wp_warning_object (self, "failed to enum EnumPortConfig on node %u: %s",
        bound_id, err->message);
    check_activation_done (self, "after enum error");
    return;
  }

  /* Remove old streams */
  remove_all_node_streams (self, node);

  /* Create new streams */
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSpaPod *param = g_value_get_boxed (&val);
    enum spa_direction dir = SPA_DIRECTION_INPUT;
    const gchar *port_group = NULL;
    gchar *stream_key;
    g_autoptr (WpProperties) stream_props = NULL;
    g_autoptr (WpAudioStream) stream = NULL;

    /* Parse mandatory fields */
    if (!wp_spa_pod_is_object (param) ||
        !wp_spa_pod_get_object (param, NULL,
            "direction", "I", &dir,
            NULL)) {
      wp_warning_object (self, "unknown direction from EnumPortConfig param %p",
          param);
      continue;
    }

    /* Get optional fields */
    wp_spa_pod_get_object (param, NULL,
        /* the raw id of the PortConfig "group" key, which is not in a
           released pipewire yet, see spa/param/port-config.h */
        "id-00000006", "s", &port_group,
        NULL);

    /* A port group with several formats has one EnumPortConfig per format,
     * but it is still a single stream */
    stream_key = g_strdup_printf ("%u:%s", dir, port_group ? port_group : "");
    if (!g_hash_table_add (seen, stream_key))
      continue;

    /* Create the audio stream with its own properties */
    stream_props = wp_properties_copy (props);
    ensure_media_role (self, stream_props);
    stream = wp_audio_stream_new (node, g_steal_pointer (&stream_props),
        (WpDirection)dir, port_group);

    /* Activate the audio stream */
    self->pending_streams++;
    wp_info_object (self, "activating stream %p...", stream);
    wp_object_activate_closure (WP_OBJECT (stream),
        WP_OBJECT_FEATURES_ALL, NULL, g_cclosure_new_object (
            (GCallback) on_stream_activated, G_OBJECT (self)));

    /* Make sure we dont have more than 1 stream per node without port group */
    if (!port_group)
      break;
  }

  /* EnumPortConfig can be empty, in which case no stream was activated above
   * and nothing else would ever complete the transition */
  check_activation_done (self, "with empty port config");
}

static void
on_audio_node_params_changed (WpNode *node, const gchar *id,
    WpStreamManager *self)
{
  /* Check if EnumPortConfig has changed */
  if (g_str_equal (id, "EnumPortConfig")) {
    self->pending_enum_port_config++;
    wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node), "EnumPortConfig",
        NULL, NULL, (GAsyncReadyCallback) on_audio_node_enum_port_config_done,
        G_OBJECT (self));
  }
}

static void
manage_audio_node_streams (WpStreamManager *self, WpNode *node)
{
  /* Handle params-changed signal */
  g_signal_connect_object (node, "params-changed",
        G_CALLBACK (on_audio_node_params_changed), self, 0);

  /* Get EnumPortConfig param to create all available audio streams */
  self->pending_enum_port_config++;
  wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node), "EnumPortConfig",
      NULL, NULL, (GAsyncReadyCallback) on_audio_node_enum_port_config_done,
      G_OBJECT (self));
}

static void
manage_video_node_streams (WpStreamManager *self, WpNode *node)
{
  g_autoptr (WpProperties) props = NULL;
  const gchar *media_class = NULL;
  WpDirection dir = WP_DIRECTION_INPUT;
  g_autoptr (WpProperties) stream_props = NULL;
  g_autoptr (WpVideoStream) stream = NULL;

  /* Get node properties */
  props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (node));
  media_class = wp_properties_get (props, PW_KEY_MEDIA_CLASS);
  g_return_if_fail (media_class);

  /* Get direction */
  if (strstr (media_class, "Source") || strstr (media_class, "Output"))
    dir = WP_DIRECTION_OUTPUT;

  /* Create the video stream with its own properties */
  stream_props = wp_properties_copy (props);
  ensure_media_role (self, stream_props);
  stream = wp_video_stream_new (node, g_steal_pointer (&stream_props), dir,
      NULL);

  /* Activate the video stream */
  self->pending_streams++;
  wp_info_object (self, "activating stream %p...", stream);
  wp_object_activate_closure (WP_OBJECT (stream),
      WP_OBJECT_FEATURES_ALL, NULL, g_cclosure_new_object (
          (GCallback) on_stream_activated, G_OBJECT (self)));
}

static void
create_all_node_streams (WpStreamManager *self, WpNode *node)
{
  g_autoptr (WpProperties) props = NULL;
  const gchar *media_class;
  guint32 bound_id;

  /* Get the media class and bound-id */
  props = wp_global_proxy_get_global_properties (WP_GLOBAL_PROXY (node));
  g_return_if_fail (props);
  media_class = wp_properties_get (props, PW_KEY_MEDIA_CLASS);
  bound_id = wp_proxy_get_bound_id (WP_PROXY (node));
  if (!media_class) {
    wp_info_object (self, "no streams for node without media class: %u",
        bound_id);
    return;
  }

  /* Manage streams */
  if (g_strstr_len (media_class, -1, "Audio")) {
    manage_audio_node_streams (self, node);
    return;
  } else if (g_strstr_len (media_class, -1, "Video")) {
    manage_video_node_streams (self, node);
    return;
  }

  wp_info_object (self, "unknown media class '%s' for node %u", media_class,
        bound_id);
}

static void
on_node_added (WpObjectManager *om, WpNode *node, gpointer d)
{
  WpStreamManager * self = WP_STREAM_MANAGER (d);
  create_all_node_streams (self, node);
}

static void
on_node_removed (WpObjectManager *om, WpNode *node, gpointer d)
{
  WpStreamManager * self = WP_STREAM_MANAGER (d);
  remove_all_node_streams (self, node);
}

static void
on_nodes_om_installed (WpObjectManager *om, gpointer d)
{
  WpTransition *transition = WP_TRANSITION (d);
  WpStreamManager *self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  /* Handle future added and removed objects */
  g_signal_connect_object (self->nodes_om, "object-added",
      G_CALLBACK (on_node_added), self, 0);
  g_signal_connect_object (self->nodes_om, "object-removed",
      G_CALLBACK (on_node_removed), self, 0);

  /* Create streams for all current nodes */
  it = wp_object_manager_new_iterator (self->nodes_om);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpNode *node = g_value_get_object (&val);
    create_all_node_streams (self, node);
  }

  /* Just finish activation now if no pending enums or streams */
  check_activation_done (self, "without streams");
}

static void
wp_stream_manager_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpStreamManager * self = WP_STREAM_MANAGER (object);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  switch (step) {
  case STEP_LOAD: {
    g_return_if_fail (self->pending_enum_port_config == 0);
    g_return_if_fail (self->pending_streams == 0);

    g_return_if_fail (self->nodes_om == NULL);
    self->nodes_om = wp_object_manager_new ();

    wp_object_manager_add_interest (self->nodes_om, WP_TYPE_NODE, NULL);
    wp_object_manager_request_object_features (self->nodes_om,
        WP_TYPE_NODE, WP_OBJECT_FEATURES_ALL);
    g_signal_connect_object (self->nodes_om, "installed",
        G_CALLBACK (on_nodes_om_installed), transition, 0);

    wp_core_install_object_manager (core, self->nodes_om);
    break;
  }
  case WP_TRANSITION_STEP_ERROR:
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_stream_manager_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpStreamManager *self = WP_STREAM_MANAGER (object);

  /* Disconnect params-changed from all managed nodes before clearing the OM */
  if (self->nodes_om) {
    g_autoptr (WpIterator) it = wp_object_manager_new_iterator (self->nodes_om);
    g_auto (GValue) val = G_VALUE_INIT;
    for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      WpNode *node = g_value_get_object (&val);
      g_signal_handlers_disconnect_by_data (node, self);
    }
  }

  g_hash_table_remove_all (self->node_streams);
  g_clear_object (&self->nodes_om);

  self->pending_enum_port_config = 0;
  self->pending_streams = 0;

  wp_object_update_features (WP_OBJECT (self), 0, WP_OBJECT_FEATURES_ALL);
}

static void
wp_stream_manager_finalize (GObject * object)
{
  WpStreamManager * self = WP_STREAM_MANAGER (object);

  g_clear_pointer (&self->node_streams, g_hash_table_unref);

  G_OBJECT_CLASS (wp_stream_manager_parent_class)->finalize (object);
}

static void
wp_stream_manager_class_init (WpStreamManagerClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass * wpobject_class = (WpObjectClass *) klass;

  object_class->finalize = wp_stream_manager_finalize;

  wpobject_class->get_supported_features =
      wp_stream_manager_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_stream_manager_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_stream_manager_activate_execute_step;
  wpobject_class->deactivate = wp_stream_manager_deactivate;
}

/*!
 * \brief Constructs a new stream manager object
 * \ingroup wpstreammanager
 * \param core the core associated with the stream manager
 * \returns (transfer full): the new WpStreamManager
 */
WpStreamManager *
wp_stream_manager_new (WpCore *core)
{
  g_return_val_if_fail (core, NULL);
  return g_object_new (WP_TYPE_STREAM_MANAGER,
      "core", core,
      NULL);
}
