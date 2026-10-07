/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <spa/utils/result.h>
#include <spa/param/format.h>
#include <spa/param/audio/raw.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/param.h>
#include <pipewire/pipewire.h>

#include "private/stream.h"
#include "wpenums.h"
#include "error.h"
#include "log.h"
#include "core.h"
#include "object-manager.h"
#include "proxy-interfaces.h"
#include "settings.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-stream")

/* WpStream */

/*! \defgroup wpstream WpStream */
/*!
 * \struct WpStream
 *
 * WpStream is an abstract base class that represents a data stream belonging
 * to a PipeWire node. A stream is a logical collection of ports with the same
 * direction (input or output) and media type (audio or video).
 *
 * This class provides an abstraction over the underlying PipeWire ports,
 * allowing for high-level management of streams without dealing directly with
 * port-level details. Each stream tracks its associated ports, their properties,
 * and their configuration.
 *
 * WpStream is an abstract class with two concrete implementations:
 * - WpAudioStream: represents an audio stream (input or output)
 * - WpVideoStream: represents a video stream (input or output)
 *
 * Streams can be linked together using WpStreamLink objects, which automatically
 * manage the underlying PipeWire links between ports. The WpStreamManager is
 * responsible for creating streams automatically for nodes that appear in the
 * system.
 *
 * A stream emits signals when ports are added, removed, or when the port
 * configuration changes, allowing applications to react to stream topology changes.
 */

enum {
  STREAM_PROP_0,
  STREAM_PROP_NODE,
  STREAM_PROP_PROPERTIES,
  STREAM_PROP_DIRECTION,
  STREAM_PROP_PORT_GROUP,
  STREAM_PROP_CREATION_TIME,
  STREAM_PROP_KIND,
  STREAM_PROP_SHAPE,
};

enum
{
  STREAM_SIGNAL_PORT_ADDED,
  STREAM_SIGNAL_PORT_REMOVED,
  STREAM_SIGNAL_PORTS_CHANGED,
  STREAM_LAST_SIGNAL,
};

static guint stream_signals[STREAM_LAST_SIGNAL] = { 0 };

typedef struct _WpStreamPrivate WpStreamPrivate;
struct _WpStreamPrivate
{
  /* Props */
  GWeakRef node;
  WpProperties *properties;
  WpDirection direction;
  gchar *port_group;
  gint64 creation_time;

  WpObjectManager *ports_om;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpStream, wp_stream, WP_TYPE_OBJECT)

static void
wp_stream_init (WpStream * self)
{
  WpStreamPrivate *priv = wp_stream_get_instance_private (self);

  /* Props */
  g_weak_ref_init (&priv->node, NULL);
  priv->properties = wp_properties_new_empty ();
  priv->creation_time = g_get_monotonic_time ();
}

