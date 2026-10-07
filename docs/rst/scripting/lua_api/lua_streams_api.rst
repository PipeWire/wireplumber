.. _lua_streams_api:

Streams
=======

A ``Stream`` is a logical group of ports on a node that share a direction and a
media type. Scripts never construct streams: the stream manager creates one per
port group as soon as a node appears on the graph, and destroys it with the
node. Streams are obtained from an :ref:`ObjectManager <lua_object_manager_api>`
or, in the linking hooks, from ``event:get_subject ()``.

This binds the :ref:`WpStream <stream_api>` C API. There are two concrete
kinds, ``AudioStream`` and ``VideoStream``, and an :ref:`Interest
<lua_object_interest_api>` can ask for either the base type or one of them::

   om = ObjectManager {
     Interest { type = "AudioStream",
       Constraint { "media.class", "=", "Stream/Output/Audio" },
     },
   }

Constants
---------

Three tables name the enumerations that the stream methods return.

``StreamKind`` tells apart the two roles a stream plays in the linking policy:

============================ ====================================================
Constant                     Meaning
============================ ====================================================
``StreamKind.SUBJECT``       an application stream, which asks to be linked to
                             something
``StreamKind.TARGET``        a device or virtual endpoint, which others ask to be
                             linked to
``StreamKind.UNKNOWN``       neither, because the node has no recognized
                             ``media.class``
============================ ====================================================

``StreamShape`` is the finer classification the kind is derived from, taken
from the node's ``media.class``: ``StreamShape.SINK``, ``StreamShape.SOURCE``,
``StreamShape.INPUT``, ``StreamShape.OUTPUT`` and ``StreamShape.UNKNOWN``. Sinks
and sources are targets; inputs and outputs are subjects.

``Direction`` is the direction of the stream's ports: ``Direction.INPUT`` or
``Direction.OUTPUT``. Note that a sink and a stream that plays into it are both
``Direction.INPUT``; the direction says which way data flows through the ports,
not which end of a link the stream is.

Features
--------

``Feature.Stream.ACTIVE`` is the only feature a stream has. It is activated by
the stream manager and means that the stream knows its ports and the formats
its node supports. A stream that is not active answers 0 to every port and
format query.

Signals
-------

A ``Stream`` emits the signals of :c:struct:`WpStream`, which are connected
with :func:`GObject.connect`:

``port-added`` (self, port)
   A port joined the stream. *port* is a :ref:`Port <lua_proxies_api>`.

``port-removed`` (self, port)
   A port left the stream.

``ports-changed`` (self)
   The set of ports settled after a change. This is emitted once per change,
   after the ``port-added`` and ``port-removed`` signals of that change, and is
   the right place to react to the stream's new shape.

An ``AudioStream`` emits two more, which report the outcome of a port
configuration this stream asked for:

``ports-configured`` (self)
   The node applied the PortConfig that was requested.

``ports-error`` (self, message)
   The node refused it.

Stream methods
--------------

.. function:: Stream.get_node(self)

   Binds :c:func:`wp_stream_get_node`

   :param self: the stream
   :returns: the node this stream belongs to
   :rtype: Node, see :ref:`lua_proxies_api`

.. function:: Stream.get_properties(self)

   Binds :c:func:`wp_stream_get_properties`

   Returns the stream's properties. These are a copy of the node's properties
   taken when the stream was created, so they do not follow later changes on
   the node.

   :param self: the stream
   :returns: the stream properties
   :rtype: :ref:`Properties <lua_properties_api>`

.. function:: Stream.get_direction(self)

   Binds :c:func:`wp_stream_get_direction`

   :param self: the stream
   :returns: ``Direction.INPUT`` or ``Direction.OUTPUT``
   :rtype: integer

.. function:: Stream.get_port_group(self)

   Binds :c:func:`wp_stream_get_port_group`

   Returns the ``port.group`` that this stream's ports carry, which is what
   separates the streams of a node that has more than one.

   :param self: the stream
   :returns: the port group, or nil

.. function:: Stream.get_creation_time(self)

   Binds :c:func:`wp_stream_get_creation_time`

   Returns the time the stream was created, in microseconds. The linking policy
   uses this to tell which of two streams is the newer one.

   :param self: the stream
   :returns: the creation time in microseconds
   :rtype: integer

