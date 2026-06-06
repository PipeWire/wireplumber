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

#include "stream.h"
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

static WpObjectInterest*
wp_stream_get_ports_interest (WpStream * self)
{
  WpStreamPrivate *priv = wp_stream_get_instance_private (self);
  WpObjectInterest *interest;
  guint node_id;

  node_id = wp_stream_get_node_bound_id (self);
  interest = wp_object_interest_new (WP_TYPE_PORT,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_NODE_ID, "=u", node_id,
      NULL);
  if (priv->port_group)
    wp_object_interest_add_constraint (interest,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "port.group",
        WP_CONSTRAINT_VERB_EQUALS, g_variant_new_string (priv->port_group));

  return interest;
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
    wp_object_manager_add_interest_full (priv->ports_om,
        wp_stream_get_ports_interest (self));

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

  interest = wp_stream_get_ports_interest (self);

  wp_object_interest_add_constraint (interest,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_PORT_MONITOR,
      WP_CONSTRAINT_VERB_NOT_EQUALS, g_variant_new_string ("true"));
  wp_object_interest_add_constraint (interest,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_PORT_CONTROL,
      WP_CONSTRAINT_VERB_NOT_EQUALS, g_variant_new_string ("true"));
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

  interest = wp_stream_get_ports_interest (self);

  wp_object_interest_add_constraint (interest,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_PORT_MONITOR,
      WP_CONSTRAINT_VERB_EQUALS, g_variant_new_string ("true"));
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

  interest = wp_stream_get_ports_interest (self);

  wp_object_interest_add_constraint (interest,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, PW_KEY_PORT_CONTROL,
      WP_CONSTRAINT_VERB_EQUALS, g_variant_new_string ("true"));
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

  /* Activation info */
  gulong params_changed_sigid;
  gboolean port_config_ready;
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

  /* Activation info */
  self->params_changed_sigid = 0;
  self->port_config_ready = FALSE;
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

    /* Get optional fields */
    wp_spa_pod_get_object (param, NULL,
        "id-00000006", "s", &port_group,
        NULL);

    /* See if the port config belongs to this streams and parse it */
    if (g_strcmp0 (stream_port_group, port_group) == 0) {
      enum spa_param_port_config_mode mode = SPA_PARAM_PORT_CONFIG_MODE_none;
      gboolean monitor = FALSE;
      gboolean control = FALSE;
      g_autoptr (WpSpaPod) format = NULL;

      wp_spa_pod_get_object (param, NULL,
          "mode",    "I", &mode,
          "monitor", "b", &monitor,
          "control", "b", &control,
          "format",  "P", &format,
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

static gboolean
update_audio_enum_format_info (WpAudioStream *self, WpIterator *it)
{
  g_auto (GValue) val = G_VALUE_INIT;

  /* Reset supported formats */
  g_ptr_array_set_size (self->supported_raw_formats, 0);
  g_ptr_array_set_size (self->supported_enc_formats, 0);

  /* Scan formats and find the best available RAW format */
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSpaPod *pod = g_value_get_boxed (&val);
    uint32_t mtype, msubtype;

    /* Skip formats without media type and sub-type */
    if (!wp_spa_pod_is_object (pod) || !wp_spa_pod_get_object (pod, NULL,
            "mediaType", "I", &mtype,
            "mediaSubtype", "I", &msubtype,
            NULL)) {
      continue;
    }

    /* Skip non-audio formats */
    if (mtype != SPA_MEDIA_TYPE_audio)
      continue;

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
          continue;

        if (position == NULL ||
            !spa_pod_copy_array (position, SPA_TYPE_Id, raw_format.position,
                SPA_N_ELEMENTS(raw_format.position)))
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

  /* Sort raw formats by channels (highest first) */
  g_ptr_array_sort (self->supported_raw_formats, compare_raw_formats_by_channels);

  if (self->supported_raw_formats->len > 0)
    wp_info_object (self, "supports RAW audio");

  if (self->supported_enc_formats->len > 0)
    wp_info_object (self, "supports encoded audio");

  return self->supported_raw_formats->len > 0 || self->supported_enc_formats->len > 0;
}

static void
audio_stream_notify_ports_error (WpAudioStream *self, const gchar *msg)
{
  wp_warning_object (self, "%s", msg);
  g_signal_emit (self, audio_stream_signals[AUDIO_STREAM_SIGNAL_PORTS_ERROR], 0,
      msg);
}

static void
on_audio_node_port_config_changed (WpPipewireObject *po, GAsyncResult *res,
    WpStream *s)
{
  g_autoptr (WpAudioStream) self = WP_AUDIO_STREAM (s);
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (GError) err = NULL;
  guint n_ports;

  /* Check for errors */
  it = wp_pipewire_object_enum_params_finish (po, res, &err);
  if (!it) {
    audio_stream_notify_ports_error (self,
        "failed to enum PortConfig on params changed");
    return;
  }

  wp_debug_object (self, "enum PortConfig finished on changed");

  /* Update PortConfig info */
  if (!update_audio_port_config_info (self, it)) {
    audio_stream_notify_ports_error (self,
        "could not find any matching PortConfig param");
    return;
  }

  self->port_config_ready = TRUE;
  wp_info_object (self, "PortConfig info updated on changed");

  /* Trigger ports-configured signal if ports are available with format */
  n_ports = wp_stream_get_ports_count (WP_STREAM (self));
  if (n_ports > 0 && self->curr_format) {
    wp_info_object (self, "ports configured via port config changed");
    g_signal_emit (self,
        audio_stream_signals[AUDIO_STREAM_SIGNAL_PORTS_CONFIGURED], 0);
  }
}

static void
on_audio_node_enum_format_changed (WpPipewireObject *po, GAsyncResult *res,
    WpStream *s)
{
  g_autoptr (WpAudioStream) self = WP_AUDIO_STREAM (s);
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (GError) err = NULL;
  g_autoptr (WpNode) node = NULL;

  /* Check for errors */
  it = wp_pipewire_object_enum_params_finish (po, res, &err);
  if (!it) {
    audio_stream_notify_ports_error (self,
        "failed to enum EnumFormat on params changed");
    return;
  }

  wp_debug_object (self, "enum PortConfig finished on params changed");

  /* Update EnumFormat info */
  if (!update_audio_enum_format_info (self, it)) {
    audio_stream_notify_ports_error (self,
        "no audio format found on params changed");
    return;
  }

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
on_audio_node_params_changed (WpNode * node, const gchar *param_name,
    WpAudioStream *self)
{
  wp_debug_object (self, "param %s changed", param_name);

  /* Only handle EnumFormat and PortConfig changes */
  if (g_str_equal (param_name, "EnumFormat")) {
    self->port_config_ready = FALSE;
    wp_debug_object (self, "enumerating EnumFormat params on changed...");
    wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node), "EnumFormat",
        NULL, NULL, (GAsyncReadyCallback) on_audio_node_enum_format_changed,
        g_object_ref (self));
  } else if (g_str_equal (param_name, "PortConfig")) {
    self->port_config_ready = FALSE;
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

  self->port_config_ready = TRUE;
  wp_info_object (self, "PortConfig info updated on activation");

  /* Only finish activation if PortConfig is ready */
  if (self->port_config_ready) {
    wp_debug_object (self, "finish activation");
    wp_object_update_features (WP_OBJECT (self), WP_STREAM_FEATURE_ACTIVE, 0);
  }
}

static void
on_audio_stream_enum_format_done (WpPipewireObject *po, GAsyncResult *res,
    WpTransition *t)
{
  g_autoptr (WpTransition) transition = t;
  WpAudioStream *self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) it = NULL;
  g_autoptr (GError) err = NULL;
  g_autoptr (WpNode) node = NULL;

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
wp_audio_stream_enable_active (WpStream *stream, WpTransition *transition)
{
  WpAudioStream *self = WP_AUDIO_STREAM (stream);
  g_autoptr (WpNode) node = NULL;

  /* Get stream node */
  node = wp_stream_get_node (WP_STREAM (self));
  g_return_if_fail (node);

  /* Listen for PortConfig params changed */
  self->params_changed_sigid = g_signal_connect_object (node,
      "params-changed", (GCallback) on_audio_node_params_changed, self, 0);

  /* Enum EnumFormat to see what formats the stream supports */
  wp_debug_object (self, "enumerating EnumFormat params on activation...");
  wp_pipewire_object_enum_params (WP_PIPEWIRE_OBJECT (node), "EnumFormat", NULL,
      NULL, (GAsyncReadyCallback) on_audio_stream_enum_format_done,
      g_object_ref (transition));
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
  self->port_config_ready = FALSE;

  /* EnumFormat info */
  g_ptr_array_set_size (self->supported_raw_formats, 0);
  g_ptr_array_set_size (self->supported_enc_formats, 0);

  /* PortConfig info */
  self->curr_mode = SPA_PARAM_PORT_CONFIG_MODE_none;
  self->curr_monitor = TRUE;
  self->curr_control = FALSE;
  g_clear_pointer (&self->curr_format, wp_spa_pod_unref);
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
  WpAudioStream *self = WP_AUDIO_STREAM (stream);
  WpAudioStream *other = WP_AUDIO_STREAM (other_stream);

  /* Set encoded only if at least one stream does not support raw formats */
  if (encoded_only)
    *encoded_only = self->supported_raw_formats->len == 0 ||
        other->supported_raw_formats->len == 0;

  /* Make sure the stream types are the same */
  if (G_OBJECT_TYPE (stream) != G_OBJECT_TYPE (other_stream))
    return FALSE;

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
  guint n_ports = wp_stream_get_ports_count (WP_STREAM (self));

  wp_info_object (self, "audio ports changed: count=%u", n_ports);

  /* Trigger ports-configured signal if ports are available with format */
  if (n_ports > 0 && self->curr_format) {
    wp_info_object (self, "ports configured via ports changed");
    g_signal_emit (self,
        audio_stream_signals[AUDIO_STREAM_SIGNAL_PORTS_CONFIGURED], 0);
  }
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
wp_audio_stream_class_init (WpAudioStreamClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpStreamClass *stream_class = (WpStreamClass *) klass;

  object_class->finalize = wp_audio_stream_finalize;

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
 * \brief Checks whether the stream's encoded formats are compatible with the
 * ones from another stream.
 *
 * \ingroup wpstream
 * \param self the stream
 * \param other the other stream to check encoded format compatibility
 * \returns TRUE if the stream is encoding compatible with other, FALSE
 *    otherwise.
 */
gboolean
wp_audio_stream_is_encoded_compatible (WpAudioStream * self,
    WpAudioStream * other)
{
  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self), FALSE);
  g_return_val_if_fail (WP_IS_AUDIO_STREAM (other), FALSE);

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
 * \brief Checks whether the ports are configured or not.
 *
 * \ingroup wpstream
 * \param self the stream
 * \returns TRUE if the ports are configured, FALSE otherwise
 */
gboolean
wp_audio_stream_has_ports_configured (WpAudioStream * self)
{
  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self), FALSE);

  return self->curr_format != NULL &&
      self->curr_mode != SPA_PARAM_PORT_CONFIG_MODE_none;
}

static gboolean
wp_audio_stream_configure_ports_internal (WpAudioStream * self,
    WpSpaPod *format, guint32 mode_id, gboolean monitor, gboolean control)
{
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpSpaPod) ports_format = NULL;
  gint channels = 0;
  const gchar *port_group = NULL;
  g_autoptr (WpSpaPod) param = NULL;
  guint32 fmt = 0;
  gint rate = 0;
  g_autoptr (WpSpaPodBuilder) b = NULL;

  /* Get the node */
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
   * works if the node is not running */
  if (wp_node_get_state (WP_NODE (node), NULL) >= WP_NODE_STATE_IDLE)
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
    wp_spa_pod_builder_add (b, "id-00000006", "s", port_group, NULL);
  if (mode_id != SPA_PARAM_PORT_CONFIG_MODE_passthrough)
    wp_spa_pod_builder_add (b, "format", "P", ports_format, NULL);
  param = wp_spa_pod_builder_end (b);

  /* Set port config */
  if (!wp_pipewire_object_set_param (WP_PIPEWIRE_OBJECT (node), "PortConfig",
      0, g_steal_pointer (&param)))
    return FALSE;

  /* Update configuration */
  g_clear_pointer (&self->curr_format, wp_spa_pod_unref);
  self->curr_format = wp_spa_pod_ref (ports_format);
  self->curr_mode = mode_id;
  self->curr_monitor = monitor;
  self->curr_control = control;

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
  g_autoptr (WpSettings) settings = NULL;
  g_autoptr (WpProperties) properties = props;
  gboolean monitor_ports, control_port, no_dsp, mono_only, encoded_only;
  g_autoptr (WpSpaPod) format = NULL;
  guint32 mode;
  const gchar *str;

  g_return_val_if_fail (WP_IS_AUDIO_STREAM (self), FALSE);
  g_return_val_if_fail (wp_object_get_active_features (WP_OBJECT (self)) &
      WP_STREAM_FEATURE_ACTIVE, FALSE);

  /* Get core */
  core = wp_object_get_core (WP_OBJECT (self));
  g_return_val_if_fail (core, FALSE);

  /* Init port configuration values to default */
  monitor_ports = self->curr_monitor;
  control_port = self->curr_control;
  no_dsp = FALSE;
  mono_only = FALSE;
  encoded_only = FALSE;

  /* Use settings values if any */
  settings = wp_settings_find (core, NULL);
  if (settings) {
    g_autoptr (WpSpaJson) val = NULL;
    val = wp_settings_get (settings, "node.features.audio.monitor-ports");
    if (val)
      wp_spa_json_parse_boolean (val, &monitor_ports);
    g_clear_pointer (&val, wp_spa_json_unref);

    val = wp_settings_get (settings, "node.features.audio.control-port");
    if (val)
      wp_spa_json_parse_boolean (val, &control_port);
    g_clear_pointer (&val, wp_spa_json_unref);

    val = wp_settings_get (settings, "node.features.audio.no-dsp");
    if (val)
      wp_spa_json_parse_boolean (val, &no_dsp);
    g_clear_pointer (&val, wp_spa_json_unref);

    val = wp_settings_get (settings, "node.features.audio.mono");
    if (val)
      wp_spa_json_parse_boolean (val, &mono_only);
    g_clear_pointer (&val, wp_spa_json_unref);
  }

  /* Use property values if any */
  if (properties) {
    str = wp_properties_get (properties, "stream.monitor-ports");
    if (str)
      monitor_ports = spa_atob (str);
    str = wp_properties_get (properties, "stream.control-port");
    if (str)
      control_port = spa_atob (str);
    str = wp_properties_get (properties, "stream.no-dsp");
    if (str)
      no_dsp = spa_atob (str);
    str = wp_properties_get (properties, "stream.mono-only");
    if (str)
      mono_only = spa_atob (str);
    str = wp_properties_get (properties, "stream.encoded-only");
    if (str)
      encoded_only = spa_atob (str);
  }

  /* Use passthrough mode if no-dsp or encoded-only is enabled, otherwise
   * default to DSP */
  mode = (no_dsp || encoded_only) ? SPA_PARAM_PORT_CONFIG_MODE_passthrough :
      SPA_PARAM_PORT_CONFIG_MODE_dsp;

  /* Force DSP mode if the stream supports RAW formats and mono_only is
   * enabled */
  if (mono_only &&  self->supported_raw_formats->len > 0) {
    mode = SPA_PARAM_PORT_CONFIG_MODE_dsp;
    mono_only = TRUE;
  }

  /* Force passthrough mode and encoded_only if the stream only supports encoded
   * formats */
  if (self->supported_enc_formats->len > 0 &&
      self->supported_raw_formats->len == 0) {
    mode = SPA_PARAM_PORT_CONFIG_MODE_passthrough;
    encoded_only = TRUE;
  }

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


/* Video Stream */

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
  g_return_if_fail (node);

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
  /* Since video streams cannot be "mixed" like audio streams, we always
   * mark them as exclusive */
  return TRUE;
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