static void
wp_stream_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  WpStream *self = WP_STREAM (object);
  WpStreamPrivate *priv = wp_stream_get_instance_private (self);

  switch (prop_id) {
    case STREAM_PROP_NODE:
      g_value_take_object (value, wp_stream_get_node (self));
      break;
    case STREAM_PROP_PROPERTIES:
      g_value_take_boxed (value, wp_stream_get_properties (self));
      break;
    case STREAM_PROP_DIRECTION:
      g_value_set_enum (value, priv->direction);
      break;
    case STREAM_PROP_PORT_GROUP:
      g_value_set_string (value, priv->port_group);
      break;
    case STREAM_PROP_CREATION_TIME:
      g_value_set_int64 (value, priv->creation_time);
      break;
    case STREAM_PROP_KIND:
      g_value_set_enum (value, wp_stream_get_kind (self));
      break;
    case STREAM_PROP_SHAPE:
      g_value_set_enum (value, wp_stream_get_shape (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
wp_stream_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  WpStream *self = WP_STREAM (object);
  WpStreamPrivate *priv = wp_stream_get_instance_private (self);

  switch (prop_id) {
    case STREAM_PROP_NODE:
      g_weak_ref_set (&priv->node, g_value_get_object (value));
      break;
    case STREAM_PROP_PROPERTIES:
      g_clear_pointer (&priv->properties, wp_properties_unref);
      priv->properties = g_value_dup_boxed (value);
      break;
    case STREAM_PROP_DIRECTION:
      priv->direction = g_value_get_enum (value);
      break;
    case STREAM_PROP_PORT_GROUP:
      g_clear_pointer (&priv->port_group, g_free);
      priv->port_group = g_value_dup_string (value);
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
wp_stream_get_supported_features (WpObject * self)
{
  return WP_STREAM_FEATURE_ACTIVE;
}

static guint
wp_stream_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  g_return_val_if_fail (missing == WP_STREAM_FEATURE_ACTIVE,
      WP_TRANSITION_STEP_ERROR);

  return STEP_ACTIVATE;
}

static void
on_port_added (WpObjectManager *ports_om, WpPort *port, WpStream * self)
{
  g_signal_emit (self, stream_signals[STREAM_SIGNAL_PORT_ADDED], 0, port);
}

static void
on_port_removed (WpObjectManager *ports_om, WpPort *port, WpStream * self)
{
  g_signal_emit (self, stream_signals[STREAM_SIGNAL_PORT_REMOVED], 0, port);
}

static void
on_ports_changed (WpObjectManager *ports_om, WpStream * self)
{
  g_signal_emit (self, stream_signals[STREAM_SIGNAL_PORTS_CHANGED], 0);
}

static void
on_ports_om_installed (WpObjectManager *ports_om, WpTransition * transition)
{
  WpStream *self = wp_transition_get_source_object (transition);

  /* Let the sub-class complete the activation */
  if (!WP_STREAM_GET_CLASS (self)->enable_active) {
    wp_transition_return_error (transition,
        g_error_new (WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "stream virtual enable_active method is not defined"));
    return;
  }
  WP_STREAM_GET_CLASS (self)->enable_active (self, transition);
}

static void
wp_stream_add_ports_interests (WpStream * self, WpObjectManager * om)
{
  WpStreamPrivate *priv = wp_stream_get_instance_private (self);
  guint node_id = wp_stream_get_node_bound_id (self);

  if (!priv->port_group) {
    wp_object_manager_add_interest (om, WP_TYPE_PORT,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_NODE_ID, "=u", node_id,
        NULL);
    return;
  }

  wp_object_manager_add_interest (om, WP_TYPE_PORT,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_NODE_ID, "=u", node_id,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "port.group", "=s",
      priv->port_group,
      NULL);

  /* In passthrough mode the node hands out the follower's ports as they are
   * and those carry no port.group, even though the PortConfig has a group.
   * FIXME - this looks like a PipeWire bug */
  wp_object_manager_add_interest (om, WP_TYPE_PORT,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_NODE_ID, "=u", node_id,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "port.group", "-",
      NULL);
}

static void
wp_stream_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpStream * self = WP_STREAM (object);
  WpStreamPrivate *priv = wp_stream_get_instance_private (self);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  switch (step) {
  case STEP_ACTIVATE: {
    /* Create the ports object manager */
    g_return_if_fail (priv->ports_om == NULL);
    priv->ports_om = wp_object_manager_new ();
    wp_stream_add_ports_interests (self, priv->ports_om);

    /* Handle ports object manager signals */
    wp_object_manager_request_object_features (priv->ports_om,
        WP_TYPE_PORT, WP_OBJECT_FEATURES_ALL);
    g_signal_connect_object (priv->ports_om, "object-added",
        G_CALLBACK (on_port_added), self, 0);
    g_signal_connect_object (priv->ports_om, "object-removed",
        G_CALLBACK (on_port_removed), self, 0);
    g_signal_connect_object (priv->ports_om, "objects-changed",
        G_CALLBACK (on_ports_changed), self, 0);
    g_signal_connect_object (priv->ports_om, "installed",
        G_CALLBACK (on_ports_om_installed), transition, 0);

    /* Install ports object manager */
    wp_core_install_object_manager (core, priv->ports_om);
    break;
  }
  case WP_TRANSITION_STEP_ERROR:
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_stream_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpStream *self = WP_STREAM (object);
  guint current = wp_object_get_active_features (object);

  if (features & current & WP_STREAM_FEATURE_ACTIVE) {
    WpStreamPrivate *priv = wp_stream_get_instance_private (self);

    /* Disable active sub-class */
    g_return_if_fail (WP_STREAM_GET_CLASS (self)->disable_active);
    WP_STREAM_GET_CLASS (self)->disable_active (self);

    /* Clear object manager */
    g_clear_object (&priv->ports_om);
  }

  wp_object_update_features (WP_OBJECT (self), 0, WP_OBJECT_FEATURES_ALL);
}

static void
wp_stream_finalize (GObject * object)
{
  WpStream *self = WP_STREAM (object);
  WpStreamPrivate *priv = wp_stream_get_instance_private (self);

  /* Props */
  g_clear_pointer (&priv->port_group, g_free);
  g_clear_pointer (&priv->properties, wp_properties_unref);
  g_weak_ref_clear (&priv->node);

  G_OBJECT_CLASS (wp_stream_parent_class)->finalize (object);
}

static void
wp_stream_class_init (WpStreamClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;

  object_class->finalize = wp_stream_finalize;
  object_class->set_property = wp_stream_set_property;
  object_class->get_property = wp_stream_get_property;

  wpobject_class->get_supported_features = wp_stream_get_supported_features;
  wpobject_class->activate_get_next_step = wp_stream_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_stream_activate_execute_step;
  wpobject_class->deactivate = wp_stream_deactivate;

  g_object_class_install_property (object_class, STREAM_PROP_NODE,
      g_param_spec_object ("node", "node", "The Node this stream belongs to",
          WP_TYPE_NODE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, STREAM_PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "Properties",
          "The stream properties", WP_TYPE_PROPERTIES,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, STREAM_PROP_DIRECTION,
      g_param_spec_enum ("direction", "Direction",
          "The stream direction", WP_TYPE_DIRECTION, WP_DIRECTION_INPUT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, STREAM_PROP_PORT_GROUP,
      g_param_spec_string ("port-group", "Port-Group",
          "The stream port group", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, STREAM_PROP_CREATION_TIME,
      g_param_spec_int64 ("creation-time", "creation-time",
          "The stream creation time in microseconds", INT64_MIN, INT64_MAX,
          0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, STREAM_PROP_KIND,
      g_param_spec_enum ("kind", "Kind",
          "The stream kind", WP_TYPE_STREAM_KIND, WP_STREAM_KIND_UNKNOWN,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, STREAM_PROP_SHAPE,
      g_param_spec_enum ("shape", "Shape",
          "The stream shape", WP_TYPE_STREAM_SHAPE, WP_STREAM_SHAPE_UNKNOWN,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  stream_signals[STREAM_SIGNAL_PORT_ADDED] = g_signal_new (
      "port-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpStreamClass, port_added), NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_PORT);

  stream_signals[STREAM_SIGNAL_PORT_REMOVED] = g_signal_new (
      "port-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpStreamClass, port_removed), NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_PORT);

  stream_signals[STREAM_SIGNAL_PORTS_CHANGED] = g_signal_new (
      "ports-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (WpStreamClass, ports_changed), NULL, NULL, NULL,
      G_TYPE_NONE, 0);
}

/*!
 * \brief Gets the node this stream belongs to
 * \ingroup wpstream
 * \param self the stream
 * \returns (transfer full): the node this stream belongs to
 */
WpNode *
wp_stream_get_node (WpStream *self)
{
  WpStreamPrivate *priv;

  g_return_val_if_fail (WP_IS_STREAM (self), NULL);

  priv = wp_stream_get_instance_private (self);

  return g_weak_ref_get (&priv->node);
}

/*!
 * \brief Gets the stream properties
 * \ingroup wpstream
 * \param self the stream
 * \returns (transfer full): the stream properties
 */
WpProperties *
wp_stream_get_properties (WpStream * self)
{
  WpStreamPrivate *priv;

  g_return_val_if_fail (WP_IS_STREAM (self), NULL);

  priv = wp_stream_get_instance_private (self);

  g_return_val_if_fail (priv->properties, NULL);
  return wp_properties_ref (priv->properties);
}

/*!
 * \brief Gets the stream direction
 * \ingroup wpstream
 * \param self the stream
 * \returns the stream direction
 */
WpDirection
wp_stream_get_direction (WpStream * self)
{
  WpStreamPrivate *priv;

  g_return_val_if_fail (WP_IS_STREAM (self), WP_DIRECTION_INPUT);

  priv = wp_stream_get_instance_private (self);

  return priv->direction;
}

/*!
 * \brief Gets the stream port group
 * \ingroup wpstream
 * \param self the stream
 * \returns (nullable): the stream port group
 */
const gchar *
wp_stream_get_port_group (WpStream *self)
{
  WpStreamPrivate *priv;

  g_return_val_if_fail (WP_IS_STREAM (self), NULL);

  priv = wp_stream_get_instance_private (self);

  return priv->port_group;
}

/*!
 * \brief Gets the stream creation time in microseconds.
 * \ingroup wpstream
 * \param self the stream
 * \returns the stream creation time in microseconds.
 */
gint64
wp_stream_get_creation_time (WpStream *self)
{
  WpStreamPrivate *priv;

  g_return_val_if_fail (WP_IS_STREAM (self), -1);

  priv = wp_stream_get_instance_private (self);

  return priv->creation_time;
}

/*!
 * \brief Gets the node's bound id this stream belongs to
 * \ingroup wpstream
 * \param self the stream
 * \returns node's bound id this stream belongs to
 */
guint
wp_stream_get_node_bound_id (WpStream * self)
{
  g_autoptr (WpNode) node = NULL;

  g_return_val_if_fail (WP_IS_STREAM (self), SPA_ID_INVALID);

  /* Make sure the node is valid and bound */
  node = wp_stream_get_node (self);
  if (!node)
    return SPA_ID_INVALID;
  if (!wp_object_test_active_features (WP_OBJECT (node),
      WP_PROXY_FEATURE_BOUND))
    return SPA_ID_INVALID;

  return wp_proxy_get_bound_id (WP_PROXY (node));
}

/*!
 * \brief Gets the stream's kind value
 * \ingroup wpstream
 * \param self the stream
 * \returns the stream's kind value
 */
WpStreamKind
wp_stream_get_kind (WpStream *self)
{
  WpStreamShape shape;

  g_return_val_if_fail (WP_IS_STREAM (self), WP_STREAM_KIND_UNKNOWN);

  shape = wp_stream_get_shape (self);
  if (shape == WP_STREAM_SHAPE_SINK || shape == WP_STREAM_SHAPE_SOURCE)
    return WP_STREAM_KIND_TARGET;
  else if (shape == WP_STREAM_SHAPE_INPUT || shape == WP_STREAM_SHAPE_OUTPUT)
    return WP_STREAM_KIND_SUBJECT;

  return WP_STREAM_KIND_UNKNOWN;
}

/*!
 * \brief Gets the stream's shape value
 * \ingroup wpstream
 * \param self the stream
 * \returns the stream's shape value
 */
WpStreamShape
wp_stream_get_shape (WpStream *self)
{
  WpStreamPrivate *priv;
  const gchar *mc;

  g_return_val_if_fail (WP_IS_STREAM (self), WP_STREAM_SHAPE_UNKNOWN);

  priv = wp_stream_get_instance_private (self);

  /* The stream specific media class has priority over the node one */
  mc = wp_properties_get (priv->properties,
      STREAM_PROPS_PREFIX PW_KEY_MEDIA_CLASS);
  if (!mc)
    mc = wp_properties_get (priv->properties, PW_KEY_MEDIA_CLASS);
  if (!mc)
    return WP_STREAM_SHAPE_UNKNOWN;

  if (g_strstr_len (mc, -1, "Sink"))
    return WP_STREAM_SHAPE_SINK;
  else if (g_strstr_len (mc, -1, "Source"))
    return WP_STREAM_SHAPE_SOURCE;
  else if (g_strstr_len (mc, -1, "Input"))
    return WP_STREAM_SHAPE_INPUT;
  else if (g_strstr_len (mc, -1, "Output"))
    return WP_STREAM_SHAPE_OUTPUT;

  return WP_STREAM_SHAPE_UNKNOWN;
}

/**
 * \brief Checks whether the stream supports a common format with another stream
 *
 * \ingroup wpstream
 * \param self the stream
 * \param other the other stream
 * \param encoded_only (out) (nullable): whether the only common format is an
 *    encoded format
 * \returns TRUE if the stream is compatible with other, FALSE otherwise.
 */
gboolean
wp_stream_is_compatible (WpStream * self, WpStream *other,
    gboolean *encoded_only)
{
  g_return_val_if_fail (WP_IS_STREAM (self), FALSE);
  g_return_val_if_fail (WP_IS_STREAM (other), FALSE);
  g_return_val_if_fail (WP_STREAM_GET_CLASS (self)->is_compatible, FALSE);

  return WP_STREAM_GET_CLASS (self)->is_compatible (self, other, encoded_only);
}

/*!
 * \brief Gets the number of supported raw formats
 * \ingroup wpstream
 * \param self the stream
 * \returns the number of supported raw formats
 */
guint
wp_stream_get_supported_raw_format_count (WpStream *self)
{
  g_return_val_if_fail (WP_IS_STREAM (self), 0);
  g_return_val_if_fail (WP_STREAM_GET_CLASS (self)->get_supported_raw_format_count, 0);

  return WP_STREAM_GET_CLASS (self)->get_supported_raw_format_count (self);
}

/*!
 * \brief Gets the number of supported encoded formats
 * \ingroup wpstream
 * \param self the stream
 * \returns the number of supported encoded formats
 */
guint
wp_stream_get_supported_enc_format_count (WpStream *self)
{
  g_return_val_if_fail (WP_IS_STREAM (self), 0);
  g_return_val_if_fail (WP_STREAM_GET_CLASS (self)->get_supported_enc_format_count, 0);

  return WP_STREAM_GET_CLASS (self)->get_supported_enc_format_count (self);
}

/*!
 * \brief Creates an iterator over the supported raw formats
 * \ingroup wpstream
 * \param self the stream
 * \returns (transfer full): a WpIterator over WpSpaPod raw format objects
 */
WpIterator *
wp_stream_new_supported_raw_format_iterator (WpStream *self)
{
  g_return_val_if_fail (WP_IS_STREAM (self), NULL);
  g_return_val_if_fail (WP_STREAM_GET_CLASS (self)->new_supported_raw_format_iterator, NULL);

  return WP_STREAM_GET_CLASS (self)->new_supported_raw_format_iterator (self);
}

/*!
 * \brief Creates an iterator over the supported encoded formats
 * \ingroup wpstream
 * \param self the stream
 * \returns (transfer full): a WpIterator over WpSpaPod encoded format objects
 */
WpIterator *
wp_stream_new_supported_enc_format_iterator (WpStream *self)
{
  g_return_val_if_fail (WP_IS_STREAM (self), NULL);
  g_return_val_if_fail (WP_STREAM_GET_CLASS (self)->new_supported_enc_format_iterator, NULL);

  return WP_STREAM_GET_CLASS (self)->new_supported_enc_format_iterator (self);
}

/*!
 * \ingroup wpstream
 * \param self the stream
 * \returns TRUE if the stream is exclusive, FALSE otherwise
 */
gboolean
wp_stream_is_exclusive (WpStream *self)
{
  g_return_val_if_fail (WP_IS_STREAM (self), FALSE);
  g_return_val_if_fail (WP_STREAM_GET_CLASS (self)->is_exclusive, FALSE);

  return WP_STREAM_GET_CLASS (self)->is_exclusive (self);
}

/*!
 * \brief Checks if the stream has the given port ID or not
 * \ingroup wpstream
 * \param self the stream
 * \param port_id the port ID to check
 * \returns TRUE if the stream has the port, FALSE otherwise
 */
gboolean
wp_stream_has_port (WpStream *self, guint32 port_id)
{
  WpStreamPrivate *priv;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  g_return_val_if_fail (WP_IS_STREAM (self), FALSE);
  g_return_val_if_fail (port_id != SPA_ID_INVALID, FALSE);

  priv = wp_stream_get_instance_private (self);
  if (!priv->ports_om)
    return FALSE;

  it = wp_stream_new_ports_iterator (self);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpPort *port = g_value_get_object (&val);
    if (wp_proxy_get_bound_id (WP_PROXY (port)) == port_id)
      return TRUE;
  }

  return FALSE;
}

/*!
 * \brief Checks if the stream has the given monitor port ID or not
 * \ingroup wpstream
 * \param self the stream
 * \param port_id the monitor port ID to check
 * \returns TRUE if the stream has the monitor port, FALSE otherwise
 */
gboolean
wp_stream_has_monitor_port (WpStream *self, guint32 port_id)
{
  WpStreamPrivate *priv;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  g_return_val_if_fail (WP_IS_STREAM (self), FALSE);
  g_return_val_if_fail (port_id != SPA_ID_INVALID, FALSE);

  priv = wp_stream_get_instance_private (self);
  if (!priv->ports_om)
    return FALSE;

  it = wp_stream_new_monitor_ports_iterator (self);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpPort *port = g_value_get_object (&val);
    if (wp_proxy_get_bound_id (WP_PROXY (port)) == port_id)
      return TRUE;
  }

  return FALSE;
}

/*!
 * \brief Checks if the stream has the given control port ID or not
 * \ingroup wpstream
 * \param self the stream
 * \param port_id the control port ID to check
 * \returns TRUE if the stream has the control port, FALSE otherwise
 */
gboolean
wp_stream_has_control_port (WpStream *self, guint32 port_id)
{
  WpStreamPrivate *priv;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  g_return_val_if_fail (WP_IS_STREAM (self), FALSE);
  g_return_val_if_fail (port_id != SPA_ID_INVALID, FALSE);

  priv = wp_stream_get_instance_private (self);
  if (!priv->ports_om)
    return FALSE;

  it = wp_stream_new_control_ports_iterator (self);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpPort *port = g_value_get_object (&val);
    if (wp_proxy_get_bound_id (WP_PROXY (port)) == port_id)
      return TRUE;
  }

  return FALSE;
}

/*!
 * \brief Gets the streams's total number of non-monitor and non-control ports
 * \ingroup wpstream
 * \param self the stream
 * \returns The total number of non-monitor and non-control ports
 */
guint
wp_stream_get_ports_count (WpStream *self)
{
  WpStreamPrivate *priv;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  guint count = 0;

  g_return_val_if_fail (WP_IS_STREAM (self), 0);

  priv = wp_stream_get_instance_private (self);
  if (!priv->ports_om)
    return 0;

  it = wp_stream_new_ports_iterator (self);
  for (; wp_iterator_next (it, &val); g_value_unset (&val))
    count++;

  return count;
}

/*!
 * \brief Gets the stream's total number of monitor ports
 * \ingroup wpstream
 * \param self the stream
 * \returns The total number of ports
 */
guint
wp_stream_get_monitor_ports_count (WpStream *self)
{
  WpStreamPrivate *priv;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  guint count = 0;

  g_return_val_if_fail (WP_IS_STREAM (self), 0);

  priv = wp_stream_get_instance_private (self);
  if (!priv->ports_om)
    return 0;

  it = wp_stream_new_monitor_ports_iterator (self);
  for (; wp_iterator_next (it, &val); g_value_unset (&val))
    count++;

  return count;
}

/*!
 * \brief Gets the stream's total number of control ports
 * \ingroup wpstream
 * \param self the stream
 * \returns The total number of ports
 */
guint
wp_stream_get_control_ports_count (WpStream *self)
{
  WpStreamPrivate *priv;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  guint count = 0;

  g_return_val_if_fail (WP_IS_STREAM (self), 0);

  priv = wp_stream_get_instance_private (self);
  if (!priv->ports_om)
    return 0;

  it = wp_stream_new_control_ports_iterator (self);
  for (; wp_iterator_next (it, &val); g_value_unset (&val))
    count++;

  return count;
}

/*!
 * \brief Iterates through all the stream non-monitor and non-control ports.
 * \ingroup wpstream
 * \param self the stream
 * \returns (transfer full): a WpIterator that iterates over all the stream
 *   ports that are not monitor or control ports.
 */
WpIterator *
wp_stream_new_ports_iterator (WpStream *self)
{
  WpStreamPrivate *priv;
  g_autoptr (WpObjectInterest) interest = NULL;

  g_return_val_if_fail (WP_IS_STREAM (self), NULL);

  priv = wp_stream_get_instance_private (self);
  g_return_val_if_fail (priv->ports_om, NULL);

  /* the object manager already holds only this stream's ports */
  interest = wp_object_interest_new (WP_TYPE_PORT,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_PORT_MONITOR, "!s", "true",
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_PORT_CONTROL, "!s", "true",
      NULL);
  return wp_object_manager_new_filtered_iterator_full (priv->ports_om,
      g_steal_pointer (&interest));
}

/*!
 * \brief Iterates through all the stream monitor ports.
 * \ingroup wpstream
 * \param self the stream
 * \returns (transfer full): a WpIterator that iterates over all the stream
 *   monitor ports.
 */
WpIterator *
wp_stream_new_monitor_ports_iterator (WpStream *self)
{
  WpStreamPrivate *priv;
  g_autoptr (WpObjectInterest) interest = NULL;

  g_return_val_if_fail (WP_IS_STREAM (self), NULL);

  priv = wp_stream_get_instance_private (self);
  g_return_val_if_fail (priv->ports_om, NULL);

  /* the object manager already holds only this stream's ports */
  interest = wp_object_interest_new (WP_TYPE_PORT,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_PORT_MONITOR, "=s", "true",
      NULL);
  return wp_object_manager_new_filtered_iterator_full (priv->ports_om,
      g_steal_pointer (&interest));
}

/*!
 * \brief Iterates through all the stream control ports.
 * \ingroup wpstream
 * \param self the stream
 * \returns (transfer full): a WpIterator that iterates over all the stream
 *   control ports.
 */
WpIterator *
wp_stream_new_control_ports_iterator (WpStream *self)
{
  WpStreamPrivate *priv;
  g_autoptr (WpObjectInterest) interest = NULL;

  g_return_val_if_fail (WP_IS_STREAM (self), NULL);

  priv = wp_stream_get_instance_private (self);
  g_return_val_if_fail (priv->ports_om, NULL);

  /* the object manager already holds only this stream's ports */
  interest = wp_object_interest_new (WP_TYPE_PORT,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_PORT_CONTROL, "=s", "true",
      NULL);
  return wp_object_manager_new_filtered_iterator_full (priv->ports_om,
      g_steal_pointer (&interest));
}


/* Audio Stream */

enum
{
  AUDIO_STREAM_SIGNAL_PORTS_CONFIGURED,
  AUDIO_STREAM_SIGNAL_PORTS_ERROR,
  AUDIO_STREAM_LAST_SIGNAL,
};

static guint audio_stream_signals[AUDIO_STREAM_LAST_SIGNAL] = { 0 };

enum {
  AUDIO_STREAM_PROP_0,
  AUDIO_STREAM_PROP_PORTS_STATE,
  AUDIO_STREAM_N_PROPS,
};

static GParamSpec *audio_stream_props[AUDIO_STREAM_N_PROPS] = { NULL };

/*!
 * \struct WpAudioStream
 *
 * A WpStream that carries audio. It reads the formats the node supports from
 * EnumFormat, both raw and encoded, and it is the only stream kind whose ports
 * are configured: wp_audio_stream_configure_ports() and its variants set
 * PortConfig on the node, choosing the port layout (DSP, convert or
 * passthrough) and whether monitor and control ports exist.
 *
 * Port configuration is asynchronous. The "ports-configured" signal is emitted
 * once the node has applied a configuration this stream asked for, and
 * "ports-error" if it refused it.
 */
struct _WpAudioStream
{
  WpStream parent;

  /* EnumFormat info */
  GPtrArray *supported_raw_formats;
  GPtrArray *supported_enc_formats;

  /* PortConfig info */
  enum spa_param_port_config_mode curr_mode;
  gboolean curr_monitor;
  gboolean curr_control;
  WpSpaPod *curr_format;
  gint pending_ports_config_seq;
  gboolean pending_ports_config_synced;
  WpAudioStreamPortsState ports_state;

  /* The configuration that curr_* held before the pending request, restored
   * if the server rejects it */
  enum spa_param_port_config_mode prev_mode;
  gboolean prev_monitor;
  gboolean prev_control;
  WpSpaPod *prev_format;

  /* Activation info */
  gulong params_changed_sigid;
  gulong error_sigid;
};

G_DEFINE_TYPE (WpAudioStream, wp_audio_stream, WP_TYPE_STREAM);

static void
wp_audio_stream_init (WpAudioStream * self)
{
  /* EnumFormat info */
  self->supported_raw_formats = g_ptr_array_new_with_free_func (
      (GDestroyNotify)wp_spa_pod_unref);
  self->supported_enc_formats = g_ptr_array_new_with_free_func (
      (GDestroyNotify)wp_spa_pod_unref);

  /* PortConfig info */
  self->curr_mode = SPA_PARAM_PORT_CONFIG_MODE_none;
  self->curr_monitor = TRUE;
  self->curr_control = FALSE;
  self->curr_format = NULL;
  self->pending_ports_config_seq = 0;
  self->pending_ports_config_synced = FALSE;
  self->ports_state = WP_AUDIO_STREAM_PORTS_STATE_UNCONFIGURED;
  self->prev_mode = SPA_PARAM_PORT_CONFIG_MODE_none;
  self->prev_monitor = TRUE;
  self->prev_control = FALSE;
  self->prev_format = NULL;

  /* Activation info */
  self->params_changed_sigid = 0;
  self->error_sigid = 0;
}

/* Sets the ports state and notifies it if it changed */
static void
audio_stream_set_ports_state (WpAudioStream *self,
    WpAudioStreamPortsState state)
{
  if (state == self->ports_state)
    return;

  self->ports_state = state;
  g_object_notify_by_pspec (G_OBJECT (self),
      audio_stream_props[AUDIO_STREAM_PROP_PORTS_STATE]);
}

/* Sets the ports state from the current port configuration, when no request
 * is in flight anymore */
static void
audio_stream_update_ports_state (WpAudioStream *self)
{
  audio_stream_set_ports_state (self,
      (self->curr_format && self->curr_mode != SPA_PARAM_PORT_CONFIG_MODE_none) ?
          WP_AUDIO_STREAM_PORTS_STATE_CONFIGURED :
          WP_AUDIO_STREAM_PORTS_STATE_UNCONFIGURED);
}

static gboolean
parse_format (WpSpaPod *format, guint32 *media_type, guint *fmt, gint *channels,
    gint *rate, WpSpaPod **position)
{
  g_autoptr (WpSpaPodParser) parser = NULL;
  guint32 t = 0, s = 0, f = 0;
  gint c = 0, r = 0;
  g_autoptr (WpSpaPod) p = NULL;

  g_return_val_if_fail (format, FALSE);
  parser = wp_spa_pod_parser_new_object (format, NULL);
  g_return_val_if_fail (parser, FALSE);

  /* Parse mandatory fields */
  if (!wp_spa_pod_parser_get (parser, "mediaType", "I", &t, NULL) ||
      !wp_spa_pod_parser_get (parser, "mediaSubtype", "I", &s, NULL) ||
      !wp_spa_pod_parser_get (parser, "format", "I", &f, NULL) ||
      !wp_spa_pod_parser_get (parser, "channels", "i", &c, NULL))
    return FALSE;

  /* Parse optional fields */
  wp_spa_pod_parser_get (parser, "rate", "i", &r, NULL);
  wp_spa_pod_parser_get (parser, "position", "P", &p, NULL);

  if (media_type)
    *media_type = t;
  if (fmt)
    *fmt = f;
  if (channels)
    *channels = c;
  if (rate)
    *rate = r;
  if (position)
    *position = p ? wp_spa_pod_ref (p) : NULL;

  return TRUE;
}

static WpSpaPod *
build_audio_raw_format (const struct spa_audio_info_raw *info)
{
  g_autoptr (WpSpaPodBuilder) builder = wp_spa_pod_builder_new_object (
      "Spa:Pod:Object:Param:Format", "Format");
  wp_spa_pod_builder_add (builder,
      "mediaType",    "K", "audio",
      "mediaSubtype", "K", "raw",
      "format",       "I", info->format,
      "channels",     "i", info->channels,
      NULL);

  /* Add optional rate */
  if (info->rate > 0)
    wp_spa_pod_builder_add (builder, "rate", "i", info->rate, NULL);

  /* Add optional position */
  if (!SPA_FLAG_IS_SET (info->flags, SPA_AUDIO_FLAG_UNPOSITIONED)) {
    /* Build the position array spa pod */
    g_autoptr (WpSpaPodBuilder) position_builder =
        wp_spa_pod_builder_new_array ();
    guint n_pos = SPA_MIN(info->channels, SPA_N_ELEMENTS(info->position));
    for (guint i = 0; i < n_pos; i++)
      wp_spa_pod_builder_add_id (position_builder, info->position[i]);

    /* Add the position property */
    wp_spa_pod_builder_add_property (builder, "position");
    g_autoptr (WpSpaPod) position =
         wp_spa_pod_builder_end (position_builder);
    wp_spa_pod_builder_add_pod (builder, position);
  }

  return wp_spa_pod_builder_end (builder);
}

static WpSpaPod *
build_audio_mono_format (WpSpaPod *format)
{
  struct spa_audio_info_raw raw_format = { 0, };
  guint fmt = 0;
  gint rate = 0;

  if (!parse_format (format, NULL, &fmt, NULL, &rate, NULL))
    return NULL;

  raw_format.format = fmt;
  raw_format.rate = rate;
  raw_format.channels = 1;
  raw_format.position [0] = SPA_AUDIO_CHANNEL_MONO;

  return build_audio_raw_format (&raw_format);
}

static WpSpaPod *
build_audio_format (WpAudioStream * self, guint32 format, gint channels,
    gint rate, WpSpaPod *pos)
{
  g_autoptr (WpSpaPod) position = pos;
  g_autoptr (WpSpaPodBuilder) b = NULL;

  g_return_val_if_fail (channels > 0, NULL);

  /* build the position array if not given */
  if (!position) {
    switch (channels) {
    case 1: {
      g_autoptr (WpSpaPodBuilder) pos_b = wp_spa_pod_builder_new_array ();
      wp_spa_pod_builder_add_id (pos_b, SPA_AUDIO_CHANNEL_MONO);
      position = wp_spa_pod_builder_end (pos_b);
      break;
    }
    case 2: {
      g_autoptr (WpSpaPodBuilder) pos_b = wp_spa_pod_builder_new_array ();
      wp_spa_pod_builder_add_id (pos_b, SPA_AUDIO_CHANNEL_FL);
      wp_spa_pod_builder_add_id (pos_b, SPA_AUDIO_CHANNEL_FR);
      position = wp_spa_pod_builder_end (pos_b);
      break;
    }
    default:
      break;
    }
  }

  /* Build the format */
  b = wp_spa_pod_builder_new_object ("Spa:Pod:Object:Param:Format", "Format");
  wp_spa_pod_builder_add_property (b, "mediaType");
  wp_spa_pod_builder_add_id (b, SPA_MEDIA_TYPE_audio);
  wp_spa_pod_builder_add_property (b, "mediaSubtype");
  wp_spa_pod_builder_add_id (b, SPA_MEDIA_SUBTYPE_raw);
  wp_spa_pod_builder_add_property (b, "format");
  wp_spa_pod_builder_add_id (b, format);
  wp_spa_pod_builder_add_property (b, "channels");
  wp_spa_pod_builder_add_int (b, channels);

  /* Add optional rate */
  if (rate > 0) {
    wp_spa_pod_builder_add_property (b, "rate");
    wp_spa_pod_builder_add_int (b, rate);
  }

  /* Add optional position */
  if (position) {
    wp_spa_pod_builder_add_property (b, "position");
    wp_spa_pod_builder_add_pod (b, position);
  }

  return wp_spa_pod_builder_end (b);
}

static gint
get_default_clock_rate (WpAudioStream * self)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpProperties) props = NULL;
  const gchar *rate_str = NULL;

  core = wp_object_get_core (WP_OBJECT (self));
  g_return_val_if_fail (core, 48000);

  props = wp_core_get_remote_properties (core);
  g_return_val_if_fail (props, 48000);

  rate_str = wp_properties_get (props, "default.clock.rate");
  return rate_str ? atoi (rate_str) : 48000;
}

static WpSpaPod *
ensure_audio_format_with_rate (WpAudioStream *self, WpSpaPod *format)
{
  guint media_type = 0, fmt = 0;
  gint channels = 0, rate = 0;
  g_autoptr (WpSpaPod) position = NULL;

  /* Just return format if it is not a RAW format, or has already valid rate */
  if (!parse_format (format, &media_type, &fmt, &channels, &rate, &position) ||
      media_type != SPA_MEDIA_TYPE_audio || rate > 0)
    return wp_spa_pod_ref (format);

  /* Otherwise use the rate from a matching supported RAW format */
  for (guint i = 0; i < self->supported_raw_formats->len; i++) {
    WpSpaPod *sup_format = g_ptr_array_index (self->supported_raw_formats, i);
    guint sup_fmt = 0;
    gint sup_channels = 0, sup_rate = 0;

    if (!parse_format (sup_format, NULL, &sup_fmt, &sup_channels, &sup_rate,
        NULL))
      continue;

    if (sup_fmt == fmt && sup_channels == channels && sup_rate > 0)
      return build_audio_format (self, fmt, channels, sup_rate,
          g_steal_pointer (&position));
  }

  /* Otherwise use default clock rate */
  return build_audio_format (self, fmt, channels, get_default_clock_rate (self),
      g_steal_pointer (&position));
}

static WpSpaPod *
build_audio_dsp_format (WpAudioStream * self, WpSpaPod *dev_format)
{
  g_autoptr (WpSpaPod) position = NULL;
  g_autoptr (WpSpaPod) format = NULL;
  gint channels = 2;
  gint rate = 0;

  /* parse device format */
  if (dev_format && !parse_format (dev_format, NULL, NULL, &channels, &rate,
      &position))
    return NULL;

  /* build F32P with same channels and position as device format */
  format = build_audio_format (self, SPA_AUDIO_FORMAT_F32P, channels, rate,
      g_steal_pointer (&position));

  /* Ensure the format always has a rate, because this will be checked when
   * configuring the ports. Also, older versions of pipewire (such as 1.4.0)
   * require a rate when configuring the ports. */
  return ensure_audio_format_with_rate (self, format);
}

static gboolean
update_audio_port_config_info (WpAudioStream *self, WpIterator *it)
{
  const gchar *stream_port_group;
  g_auto (GValue) val = G_VALUE_INIT;
  gboolean ports_config_found = FALSE;

  /* Get the stream port group */
  stream_port_group = wp_stream_get_port_group (WP_STREAM (self));

  /* Find the PortConfig for this stream and update the current one */
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSpaPod *param = g_value_get_boxed (&val);
    enum spa_direction dir = SPA_DIRECTION_INPUT;
    const gchar *port_group = NULL;

    /* Parse mandatory fields */
    if (!wp_spa_pod_is_object (param) ||
        !wp_spa_pod_get_object (param, NULL,
            "direction", "I", &dir,
            NULL)) {
      wp_warning_object (self, "unknown direction from PortConfig param %p",
          param);
      continue;
    }

    /* Get optional fields. The PortConfig "group" key is not in a released
       pipewire yet, see "spa: add port config group name" in
       spa/param/port-config.h, so we name it by its raw id until it is */
    wp_spa_pod_get_object (param, NULL,
        "id-00000006", "s", &port_group,
        NULL);

    /* See if the port config belongs to this streams and parse it */
    if (g_strcmp0 (stream_port_group, port_group) == 0) {
      enum spa_param_port_config_mode mode = SPA_PARAM_PORT_CONFIG_MODE_none;
      gboolean monitor = FALSE;
      gboolean control = FALSE;
      g_autoptr (WpSpaPod) format = NULL;

      /* Every field is optional: a PortConfig in no-dsp mode carries only the
       * mode and the format, and one from a node that has not been configured
       * yet carries neither */
      wp_spa_pod_get_object (param, NULL,
          "mode",    "?I", &mode,
          "monitor", "?b", &monitor,
          "control", "?b", &control,
          "format",  "?P", &format,
          NULL);

      /* Update mode, monitor and control values */
      self->curr_mode = mode;
      self->curr_monitor = monitor;
      self->curr_control = control;

      /* Ensure PortConfig's format has always rate, because this will be
       * checked when configuring the ports. Also, older versions of pipewire
       * (such as 1.4.0) requires a rate when configuring the ports. */
      if (format) {
        g_clear_pointer (&self->curr_format, wp_spa_pod_unref);
        self->curr_format = ensure_audio_format_with_rate (self, format);
      }

      ports_config_found = TRUE;
      break;
    }
  }

  if (self->ports_state != WP_AUDIO_STREAM_PORTS_STATE_CONFIGURING)
    audio_stream_update_ports_state (self);
  return ports_config_found;
}

static gint
compare_raw_formats_by_channels (gconstpointer a, gconstpointer b)
{
  WpSpaPod *pod_a = *(WpSpaPod **) a;
  WpSpaPod *pod_b = *(WpSpaPod **) b;
  gint channels_a = 0, channels_b = 0;

  wp_spa_pod_get_object (pod_a, NULL, "channels", "i", &channels_a, NULL);
  wp_spa_pod_get_object (pod_b, NULL, "channels", "i", &channels_b, NULL);

  return channels_b - channels_a;
}

/* A client that does not position its channels can still advertise a position
 * array, with every channel UNKNOWN */
static gboolean
has_only_unknown_positions (const struct spa_audio_info_raw *info)
{
  guint32 n_pos = SPA_MIN (info->channels, SPA_N_ELEMENTS (info->position));

  for (guint32 i = 0; i < n_pos; i++) {
    if (info->position[i] != SPA_AUDIO_CHANNEL_UNKNOWN)
      return FALSE;
  }

  return TRUE;
}

static void
audio_stream_add_supported_format (WpAudioStream *self, WpSpaPod *pod)
{
  uint32_t mtype, msubtype;

  /* Skip formats without media type and sub-type */
  if (!wp_spa_pod_is_object (pod) || !wp_spa_pod_get_object (pod, NULL,
          "mediaType", "I", &mtype,
          "mediaSubtype", "I", &msubtype,
          NULL))
    return;

  /* Skip non-audio formats */
  if (mtype != SPA_MEDIA_TYPE_audio)
    return;

  switch (msubtype) {
    case SPA_MEDIA_SUBTYPE_raw: {
      struct spa_audio_info_raw raw_format = { 0, };
      struct spa_pod *position = NULL;

      wp_spa_pod_fixate (pod);

      if (spa_pod_parse_object (wp_spa_pod_get_spa_pod (pod),
          SPA_TYPE_OBJECT_Format, NULL,
          SPA_FORMAT_AUDIO_format, SPA_POD_OPT_Id (&raw_format.format),
          SPA_FORMAT_AUDIO_rate, SPA_POD_OPT_Int (&raw_format.rate),
          SPA_FORMAT_AUDIO_channels, SPA_POD_OPT_Int (&raw_format.channels),
          SPA_FORMAT_AUDIO_position, SPA_POD_OPT_Pod (&position)) < 0)
        return;

      if (position == NULL ||
          !spa_pod_copy_array (position, SPA_TYPE_Id, raw_format.position,
              SPA_N_ELEMENTS(raw_format.position)) ||
          has_only_unknown_positions (&raw_format))
        SPA_FLAG_SET (raw_format.flags, SPA_AUDIO_FLAG_UNPOSITIONED);

      g_ptr_array_add (self->supported_raw_formats,
          build_audio_raw_format (&raw_format));
      break;
    }

    case SPA_MEDIA_SUBTYPE_iec958:
    case SPA_MEDIA_SUBTYPE_dsd:
      wp_info_object (self, "passthrough IEC958/DSD format found");
      g_ptr_array_add (self->supported_enc_formats, wp_spa_pod_ref (pod));
      break;

    default: {
      enum spa_audio_format audio_format;
      if (spa_pod_parse_object (wp_spa_pod_get_spa_pod (pod),
            SPA_TYPE_OBJECT_Format, NULL,
            SPA_FORMAT_AUDIO_format, SPA_POD_OPT_Id (&audio_format)) >= 0) {
        if (audio_format == SPA_AUDIO_FORMAT_ENCODED)
          g_ptr_array_add (self->supported_enc_formats, wp_spa_pod_ref (pod));
      }
      break;
    }
  }
}

/* Sort the supported formats and tell whether any was found */
static gboolean
audio_stream_finish_supported_formats (WpAudioStream *self)
{
  /* Sort raw formats by channels (highest first) */
  g_ptr_array_sort (self->supported_raw_formats, compare_raw_formats_by_channels);

  if (self->supported_raw_formats->len > 0)
    wp_info_object (self, "supports RAW audio");

  if (self->supported_enc_formats->len > 0)
    wp_info_object (self, "supports encoded audio");

  return self->supported_raw_formats->len > 0 || self->supported_enc_formats->len > 0;
}

static void
audio_stream_reset_supported_formats (WpAudioStream *self)
{
  g_ptr_array_set_size (self->supported_raw_formats, 0);
  g_ptr_array_set_size (self->supported_enc_formats, 0);
}

static gboolean
update_audio_enum_format_info (WpAudioStream *self, WpIterator *it)
{
  g_auto (GValue) val = G_VALUE_INIT;

  audio_stream_reset_supported_formats (self);

  for (; wp_iterator_next (it, &val); g_value_unset (&val))
    audio_stream_add_supported_format (self, g_value_get_boxed (&val));

  return audio_stream_finish_supported_formats (self);
}

/*
 * Reads the supported formats from the internalFormat of the EnumPortConfig
 * params that belong to this stream. A node with several streams has no node
 * level EnumFormat and advertises the formats of each stream this way, with
 * one EnumPortConfig per format.
 */
static gboolean
update_audio_enum_port_config_format_info (WpAudioStream *self,
    WpIterator *it)
{
  WpDirection stream_dir = wp_stream_get_direction (WP_STREAM (self));
  const gchar *stream_port_group = wp_stream_get_port_group (WP_STREAM (self));
  g_auto (GValue) val = G_VALUE_INIT;

  audio_stream_reset_supported_formats (self);

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSpaPod *param = g_value_get_boxed (&val);
    enum spa_direction dir = SPA_DIRECTION_INPUT;
    const gchar *port_group = NULL;
    g_autoptr (WpSpaPod) format = NULL;

    if (!wp_spa_pod_is_object (param) ||
        !wp_spa_pod_get_object (param, NULL,
            "direction", "I", &dir,
            NULL))
      continue;

    /* The "group" and "internalFormat" keys are not in a released pipewire
       yet, see spa/param/port-config.h, so we name them by their raw ids */
    wp_spa_pod_get_object (param, NULL,
        "id-00000006", "?s", &port_group,
        "id-00000007", "?P", &format,
        NULL);

    if ((WpDirection) dir != stream_dir ||
        g_strcmp0 (stream_port_group, port_group) != 0 || !format)
      continue;

    audio_stream_add_supported_format (self, format);
  }

  return audio_stream_finish_supported_formats (self);
}

static void
audio_stream_notify_ports_error (WpAudioStream *self, const gchar *msg)
{
  audio_stream_update_ports_state (self);
  wp_warning_object (self, "%s", msg);
  g_signal_emit (self, audio_stream_signals[AUDIO_STREAM_SIGNAL_PORTS_ERROR], 0,
      msg);
}

/*
 * Emits 'ports-configured' once the configuration requested by
 * wp_audio_stream_configure_ports_internal() has taken effect: the server has
 * handled the request and the ports of the new configuration exist.
 *
 * The node reports port changes more than once and it also reports changes that
 * this stream did not request. Only one of those completes the request; the
 * rest must not be mistaken for it.
 */
static void
audio_stream_check_ports_configured (WpAudioStream *self)
{
  if (self->ports_state != WP_AUDIO_STREAM_PORTS_STATE_CONFIGURING ||
      !self->pending_ports_config_synced ||
      !self->curr_format ||
      wp_stream_get_ports_count (WP_STREAM (self)) == 0)
    return;

  audio_stream_update_ports_state (self);
  wp_info_object (self, "ports configured");
  g_signal_emit (self,
      audio_stream_signals[AUDIO_STREAM_SIGNAL_PORTS_CONFIGURED], 0);
}

/*
 * The barrier that tells us the PortConfig request has been handled. The node
 * only advertises a PortConfig change as a side effect of its follower's
 * Format changing, which does not happen in no-dsp mode, and the server's
 * cached PortConfig is refreshed from the same advertisement, so there is no
 * param change to wait for instead.
 */
static void
on_ports_config_synced (WpCore *core, GAsyncResult *res, WpAudioStream *self)
{
  g_autoptr (GError) error = NULL;

  if (!wp_core_sync_finish (core, res, &error)) {
    if (self->ports_state == WP_AUDIO_STREAM_PORTS_STATE_CONFIGURING)
      audio_stream_notify_ports_error (self, error->message);
    return;
  }

  if (self->ports_state != WP_AUDIO_STREAM_PORTS_STATE_CONFIGURING)
    return;

  self->pending_ports_config_synced = TRUE;
  audio_stream_check_ports_configured (self);
}

/*
 * The server rejects a PortConfig it cannot apply by erroring the node, which
 * reaches us here with the sequence number of the request that failed. The
 * node keeps the configuration it had, so restore what we optimistically
 * recorded as current and report the failure.
 *
 * Node state errors also arrive with this sequence number, some with res 0;
 * those are not a rejection.
 */
static void
on_audio_node_error (WpNode *node, gint seq, gint res, const gchar *message,
    WpAudioStream *self)
{
  g_autofree gchar *msg = NULL;

  if (self->ports_state != WP_AUDIO_STREAM_PORTS_STATE_CONFIGURING ||
      seq != self->pending_ports_config_seq || res >= 0)
    return;

  g_clear_pointer (&self->curr_format, wp_spa_pod_unref);
  self->curr_format = self->prev_format ?
      wp_spa_pod_ref (self->prev_format) : NULL;
  self->curr_mode = self->prev_mode;
  self->curr_monitor = self->prev_monitor;
  self->curr_control = self->prev_control;

  msg = g_strdup_printf ("the port configuration was rejected: %s (%s)",
      message, spa_strerror (res));
  audio_stream_notify_ports_error (self, msg);
}

static void
on_audio_node_port_config_changed (WpPipewireObject *po, GAsyncResult *res,
    WpStream *s)
{
  g_autoptr (WpAudioStream) self = WP_AUDIO_STREAM (s);
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (GError) err = NULL;

  /* Check for errors */
  it = wp_pipewire_object_enum_params_finish (po, res, &err);
  if (!it) {
    audio_stream_notify_ports_error (self,
        "failed to enum PortConfig on params changed");
    return;
  }

  wp_debug_object (self, "enum PortConfig finished on changed");

  /* Update PortConfig info. A node in passthrough mode can advertise no
   * PortConfig; a rejected request is reported by on_audio_node_error() */
  if (update_audio_port_config_info (self, it))
    wp_info_object (self, "PortConfig info updated on changed");
  else
    wp_debug_object (self, "no matching PortConfig param on changed");

  audio_stream_check_ports_configured (self);
}

static void
audio_stream_enum_port_config_on_changed (WpAudioStream *self)
{
  g_autoptr (WpNode) node = NULL;

  /* Get stream node */
  node = wp_stream_get_node (WP_STREAM (self));
  if (!node) {
    audio_stream_notify_ports_error (self,
        "stream node is not valid on params changed");
    return;
  }

  /* Enum PortConfig to see if we need to configure ports or not */
  wp_debug_object (self, "evaluating PortConfig params on changed...");
  wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node), "PortConfig", NULL,
      NULL, (GAsyncReadyCallback) on_audio_node_port_config_changed,
      g_object_ref (self));
}

static void
on_audio_node_enum_format_changed (WpPipewireObject *po, GAsyncResult *res,
    WpStream *s)
{
  g_autoptr (WpAudioStream) self = WP_AUDIO_STREAM (s);
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (GError) err = NULL;

  /* Check for errors */
  it = wp_pipewire_object_enum_params_finish (po, res, &err);
  if (!it) {
    audio_stream_notify_ports_error (self,
        "failed to enum EnumFormat on params changed");
    return;
  }

  wp_debug_object (self, "enum EnumFormat finished on params changed");

  /* Update EnumFormat info */
  if (!update_audio_enum_format_info (self, it)) {
    audio_stream_notify_ports_error (self,
        "no audio format found on params changed");
    return;
  }

  audio_stream_enum_port_config_on_changed (self);
}

static void
on_audio_node_params_changed (WpNode * node, const gchar *param_name,
    WpAudioStream *self)
{
  wp_debug_object (self, "param %s changed", param_name);

  /* Only handle EnumFormat and PortConfig changes */
  if (g_str_equal (param_name, "EnumFormat")) {
    wp_debug_object (self, "enumerating EnumFormat params on changed...");
    wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node), "EnumFormat",
        NULL, NULL, (GAsyncReadyCallback) on_audio_node_enum_format_changed,
        g_object_ref (self));
  } else if (g_str_equal (param_name, "PortConfig")) {
    wp_debug_object (self, "enumerating PortConfig params on changed...");
    wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node), "PortConfig",
        NULL, NULL, (GAsyncReadyCallback) on_audio_node_port_config_changed,
        g_object_ref (self));
  }
}