.. function:: Stream.get_node_bound_id(self)

   Binds :c:func:`wp_stream_get_node_bound_id`

   Returns the global id of the stream's node, which is the id that clients and
   the metadata refer to.

   :param self: the stream
   :returns: the node's bound id, or ``Id.INVALID`` if the node is gone
   :rtype: integer

.. function:: Stream.get_kind(self)

   Binds :c:func:`wp_stream_get_kind`

   :param self: the stream
   :returns: one of the ``StreamKind`` constants
   :rtype: integer

.. function:: Stream.get_shape(self)

   Binds :c:func:`wp_stream_get_shape`

   :param self: the stream
   :returns: one of the ``StreamShape`` constants
   :rtype: integer

.. function:: Stream.is_compatible(self, other)

   Binds :c:func:`wp_stream_is_compatible`

   Checks whether the two streams have a format in common, which is the
   precondition for linking them. The second return value says that the only
   common format is an encoded one, in which case the link has to be made in
   passthrough mode.

   :param self: the stream
   :param other: the other stream
   :returns: whether the streams are compatible, and whether only an encoded
      format is common to both
   :rtype: boolean, boolean

.. function:: Stream.is_exclusive(self)

   Binds :c:func:`wp_stream_is_exclusive`

   Returns whether the stream accepts only one peer at a time. A video input
   stream always is, because video cannot be mixed; an audio stream is only
   when its ports are configured in passthrough mode, where there is nothing
   in the graph that could mix them.

   :param self: the stream
   :returns: whether the stream is exclusive
   :rtype: boolean

.. function:: Stream.get_supported_raw_format_count(self)
              Stream.get_supported_enc_format_count(self)

   Bind :c:func:`wp_stream_get_supported_raw_format_count` and
   :c:func:`wp_stream_get_supported_enc_format_count`

   Return how many raw, respectively encoded, formats the node reported in
   EnumFormat for this stream.

   :param self: the stream
   :returns: the number of formats
   :rtype: integer

.. function:: Stream.iterate_supported_raw_formats(self)
              Stream.iterate_supported_enc_formats(self)

   Bind :c:func:`wp_stream_new_supported_raw_format_iterator` and
   :c:func:`wp_stream_new_supported_enc_format_iterator`

   :param self: the stream
   :returns: the formats, as :ref:`Spa Pod <lua_spa_pod>` objects
   :rtype: :ref:`Iterator <lua_iterator_api>`

.. function:: Stream.has_port(self, port_id)
              Stream.has_monitor_port(self, port_id)
              Stream.has_control_port(self, port_id)

   Bind :c:func:`wp_stream_has_port`, :c:func:`wp_stream_has_monitor_port` and
   :c:func:`wp_stream_has_control_port`

   Check whether the given port belongs to this stream. The three kinds of port
   are disjoint: ``has_port`` answers only for the ports that carry the
   stream's own data, not for its monitor or control ports.

   :param self: the stream
   :param integer port_id: the global id of the port
   :returns: whether the stream has that port
   :rtype: boolean

.. function:: Stream.get_ports_count(self)
              Stream.get_monitor_ports_count(self)
              Stream.get_control_ports_count(self)

   Bind :c:func:`wp_stream_get_ports_count`,
   :c:func:`wp_stream_get_monitor_ports_count` and
   :c:func:`wp_stream_get_control_ports_count`

   :param self: the stream
   :returns: the number of ports of that kind
   :rtype: integer

.. function:: Stream.iterate_ports(self)
              Stream.iterate_monitor_ports(self)
              Stream.iterate_control_ports(self)

   Bind :c:func:`wp_stream_new_ports_iterator`,
   :c:func:`wp_stream_new_monitor_ports_iterator` and
   :c:func:`wp_stream_new_control_ports_iterator`

   :param self: the stream
   :returns: the ports of that kind
   :rtype: :ref:`Iterator <lua_iterator_api>` of Port

AudioStream methods
-------------------

An ``AudioStream`` has everything above, plus the port configuration API. Ports
are configured through PortConfig on the node, which decides the port layout
(one mono port per channel in DSP mode, or the negotiated format on a single
port otherwise) and whether monitor and control ports exist.

Configuring ports is asynchronous. The methods below return true when a request
was sent, in which case ``ports-configured`` or ``ports-error`` follows, and
false when the ports already have the requested configuration and nothing needs
to be done.