static void
on_audio_node_port_config_done (WpPipewireObject *po, GAsyncResult *res,
    WpTransition *t)
{
  g_autoptr (WpTransition) transition = t;
  WpAudioStream *self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (GError) err = NULL;

  /* Check for errors */
  it = wp_pipewire_object_enum_params_finish (po, res, &err);
  if (!it) {
    wp_transition_return_error (transition, g_steal_pointer (&err));
    return;
  }

  wp_debug_object (self, "enum PortConfig finished on activation");

  /* Update PortConfig info */
  if (!update_audio_port_config_info (self, it)) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "could not find any matching PortConfig param"));
    return;
  }

  wp_info_object (self, "PortConfig info updated on activation");

  wp_debug_object (self, "finish activation");
  wp_object_update_features (WP_OBJECT (self), WP_STREAM_FEATURE_ACTIVE, 0);
}

static void
audio_stream_enum_port_config_on_activation (WpAudioStream *self,
    WpTransition *transition)
{
  g_autoptr (WpNode) node = NULL;

  /* Get stream node */
  node = wp_stream_get_node (WP_STREAM (self));
  if (!node) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "stream node is not valid on activation"));
    return;
  }

  /* Enum PortConfig to see if we need to configure ports or not */
  wp_debug_object (self, "enumerating PortConfig params on activation...");
  wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node), "PortConfig", NULL,
      NULL, (GAsyncReadyCallback) on_audio_node_port_config_done,
      g_object_ref (transition));
}

static void
on_audio_stream_enum_format_done (WpPipewireObject *po, GAsyncResult *res,
    WpTransition *t)
{
  g_autoptr (WpTransition) transition = t;
  WpAudioStream *self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (GError) err = NULL;

  /* Check for errors */
  it = wp_pipewire_object_enum_params_finish (po, res, &err);
  if (!it) {
    wp_transition_return_error (transition, g_steal_pointer (&err));
    return;
  }

  wp_debug_object (self, "enum EnumFormat finished on activation");

  if (!update_audio_enum_format_info (self, it)) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "no audio format found on activation"));
    return;
  }

  wp_info_object (self, "EnumFormat info update on activation");

  audio_stream_enum_port_config_on_activation (self, transition);
}

static void
on_audio_stream_enum_port_config_format_done (WpPipewireObject *po,
    GAsyncResult *res, WpTransition *t)
{
  g_autoptr (WpTransition) transition = t;
  WpAudioStream *self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (GError) err = NULL;

  /* Check for errors */
  it = wp_pipewire_object_enum_params_finish (po, res, &err);
  if (!it) {
    wp_transition_return_error (transition, g_steal_pointer (&err));
    return;
  }

  wp_debug_object (self, "enum EnumPortConfig finished on activation");

  if (!update_audio_enum_port_config_format_info (self, it)) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "no audio format found on activation"));
    return;
  }

  wp_info_object (self, "EnumPortConfig formats update on activation");

  audio_stream_enum_port_config_on_activation (self, transition);
}