Scripts do not normally call these directly: a
:ref:`StreamLink <lua_stream_links_api>` configures both of its streams when it
is activated, and that is the only place that can decide which of the two
configurations the pair should agree on.

.. function:: AudioStream.get_ports_format(self)

   Binds :c:func:`wp_audio_stream_get_ports_format`

   :param self: the stream
   :returns: the format of the last requested PortConfig, or nil
   :rtype: :ref:`Spa Pod <lua_spa_pod>`

.. function:: AudioStream.get_ports_mode(self)

   Binds :c:func:`wp_audio_stream_get_ports_mode`

   :param self: the stream
   :returns: the mode of the last requested PortConfig (``"none"``,
      ``"dsp"``, ``"convert"`` or ``"passthrough"``), or nil
   :rtype: string

.. function:: AudioStream.get_ports_state(self)

   Binds :c:func:`wp_audio_stream_get_ports_state`

   ``AudioStreamPortsState.CONFIGURING`` lasts from the moment a port
   configuration is requested until its ports exist or the request fails.

   :param self: the stream
   :returns: one of the ``AudioStreamPortsState`` constants:
      ``UNCONFIGURED`` (the ports were never configured), ``CONFIGURING`` or
      ``CONFIGURED``
   :rtype: integer

.. function:: AudioStream.has_monitor_ports_enabled(self)
              AudioStream.has_control_ports_enabled(self)

   Bind :c:func:`wp_audio_stream_has_monitor_ports_enabled` and
   :c:func:`wp_audio_stream_has_control_ports_enabled`

   Return the monitor, respectively control, flag of the PortConfig that was
   last requested.

   :param self: the stream
   :returns: whether those ports were asked for
   :rtype: boolean

.. function:: AudioStream.is_monitorable(self, peer)

   Binds :c:func:`wp_audio_stream_is_monitorable`

   Answers what ``has_monitor_ports_enabled`` will answer once the ports are
   configured, by resolving the settings and the stream's formats the same way
   the configuration does. Unlike ``has_monitor_ports_enabled``, it is usable
   before anything has been configured, which is the case while the policy is
   still choosing a target.

   :param self: the stream
   :param peer: *(optional)* the stream this one would be linked with, or nil
      to ask about the default configuration
   :returns: whether configuring the ports now would enable monitor ports
   :rtype: boolean

.. function:: AudioStream.configure_ports(self, format, mode, monitor, control)

   Binds :c:func:`wp_audio_stream_configure_ports`

   :param self: the stream
   :param format: the format to configure, as a :ref:`Spa Pod <lua_spa_pod>`
   :param string mode: the port config mode
   :param boolean monitor: whether to create monitor ports
   :param boolean control: whether to create control ports
   :returns: whether a configuration request was sent
   :rtype: boolean

.. function:: AudioStream.configure_ports_with_props(self, props)

   Binds :c:func:`wp_audio_stream_configure_ports_with_props`

   Configures the ports from a set of properties, falling back to the settings
   and the node's own properties for everything the given properties do not
   say.

   :param self: the stream
   :param props: *(optional)* a table or
      :ref:`Properties <lua_properties_api>` object, or nil for the default
      configuration
   :returns: whether a configuration request was sent
   :rtype: boolean

.. function:: AudioStream.configure_ports_with_stream(self, other)

   Binds :c:func:`wp_audio_stream_configure_ports_with_stream`

   Configures the ports with the same configuration that *other* has, so that
   the two streams can be linked.

   :param self: the stream
   :param other: the stream to copy the configuration from
   :returns: whether a configuration request was sent
   :rtype: boolean

.. function:: AudioStream.configure_ports_for_peer(self, peer)

   Binds :c:func:`wp_audio_stream_configure_ports_for_peer`

   Configures the ports so that the stream can be linked with *peer*. This
   copies the configuration of *peer* like ``configure_ports_with_stream``,
   unless *peer* has unpositioned (AUX) DSP ports or this stream has
   ``stream.dont-remix`` set, in which case the stream keeps its own channels.

   :param self: the stream
   :param peer: the stream to link with, with its ports configured
   :returns: whether a configuration request was sent
   :rtype: boolean

VideoStream methods
-------------------

``VideoStream`` adds nothing to ``Stream``. Video ports are not configured by
WirePlumber, so there is no equivalent of the ``AudioStream`` methods above,
and ``iterate_supported_enc_formats`` is always empty.