static gboolean
node_has_readable_param (WpNode *node, const gchar *param_name)
{
  g_autoptr (GVariant) info =
      wp_pipewire_object_get_param_info (WP_PIPEWIRE_OBJECT (node));
  const gchar *flags = NULL;

  return info && g_variant_lookup (info, param_name, "&s", &flags) &&
      strchr (flags, 'r') != NULL;
}

static void
wp_audio_stream_enable_active (WpStream *stream, WpTransition *transition)
{
  WpAudioStream *self = WP_AUDIO_STREAM (stream);
  g_autoptr (WpNode) node = NULL;

  /* Get stream node */
  node = wp_stream_get_node (WP_STREAM (self));
  if (!node) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "stream node is not valid on activation"));
    return;
  }

  /* Listen for PortConfig params changed */
  self->params_changed_sigid = g_signal_connect_object (node,
      "params-changed", (GCallback) on_audio_node_params_changed, self, 0);

  /* Listen for rejected PortConfig requests */
  self->error_sigid = g_signal_connect_object (node,
      "error", (GCallback) on_audio_node_error, self, 0);

  /* Enum EnumFormat to see what formats the stream supports. A node with
   * several streams has no EnumFormat; it advertises the formats of each
   * stream in the internalFormat of its EnumPortConfig instead */
  if (node_has_readable_param (node, "EnumFormat")) {
    wp_debug_object (self, "enumerating EnumFormat params on activation...");
    wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node), "EnumFormat",
        NULL, NULL, (GAsyncReadyCallback) on_audio_stream_enum_format_done,
        g_object_ref (transition));
  } else {
    wp_debug_object (self,
        "enumerating EnumPortConfig params on activation...");
    wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node),
        "EnumPortConfig", NULL, NULL,
        (GAsyncReadyCallback) on_audio_stream_enum_port_config_format_done,
        g_object_ref (transition));
  }
}

static void
wp_audio_stream_disable_active (WpStream *stream)
{
  WpAudioStream *self = WP_AUDIO_STREAM (stream);
  g_autoptr (WpNode) node = NULL;

  /* Activation info */
  node = wp_stream_get_node (WP_STREAM (self));
  if (node && self->params_changed_sigid) {
    g_signal_handler_disconnect (node, self->params_changed_sigid);
    self->params_changed_sigid = 0;
  }
  if (node && self->error_sigid) {
    g_signal_handler_disconnect (node, self->error_sigid);
    self->error_sigid = 0;
  }

  /* EnumFormat info */
  g_ptr_array_set_size (self->supported_raw_formats, 0);
  g_ptr_array_set_size (self->supported_enc_formats, 0);

  /* PortConfig info */
  self->curr_mode = SPA_PARAM_PORT_CONFIG_MODE_none;
  self->curr_monitor = TRUE;
  self->curr_control = FALSE;
  g_clear_pointer (&self->curr_format, wp_spa_pod_unref);
  self->prev_mode = SPA_PARAM_PORT_CONFIG_MODE_none;
  self->prev_monitor = TRUE;
  self->prev_control = FALSE;
  g_clear_pointer (&self->prev_format, wp_spa_pod_unref);
  audio_stream_update_ports_state (self);
}

static guint
wp_audio_stream_get_supported_raw_format_count (WpStream *stream)
{
  WpAudioStream *self = WP_AUDIO_STREAM (stream);
  return self->supported_raw_formats->len;
}

static guint
wp_audio_stream_get_supported_enc_format_count (WpStream *stream)
{
  WpAudioStream *self = WP_AUDIO_STREAM (stream);
  return self->supported_enc_formats->len;
}

static WpIterator *
wp_audio_stream_new_supported_raw_format_iterator (WpStream *stream)
{
  WpAudioStream *self = WP_AUDIO_STREAM (stream);
  return wp_iterator_new_ptr_array (g_ptr_array_ref (self->supported_raw_formats),
      WP_TYPE_SPA_POD);
}

static WpIterator *
wp_audio_stream_new_supported_enc_format_iterator (WpStream *stream)
{
  WpAudioStream *self = WP_AUDIO_STREAM (stream);
  return wp_iterator_new_ptr_array (g_ptr_array_ref (self->supported_enc_formats),
      WP_TYPE_SPA_POD);
}

static gboolean
wp_audio_stream_is_exclusive (WpStream *stream)
{
  WpAudioStream *self = WP_AUDIO_STREAM (stream);

  /* Since audio streams can be mixed, we only mark them as exclusive if
   * they are configured in passthrough */
  return self->curr_mode == SPA_PARAM_PORT_CONFIG_MODE_passthrough;
}

static gboolean
wp_audio_stream_is_compatible (WpStream *stream, WpStream *other_stream,
    gboolean *encoded_only)
{
  WpAudioStream *self, *other;

  if (encoded_only)
    *encoded_only = FALSE;

  /* Make sure the stream types are the same */
  if (G_OBJECT_TYPE (stream) != G_OBJECT_TYPE (other_stream))
    return FALSE;

  self = WP_AUDIO_STREAM (stream);
  other = WP_AUDIO_STREAM (other_stream);

  /* Set encoded only if at least one stream does not support raw formats */
  if (encoded_only)
    *encoded_only = self->supported_raw_formats->len == 0 ||
        other->supported_raw_formats->len == 0;

  /* Since audio streams are always adapters and can be configured in DSP mode,
   * both streams are always compatible if they support raw formats */
  if (self->supported_raw_formats->len > 0 &&
      other->supported_raw_formats->len > 0)
    return TRUE;

  /* Otherwise check if both streams share a common encoded format */
  for (guint i = 0; i < self->supported_enc_formats->len; i++) {
    WpSpaPod *pod = g_ptr_array_index (self->supported_enc_formats, i);
    for (guint j = 0; j < other->supported_enc_formats->len; j++) {
      WpSpaPod *other_pod = g_ptr_array_index (other->supported_enc_formats, j);
      if (wp_spa_pod_filter (pod, other_pod))
        return TRUE;
    }
  }

  return FALSE;
}

static void
wp_audio_stream_ports_changed (WpStream *stream)
{
  WpAudioStream *self = WP_AUDIO_STREAM (stream);

  wp_info_object (self, "audio ports changed: count=%u",
      wp_stream_get_ports_count (stream));

  audio_stream_check_ports_configured (self);
}

static void
wp_audio_stream_finalize (GObject * object)
{
  WpAudioStream *self = WP_AUDIO_STREAM (object);

  g_clear_pointer (&self->supported_raw_formats, g_ptr_array_unref);
  g_clear_pointer (&self->supported_enc_formats, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_audio_stream_parent_class)->finalize (object);
}

static void
wp_audio_stream_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  WpAudioStream *self = WP_AUDIO_STREAM (object);

  switch (prop_id) {
    case AUDIO_STREAM_PROP_PORTS_STATE:
      g_value_set_enum (value, self->ports_state);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
wp_audio_stream_class_init (WpAudioStreamClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpStreamClass *stream_class = (WpStreamClass *) klass;

  object_class->finalize = wp_audio_stream_finalize;
  object_class->get_property = wp_audio_stream_get_property;

  stream_class->enable_active = wp_audio_stream_enable_active;
  stream_class->disable_active = wp_audio_stream_disable_active;
  stream_class->is_compatible = wp_audio_stream_is_compatible;
  stream_class->get_supported_raw_format_count = wp_audio_stream_get_supported_raw_format_count;
  stream_class->get_supported_enc_format_count = wp_audio_stream_get_supported_enc_format_count;
  stream_class->new_supported_raw_format_iterator = wp_audio_stream_new_supported_raw_format_iterator;
  stream_class->new_supported_enc_format_iterator = wp_audio_stream_new_supported_enc_format_iterator;
  stream_class->is_exclusive = wp_audio_stream_is_exclusive;
  stream_class->ports_changed = wp_audio_stream_ports_changed;

  audio_stream_signals[AUDIO_STREAM_SIGNAL_PORTS_CONFIGURED] = g_signal_new (
      "ports-configured", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  audio_stream_signals[AUDIO_STREAM_SIGNAL_PORTS_ERROR] = g_signal_new (
      "ports-error", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  audio_stream_props[AUDIO_STREAM_PROP_PORTS_STATE] = g_param_spec_enum (
      "ports-state", "Ports State", "The state of the stream ports",
      WP_TYPE_AUDIO_STREAM_PORTS_STATE,
      WP_AUDIO_STREAM_PORTS_STATE_UNCONFIGURED,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, AUDIO_STREAM_N_PROPS,
      audio_stream_props);
}

WpAudioStream *
wp_audio_stream_new (WpNode *node, WpProperties *props, WpDirection dir,
    const gchar *port_group)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpProperties) properties = NULL;

  core = wp_object_get_core (WP_OBJECT (node));
  properties = wp_properties_ensure_unique_owner (props);

  return g_object_new (WP_TYPE_AUDIO_STREAM,
      "core", core,
      "node", node,
      "properties", properties,
      "direction", dir,
      "port-group", port_group,
      NULL);
}

/**
 * \brief Gets the stream ports format
 *
 * \ingroup wpstream
 * \param self the stream
 * \returns (transfer full): the stream port's format
 */
WpSpaPod *
wp_audio_stream_get_ports_format (WpAudioStream * self)
{
  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self), NULL);

  return self->curr_format ? wp_spa_pod_ref (self->curr_format) : NULL;
}

/**
 * \brief Gets the stream ports mode
 *
 * \ingroup wpstream
 * \param self the stream
 * \returns (transfer none): the stream port's mode
 */
const gchar *
wp_audio_stream_get_ports_mode (WpAudioStream * self)
{
  WpSpaIdTable table;
  WpSpaIdValue id_value;

  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self), NULL);

  table = wp_spa_id_table_from_name ("Spa:Enum:ParamPortConfigMode");
  g_return_val_if_fail (table, NULL);

  id_value = wp_spa_id_table_find_value (table, self->curr_mode);
  g_return_val_if_fail (id_value, NULL);

  return wp_spa_id_value_short_name (id_value);
}

/*!
 * \brief Checks whether the monitor ports are enabled or not.
 *
 * \ingroup wpstream
 * \param self the stream
 * \returns TRUE if the monitor ports are enabled, FALSE otherwise
 */
gboolean
wp_audio_stream_has_monitor_ports_enabled (WpAudioStream * self)
{
  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self), FALSE);

  return self->curr_monitor;
}

/*!
 * \brief Checks whether the control ports are enabled or not.
 *
 * \ingroup wpstream
 * \param self the stream
 * \returns TRUE if the control ports are enabled, FALSE otherwise
 */
gboolean
wp_audio_stream_has_control_ports_enabled (WpAudioStream * self)
{
  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self), FALSE);

  return self->curr_control;
}

/*!
 * \brief Gets the state of the stream ports.
 *
 * The state is WP_AUDIO_STREAM_PORTS_STATE_CONFIGURING from the moment a port
 * configuration is requested until its ports exist or it fails, which is when
 * the "ports-configured" or "ports-error" signal is emitted. A change of the
 * state is notified through the "ports-state" property.
 *
 * \ingroup wpstream
 * \param self the stream
 * \returns the state of the stream ports
 */
WpAudioStreamPortsState
wp_audio_stream_get_ports_state (WpAudioStream * self)
{
  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self),
      WP_AUDIO_STREAM_PORTS_STATE_UNCONFIGURED);

  return self->ports_state;
}

static gboolean
wp_audio_stream_configure_ports_internal (WpAudioStream * self,
    WpSpaPod *format, guint32 mode_id, gboolean monitor, gboolean control)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpSpaPod) ports_format = NULL;
  gint channels = 0;
  const gchar *port_group = NULL;
  g_autoptr (WpSpaPod) param = NULL;
  guint32 fmt = 0;
  gint rate = 0;
  g_autoptr (WpSpaPodBuilder) b = NULL;
  gint seq;

  /* Get the core and the node */
  core = wp_object_get_core (WP_OBJECT (self));
  node = wp_stream_get_node (WP_STREAM (self));
  if (!node) {
    wp_warning_object (self, "cannot configure ports without valid node");
    return FALSE;
  }

  /* Ensure DSP format if mode is DSP */
  if (mode_id == SPA_PARAM_PORT_CONFIG_MODE_dsp) {
    ports_format = build_audio_dsp_format (self, format);
    if (!ports_format) {
      wp_warning_object (self, "cannot set use DSP mode with provided format");
      return FALSE;
    }
  } else {
    ports_format = wp_spa_pod_ref (format);
  }

  /* Log some info */
  wp_spa_pod_get_object (ports_format, NULL,
      "format", "I", &fmt,
      "channels", "i", &channels,
      "rate", "i", &rate,
      NULL);
  wp_info_object (self,
      "setting PortConfig: mode:%u fmt:%d channels:%d rate=%d mon=%d ctrl=%d",
      mode_id, fmt, channels, rate, monitor, control);

  /* Skip reconfiguring if the same values are requested */
  if (self->curr_format) {
    guint32 curr_fmt = 0;
    gint curr_channels = 0;
    gint curr_rate = 0;
    wp_spa_pod_get_object (self->curr_format, NULL,
        "format", "I", &curr_fmt,
        "channels", "i", &curr_channels,
        "rate", "i", &curr_rate,
        NULL);
    wp_info_object (self,
        "current PortConfig: mode:%u fmt:%d channels:%d rate=%d mon=%d ctrl=%d",
        self->curr_mode, curr_fmt, curr_channels, curr_rate, self->curr_monitor,
        self->curr_control);
    if (mode_id == self->curr_mode &&
        monitor == self->curr_monitor &&
        control == self->curr_control &&
        fmt == curr_fmt && channels == curr_channels && rate == curr_rate) {
      wp_info_object (self,
        "ports already configured with requested values, not configuring");
      return FALSE;
    }
  }
  wp_info_object (self, "configuring ports with different PortConfig");

  /* Ensure the node is suspended, since setting the PortConfig param only
   * works if the node is not running. Do not decide this from the node's
   * state: that is the last state the server reported, and a node started by
   * links that were activated a moment ago still reads as suspended here.
   * Suspending a node that is already suspended is a no-op. */
  wp_node_send_command (WP_NODE (node), "Suspend");

  /* Build the PortConfig param */
  b = wp_spa_pod_builder_new_object (
      "Spa:Pod:Object:Param:PortConfig", "PortConfig");
  wp_spa_pod_builder_add (b,
      "direction", "I", wp_stream_get_direction (WP_STREAM (self)),
      "mode",      "I", mode_id,
      "monitor",   "b", monitor,
      "control",   "b", control,
      NULL);
  port_group = wp_stream_get_port_group (WP_STREAM (self));
  if (port_group)
    /* the raw id of the PortConfig "group" key, see the comment in
       update_audio_port_config_info() */
    wp_spa_pod_builder_add (b, "id-00000006", "s", port_group, NULL);
  if (mode_id != SPA_PARAM_PORT_CONFIG_MODE_passthrough)
    wp_spa_pod_builder_add (b, "format", "P", ports_format, NULL);
  param = wp_spa_pod_builder_end (b);

  /* Set port config. The server may still reject the request; that is
   * reported later, on the node's "error" signal */
  seq = wp_pipewire_object_set_param_seq (WP_PIPEWIRE_OBJECT (node),
      "PortConfig", 0, g_steal_pointer (&param));
  if (seq < 0) {
    g_autofree gchar *msg = g_strdup_printf (
        "could not request the port configuration: %s", spa_strerror (seq));
    audio_stream_notify_ports_error (self, msg);
    return FALSE;
  }

  /* Remember the configuration we are replacing, to restore it if the
   * request is rejected */
  g_clear_pointer (&self->prev_format, wp_spa_pod_unref);
  self->prev_format = self->curr_format ?
      wp_spa_pod_ref (self->curr_format) : NULL;
  self->prev_mode = self->curr_mode;
  self->prev_monitor = self->curr_monitor;
  self->prev_control = self->curr_control;

  /* Update configuration */
  g_clear_pointer (&self->curr_format, wp_spa_pod_unref);
  self->curr_format = wp_spa_pod_ref (ports_format);
  self->curr_mode = mode_id;
  self->curr_monitor = monitor;
  self->curr_control = control;

  /* 'ports-configured' is emitted once the request has been handled and the
   * new ports exist; see on_ports_config_synced() */
  self->pending_ports_config_seq = seq;
  self->pending_ports_config_synced = FALSE;
  audio_stream_set_ports_state (self, WP_AUDIO_STREAM_PORTS_STATE_CONFIGURING);
  wp_core_sync_closure (core, NULL, g_cclosure_new_object (
      G_CALLBACK (on_ports_config_synced), G_OBJECT (self)));

  return TRUE;
}

/*!
 * \brief Configures the stream ports.
 *
 * Note that this won't re-configure the ports if they are already configured
 * with the same format, mode, monitor and control values, in which case FALSE
 * is returned. Only TRUE is returned when the configuration request was sent,
 * in which case the user has to handle the 'ports-configured' and 'ports-error'
 * signals to know if the pending configuration was successfully or not.
 *
 * \ingroup wpstream
 * \param self the stream
 * \param format (transfer none): the format
 * \param mode: the mode
 * \param monitor whether to enable monitor ports or not
 * \param control whether to enable control ports or not
 * \returns TRUE if the ports will be re-configured, FALSE otherwise
 */
gboolean
wp_audio_stream_configure_ports (WpAudioStream * self, WpSpaPod *format,
    const gchar *mode, gboolean monitor, gboolean control)
{
  WpSpaIdTable mode_table;
  WpSpaIdValue mode_val;
  guint32 mode_id;

  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self), FALSE);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
      WP_STREAM_FEATURE_ACTIVE, FALSE);
  g_return_val_if_fail (format, FALSE);
  g_return_val_if_fail (mode, FALSE);

  /* Get the mode Id */
  mode_table = wp_spa_id_table_from_name ("Spa:Enum:ParamPortConfigMode");
  g_return_val_if_fail (mode_table, FALSE);
  mode_val = wp_spa_id_table_find_value_from_short_name (mode_table, mode);
  if (!mode_val) {
    wp_warning_object (self, "mode %s is not valid", mode);
    return FALSE;
  }
  mode_id = wp_spa_id_value_number (mode_val);

  /* Configure */
  return wp_audio_stream_configure_ports_internal (self, format, mode_id,
      monitor, control);
}

/* The mono setting applies to hardware audio sinks only, which is the same set
 * of streams that node/configure-ports.lua reconfigures when it changes */
static gboolean
wp_audio_stream_is_hw_audio_sink (WpAudioStream * self)
{
  g_autoptr (WpProperties) props = NULL;
  const gchar *factory;

  props = wp_stream_get_properties (WP_STREAM (self));
  factory = wp_properties_get (props, PW_KEY_FACTORY_NAME);

  return !g_strcmp0 (factory, "api.alsa.pcm.sink") ||
      !g_strcmp0 (factory, "api.bluez5.a2dp.sink");
}

/* Resolves the port configuration that would be applied for the given
 * properties, from the settings, the properties and the stream's supported
 * formats. Kept separate so that wp_audio_stream_is_monitorable()
 * answers with the same rules that are used to configure the ports. */
static void
wp_audio_stream_resolve_ports_config (WpAudioStream * self, WpCore * core,
    WpProperties * properties, guint32 * mode, gboolean * monitor_ports,
    gboolean * control_port, gboolean * mono_only, gboolean * encoded_only)
{
  g_autoptr (WpSettings) settings = NULL;
  gboolean no_dsp;
  const gchar *str;

  /* Init port configuration values to default */
  *monitor_ports = self->curr_monitor;
  *control_port = self->curr_control;
  no_dsp = FALSE;
  *mono_only = FALSE;
  *encoded_only = FALSE;

  /* Use settings values if any */
  settings = wp_settings_find (core, NULL);
  if (settings) {
    g_autoptr (WpSpaJson) val = NULL;
    val = wp_settings_get (settings, "node.features.audio.monitor-ports");
    if (val)
      wp_spa_json_parse_boolean (val, monitor_ports);
    g_clear_pointer (&val, wp_spa_json_unref);

    val = wp_settings_get (settings, "node.features.audio.control-port");
    if (val)
      wp_spa_json_parse_boolean (val, control_port);
    g_clear_pointer (&val, wp_spa_json_unref);

    val = wp_settings_get (settings, "node.features.audio.no-dsp");
    if (val)
      wp_spa_json_parse_boolean (val, &no_dsp);
    g_clear_pointer (&val, wp_spa_json_unref);

    if (wp_audio_stream_is_hw_audio_sink (self)) {
      val = wp_settings_get (settings, "node.features.audio.mono");
      if (val)
        wp_spa_json_parse_boolean (val, mono_only);
      g_clear_pointer (&val, wp_spa_json_unref);
    }
  }

  /* Use property values if any */
  if (properties) {
    str = wp_properties_get (properties, "stream.monitor-ports");
    if (str)
      *monitor_ports = spa_atob (str);
    str = wp_properties_get (properties, "stream.control-port");
    if (str)
      *control_port = spa_atob (str);
    str = wp_properties_get (properties, "stream.no-dsp");
    if (str)
      no_dsp = spa_atob (str);
    str = wp_properties_get (properties, "stream.mono-only");
    if (str)
      *mono_only = spa_atob (str);
    str = wp_properties_get (properties, "stream.encoded-only");
    if (str)
      *encoded_only = spa_atob (str);
  }

  /* Use passthrough mode if no-dsp or encoded-only is enabled, otherwise
   * default to DSP */
  *mode = (no_dsp || *encoded_only) ? SPA_PARAM_PORT_CONFIG_MODE_passthrough :
      SPA_PARAM_PORT_CONFIG_MODE_dsp;

  /* Force DSP mode if the stream supports RAW formats and mono_only is
   * enabled */
  if (*mono_only && self->supported_raw_formats->len > 0)
    *mode = SPA_PARAM_PORT_CONFIG_MODE_dsp;

  /* Force passthrough mode and encoded_only if the stream only supports encoded
   * formats */
  if (self->supported_enc_formats->len > 0 &&
      self->supported_raw_formats->len == 0) {
    *mode = SPA_PARAM_PORT_CONFIG_MODE_passthrough;
    *encoded_only = TRUE;
  }
}

/*!
 * \brief Checks whether the stream would have monitor ports if its ports were
 * configured now in order to link it with the given peer.
 *
 * Unlike wp_audio_stream_has_monitor_ports_enabled(), which reports the
 * monitor flag of the PortConfig that was last requested, this resolves the
 * settings, the stream's supported formats and the resulting port config mode
 * the same way wp_audio_stream_configure_ports_with_props() does. It is
 * therefore usable before the ports have been configured at all, which is the
 * case while policy scripts are still selecting a link target.
 *
 * \ingroup wpstream
 * \param self the stream
 * \param peer (transfer none) (nullable): the stream that self would be linked
 *   with, or NULL to ask about the default configuration
 * \returns TRUE if configuring the ports now would enable monitor ports,
 *   FALSE otherwise
 */
gboolean
wp_audio_stream_is_monitorable (WpAudioStream * self,
    WpAudioStream * peer)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpProperties) props = NULL;
  guint32 mode;
  gboolean monitor_ports, control_port, mono_only, encoded_only;

  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self), FALSE);
  g_return_val_if_fail (!peer || WP_IS_AUDIO_STREAM (peer), FALSE);

  core = wp_object_get_core (WP_OBJECT (self));
  g_return_val_if_fail (core, FALSE);

  /* A link whose streams are only compatible in an encoded format configures
   * them in passthrough mode, which has no monitor ports. This is the only
   * property wp_audio_stream_link_setup_ports() passes down, see there. */
  if (peer) {
    gboolean link_encoded_only = FALSE;
    wp_stream_is_compatible (WP_STREAM (self), WP_STREAM (peer),
        &link_encoded_only);
    if (link_encoded_only) {
      props = wp_properties_new_empty ();
      wp_properties_set (props, "stream.encoded-only", "true");
    }
  }

  wp_audio_stream_resolve_ports_config (self, core, props, &mode,
      &monitor_ports, &control_port, &mono_only, &encoded_only);

  /* Monitor ports only exist in DSP mode */
  return monitor_ports && mode == SPA_PARAM_PORT_CONFIG_MODE_dsp;
}

/*!
 * \brief Configures the ports given a set of properties. If no properties are
 * given, the default configuration will be used.
 *
 * \ingroup wpstream
 * \param self the stream
 * \param props (transfer full) (nullable): the properties, or NULL to use the
 * default configuration.
 * \returns TRUE if the ports will be re-configured, FALSE otherwise
 */
gboolean
wp_audio_stream_configure_ports_with_props (WpAudioStream * self,
    WpProperties *props)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpProperties) properties = props;
  gboolean monitor_ports, control_port, mono_only, encoded_only;
  g_autoptr (WpSpaPod) format = NULL;
  guint32 mode;

  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self), FALSE);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
      WP_STREAM_FEATURE_ACTIVE, FALSE);

  /* Get core */
  core = wp_object_get_core (WP_OBJECT (self));
  g_return_val_if_fail (core, FALSE);

  wp_audio_stream_resolve_ports_config (self, core, properties, &mode,
      &monitor_ports, &control_port, &mono_only, &encoded_only);

  /* Select format */
  if (encoded_only) {
    g_return_val_if_fail (self->supported_enc_formats->len > 0, FALSE);
    format = wp_spa_pod_ref (g_ptr_array_index (self->supported_enc_formats, 0));
  } else if (self->curr_format && self->supported_raw_formats->len > 0) {
    WpSpaPod *best = g_ptr_array_index (self->supported_raw_formats, 0);
    format = mono_only ? build_audio_mono_format (self->curr_format) :
        wp_spa_pod_ref (best);
  } else if (self->curr_format) {
    format = mono_only ? build_audio_mono_format (self->curr_format) :
        wp_spa_pod_ref (self->curr_format);
  } else if (self->supported_raw_formats->len > 0) {
    WpSpaPod *best = g_ptr_array_index (self->supported_raw_formats, 0);
    format = mono_only ? build_audio_mono_format (best) : wp_spa_pod_ref (best);
  } else {
    g_return_val_if_reached (FALSE);
  }

  /* Configure ports */
  return wp_audio_stream_configure_ports_internal (self, format, mode,
      monitor_ports, control_port);
}

/*!
 * \brief Configures the ports using the same configuration as another stream.
 *
 * \ingroup wpstream
 * \param self the stream
 * \param stream (transfer none): the other stream
 * \returns TRUE if the ports will be re-configured, FALSE otherwise
 */
gboolean
wp_audio_stream_configure_ports_with_stream (WpAudioStream * self,
    WpAudioStream *stream)
{
  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self), FALSE);
  g_return_val_if_fail (WP_IS_AUDIO_STREAM (stream), FALSE);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
      WP_STREAM_FEATURE_ACTIVE, FALSE);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (stream)) &
      WP_STREAM_FEATURE_ACTIVE, FALSE);
  g_return_val_if_fail (stream->curr_format, FALSE);

  return wp_audio_stream_configure_ports_internal (self, stream->curr_format,
      stream->curr_mode, stream->curr_monitor, stream->curr_control);
}

/* TRUE if the channels of the format have no positions, or AUX ones, like the
 * channels of a pro audio device */
static gboolean
is_unpositioned_format (WpSpaPod *format)
{
  struct spa_audio_info_raw info = { 0, };
  struct spa_pod *position = NULL;
  guint32 n_pos;

  if (spa_pod_parse_object (wp_spa_pod_get_spa_pod (format),
      SPA_TYPE_OBJECT_Format, NULL,
      SPA_FORMAT_AUDIO_channels, SPA_POD_OPT_Int (&info.channels),
      SPA_FORMAT_AUDIO_position, SPA_POD_OPT_Pod (&position)) < 0)
    return FALSE;

  if (!position)
    return TRUE;

  n_pos = spa_pod_copy_array (position, SPA_TYPE_Id, info.position,
      SPA_N_ELEMENTS (info.position));
  if (n_pos == 0)
    return TRUE;

  for (guint32 i = 0; i < SPA_MIN (n_pos, info.channels); i++) {
    if (info.position[i] >= SPA_AUDIO_CHANNEL_START_Aux &&
        info.position[i] <= SPA_AUDIO_CHANNEL_LAST_Aux)
      return TRUE;
  }

  return FALSE;
}

static gboolean
wp_audio_stream_is_dont_remix (WpAudioStream * self)
{
  g_autoptr (WpProperties) props = wp_stream_get_properties (WP_STREAM (self));
  const gchar *str = wp_properties_get (props, "stream.dont-remix");

  return str && spa_atob (str);
}

/*!
 * \brief Configures the ports to link them with the ports of another stream,
 * which must have its ports configured already.
 *
 * The ports take the same configuration as the other stream, like
 * wp_audio_stream_configure_ports_with_stream() does, unless the other stream
 * has DSP ports with unpositioned channels, such as a pro audio device, or
 * \a self has the "stream.dont-remix" property set. Remixing into AUX channels
 * puts the channels of \a self in no meaningful place, so in that case the DSP
 * ports of \a self keep the channels they have, or those of its own best
 * format if it has no DSP ports yet, and only the monitor and control flags are
 * taken from the other stream.
 *
 * \ingroup wpstream
 * \param self the stream
 * \param peer (transfer none): the stream to link with
 * \returns TRUE if the ports will be re-configured, FALSE otherwise
 */
gboolean
wp_audio_stream_configure_ports_for_peer (WpAudioStream * self,
    WpAudioStream *peer)
{
  WpSpaPod *format;

  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self), FALSE);
  g_return_val_if_fail (WP_IS_AUDIO_STREAM (peer), FALSE);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
      WP_STREAM_FEATURE_ACTIVE, FALSE);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (peer)) &
      WP_STREAM_FEATURE_ACTIVE, FALSE);
  g_return_val_if_fail (peer->curr_format, FALSE);

  if (peer->curr_mode != SPA_PARAM_PORT_CONFIG_MODE_dsp ||
      (!is_unpositioned_format (peer->curr_format) &&
       !wp_audio_stream_is_dont_remix (self)))
    return wp_audio_stream_configure_ports_with_stream (self, peer);

  if (self->curr_mode == SPA_PARAM_PORT_CONFIG_MODE_dsp && self->curr_format)
    format = self->curr_format;
  else if (self->supported_raw_formats->len > 0)
    format = g_ptr_array_index (self->supported_raw_formats, 0);
  else
    return wp_audio_stream_configure_ports_with_stream (self, peer);

  wp_info_object (self, "keeping own channel layout to link with %p", peer);
  return wp_audio_stream_configure_ports_internal (self, format,
      SPA_PARAM_PORT_CONFIG_MODE_dsp, peer->curr_monitor, peer->curr_control);
}


/* Video Stream */

/*!
 * \struct WpVideoStream
 *
 * A WpStream that carries video. Video nodes have a single port per stream and
 * WirePlumber does not configure their ports, so this class only tracks the
 * raw formats the node supports.
 *
 * An input video stream is exclusive: video cannot be mixed, so it accepts one
 * peer at a time. An output video stream can feed as many peers as ask for it.
 */
struct _WpVideoStream
{
  WpStream parent;

  /* EnumFormat info */
  GPtrArray *supported_raw_formats;

  /* Activation info */
  gulong params_changed_sigid;
};

G_DEFINE_TYPE (WpVideoStream, wp_video_stream, WP_TYPE_STREAM);

static void
wp_video_stream_init (WpVideoStream * self)
{
  self->supported_raw_formats = g_ptr_array_new_with_free_func (
      (GDestroyNotify)wp_spa_pod_unref);
  self->params_changed_sigid = 0;
}

static gboolean
update_video_enum_format_info (WpVideoStream *self, WpIterator *it)
{
  g_auto (GValue) val = G_VALUE_INIT;

  g_ptr_array_set_size (self->supported_raw_formats, 0);

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSpaPod *pod = g_value_get_boxed (&val);
    uint32_t mtype, msubtype;

    if (!wp_spa_pod_is_object (pod) || !wp_spa_pod_get_object (pod, NULL,
            "mediaType", "I", &mtype,
            "mediaSubtype", "I", &msubtype,
            NULL))
      continue;

    if (mtype != SPA_MEDIA_TYPE_video)
      continue;

    if (msubtype == SPA_MEDIA_SUBTYPE_raw) {
      wp_spa_pod_fixate (pod);
      g_ptr_array_add (self->supported_raw_formats, wp_spa_pod_ref (pod));
    }
  }

  return self->supported_raw_formats->len > 0;
}

static void
on_video_node_enum_format_changed (WpPipewireObject *po, GAsyncResult *res,
    WpStream *s)
{
  g_autoptr (WpVideoStream) self = WP_VIDEO_STREAM (s);
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (GError) err = NULL;

  it = wp_pipewire_object_enum_params_finish (po, res, &err);
  if (!it) {
    wp_warning_object (self, "failed to enum EnumFormat on params changed");
    return;
  }

  wp_debug_object (self, "enum EnumFormat finished on params changed");

  if (!update_video_enum_format_info (self, it)) {
    wp_warning_object (self, "no video format found on params changed");
    return;
  }

  wp_info_object (self, "updated video formats on params changed");
}

static void
on_video_node_params_changed (WpNode * node, const gchar *param_name,
    WpVideoStream *self)
{
  if (g_str_equal (param_name, "EnumFormat")) {
    wp_debug_object (self, "enumerating EnumFormat params on changed...");
    wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node), "EnumFormat",
        NULL, NULL, (GAsyncReadyCallback) on_video_node_enum_format_changed,
        g_object_ref (self));
  }
}

static void
on_video_stream_enum_format_done (WpPipewireObject *po, GAsyncResult *res,
    WpTransition *t)
{
  g_autoptr (WpTransition) transition = t;
  WpVideoStream *self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (GError) err = NULL;

  it = wp_pipewire_object_enum_params_finish (po, res, &err);
  if (!it) {
    wp_transition_return_error (transition, g_steal_pointer (&err));
    return;
  }

  wp_debug_object (self, "enum EnumFormat finished on activation");

  if (!update_video_enum_format_info (self, it)) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "no video format found on activation"));
    return;
  }

  wp_debug_object (self, "finish activation");
  wp_object_update_features (WP_OBJECT (self), WP_STREAM_FEATURE_ACTIVE, 0);
}

static void
wp_video_stream_enable_active (WpStream *stream, WpTransition *transition)
{
  WpVideoStream *self = WP_VIDEO_STREAM (stream);
  g_autoptr (WpNode) node = NULL;

  node = wp_stream_get_node (WP_STREAM (self));
  if (!node) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "stream node is not valid on activation"));
    return;
  }

  self->params_changed_sigid = g_signal_connect_object (node,
      "params-changed", (GCallback) on_video_node_params_changed, self, 0);

  wp_debug_object (self, "enumerating EnumFormat params on activation...");
  wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node), "EnumFormat", NULL,
      NULL, (GAsyncReadyCallback) on_video_stream_enum_format_done,
      g_object_ref (transition));
}

static void
wp_video_stream_disable_active (WpStream *stream)
{
  WpVideoStream *self = WP_VIDEO_STREAM (stream);
  g_autoptr (WpNode) node = NULL;

  node = wp_stream_get_node (WP_STREAM (self));
  if (node && self->params_changed_sigid) {
    g_signal_handler_disconnect (node, self->params_changed_sigid);
    self->params_changed_sigid = 0;
  }

  g_ptr_array_set_size (self->supported_raw_formats, 0);
}

static gboolean
wp_video_stream_is_compatible (WpStream *stream, WpStream *other_stream,
    gboolean *encoded_only)
{
  /* Only RAW video formats are supported for now */
  if (encoded_only)
    *encoded_only = FALSE;

  /* Make sure the stream types are the same */
  if (G_OBJECT_TYPE (stream) != G_OBJECT_TYPE (other_stream))
    return FALSE;

  return TRUE;
}

static guint
wp_video_stream_get_supported_raw_format_count (WpStream *stream)
{
  WpVideoStream *self = WP_VIDEO_STREAM (stream);
  return self->supported_raw_formats->len;
}

static guint
wp_video_stream_get_supported_enc_format_count (WpStream *stream)
{
  /* Don't support video encoded streams for now */
  return 0;
}

static WpIterator *
wp_video_stream_new_supported_raw_format_iterator (WpStream *stream)
{
  WpVideoStream *self = WP_VIDEO_STREAM (stream);
  return wp_iterator_new_ptr_array (g_ptr_array_ref (self->supported_raw_formats),
      WP_TYPE_SPA_POD);
}

static WpIterator *
wp_video_stream_new_supported_enc_format_iterator (WpStream *stream)
{
  /* Video streams don't support encoded formats */
  return wp_iterator_new_ptr_array (
      g_ptr_array_new_with_free_func ((GDestroyNotify) wp_spa_pod_unref),
      WP_TYPE_SPA_POD);
}

static gboolean
wp_video_stream_is_exclusive (WpStream *stream)
{
  /* Video streams cannot be "mixed" like audio streams, so a stream that
   * receives video can only be linked with one other stream. A stream that
   * sends video can feed as many as they want */
  return wp_stream_get_direction (stream) == WP_DIRECTION_INPUT;
}

static void
wp_video_stream_finalize (GObject * object)
{
  WpVideoStream *self = WP_VIDEO_STREAM (object);

  g_clear_pointer (&self->supported_raw_formats, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_video_stream_parent_class)->finalize (object);
}

static void
wp_video_stream_class_init (WpVideoStreamClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpStreamClass *stream_class = (WpStreamClass *) klass;

  object_class->finalize = wp_video_stream_finalize;

  stream_class->enable_active = wp_video_stream_enable_active;
  stream_class->disable_active = wp_video_stream_disable_active;
  stream_class->is_compatible = wp_video_stream_is_compatible;
  stream_class->get_supported_raw_format_count = wp_video_stream_get_supported_raw_format_count;
  stream_class->get_supported_enc_format_count = wp_video_stream_get_supported_enc_format_count;
  stream_class->new_supported_raw_format_iterator = wp_video_stream_new_supported_raw_format_iterator;
  stream_class->new_supported_enc_format_iterator = wp_video_stream_new_supported_enc_format_iterator;
  stream_class->is_exclusive = wp_video_stream_is_exclusive;
}

WpVideoStream *
wp_video_stream_new (WpNode *node, WpProperties *props, WpDirection dir,
    const gchar *port_group)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpProperties) properties = NULL;

  core = wp_object_get_core (WP_OBJECT (node));
  properties = wp_properties_ensure_unique_owner (props);

  return g_object_new (WP_TYPE_VIDEO_STREAM,
      "core", core,
      "node", node,
      "properties", properties,
      "direction", dir,
      "port-group", port_group,
      NULL);
}
