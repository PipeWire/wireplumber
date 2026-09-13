.. _lua_stream_links_api:

Stream Links
============

A ``StreamLink`` connects an output :ref:`Stream <lua_streams_api>` to an input
one. It owns the individual PipeWire links between their ports: it works out
which port pairs to connect, creates the links when it is activated, recreates
them when either stream reconfigures its ports, and destroys them when it is
deactivated or destroyed.

Stream links are created and tracked by the ``StreamLinkManager``, which also
adopts the links it finds on the graph that WirePlumber did not make itself, so
that the peers of a stream are complete no matter who linked it.

This binds the :ref:`WpStreamLink <stream_link_api>` and
:ref:`WpStreamLinkManager <stream_link_manager_api>` C API.

Features
--------

``Feature.StreamLink.ACTIVE`` is the only feature a stream link has. Activating
it configures the ports of both streams, if their media type needs that, and
creates the PipeWire links; deactivating it destroys them again without
destroying the stream link, which is how the media role policy suspends and
resumes playback.

A script that asks for a link is the one that has to activate it:
:func:`StreamLinkManager.link` hands back an inactive link, and until it is
activated nothing is connected. The manager activates a link on its own only
when it made that link itself, adopting one it found on the graph.

A link that is created but never activated links nothing, but still counts as a
peer of both its streams, so a failed activation has to be followed by
:func:`StreamLinkManager.remove_link`.

Signals
-------

A ``StreamLink`` emits the signals of :c:struct:`WpStreamLink`, which are
connected with :func:`GObject.connect`:

``link-added`` (self, link)
   A PipeWire link between the two streams was created. *link* is a
   :ref:`Link <lua_proxies_api>`.

``link-removed`` (self, link)
   One of them went away.

``link-error`` (self, message)
   One of them entered the error state. This is the failure that has to be
   reported back to the client that owns the stream, since the client only sees
   its own node and not the links.

StreamLink methods
------------------

.. function:: StreamLink.get_output_stream(self)

   Binds :c:func:`wp_stream_link_get_output_stream`

   The second return value says that the link is made from the output stream's
   monitor ports rather than from its own ports, which is how a capture stream
   records what a sink is playing.

   :param self: the stream link
   :returns: the output stream, and whether it is linked through its monitor
      ports
   :rtype: Stream, boolean

.. function:: StreamLink.get_input_stream(self)

   Binds :c:func:`wp_stream_link_get_input_stream`

   :param self: the stream link
   :returns: the input stream
   :rtype: Stream

.. function:: StreamLink.get_links_count(self)

   Binds :c:func:`wp_stream_link_get_links_count`

   :param self: the stream link
   :returns: the number of PipeWire links between the two streams
   :rtype: integer

.. function:: StreamLink.iterate_links(self)

   Binds :c:func:`wp_stream_link_new_links_iterator`

   :param self: the stream link
   :returns: the PipeWire links between the two streams
   :rtype: :ref:`Iterator <lua_iterator_api>` of Link

.. function:: StreamLink.reconfigure_ports(self)

   Binds :c:func:`wp_stream_link_reconfigure_ports`

   Configures the ports of both streams again and recreates the links. Use this
   when something the port configuration depends on has changed, such as a
   ``node.features.audio.*`` setting. Configuring one of the two streams
   directly is not equivalent: the two have to agree on one configuration, and
   it is the stream link that decides which one that is.

   Nothing happens, and false is returned, for a link that is not active yet,
   one whose links were only adopted from the graph, and one whose media type
   needs no port configuration.

   :param self: the stream link
   :returns: whether the ports are being reconfigured
   :rtype: boolean

StreamLinkManager functions
---------------------------

All of these are static; the underlying ``WpStreamLinkManager`` object is
looked up automatically.

.. function:: StreamLinkManager.get_name()

   Binds :c:func:`wp_stream_link_manager_get_name`

   :returns: the name of the stream link manager, or nil if none is loaded

.. function:: StreamLinkManager.link(subject, target)

   Binds :c:func:`wp_stream_link_manager_link`

   Creates a stream link between a stream that asks to be linked and the target
   it asked for. Which of the two is the output and which is the input follows
   from the direction of the subject.

   The subject is recorded on the link, so that :func:`StreamLinkManager.unlink`
   can later tell the links this stream asked for apart from the links other
   streams asked for on it. This is what a policy script should use.

   The returned link is not activated; activate it to establish the connection.
   It is destroyed automatically if either of its streams goes away.

   :param subject: the stream that asks for the link
   :param target: the stream it wants to be linked to
   :returns: the new stream link, or nil if it could not be created
   :rtype: StreamLink

.. function:: StreamLinkManager.create_link(output, input)

   Binds :c:func:`wp_stream_link_manager_create_link`

   Creates a stream link with no subject, which
   :func:`StreamLinkManager.unlink` will therefore not remove. This is for
   links that already exist on the graph and are only being adopted; use
   :func:`StreamLinkManager.link` for a link the policy decides to make.

   :param output: the output stream
   :param input: the input stream
   :returns: the new stream link, or nil if it could not be created
   :rtype: StreamLink

.. function:: StreamLinkManager.find_link(output, input)

   Binds :c:func:`wp_stream_link_manager_find_link`

   :param output: the output stream
   :param input: the input stream
   :returns: the stream link between the two, or nil if there is none
   :rtype: StreamLink

.. function:: StreamLinkManager.remove_link(link)

   Binds :c:func:`wp_stream_link_manager_remove_link`

   Destroys a stream link that is not active, removing it from the manager.
   This is what a script that activates a link itself must do when the
   activation fails: a link left in the manager still counts as a peer of both
   its streams and makes them look linked.

   The link must not be active. An active link owns the PipeWire links it
   created and this does not destroy them; use :func:`StreamLinkManager.unlink`
   for an established link.

   :param link: the stream link to destroy

.. function:: StreamLinkManager.unlink(stream)

   Binds :c:func:`wp_stream_link_manager_unlink`

   Destroys the stream links that the given stream asked for, unlinking it from
   the targets it was linked to. The stream is usually also part of links that
   other streams asked for on it, and those are left alone: they are the other
   streams' to undo. Adopted links have no subject and are left alone as well.

   :param stream: the stream to unlink
   :returns: whether the stream had links to remove
   :rtype: boolean

.. function:: StreamLinkManager.get_peers_count(stream)

   Binds :c:func:`wp_stream_link_manager_get_peers_count`

   :param stream: the stream
   :returns: the number of streams this one is linked to
   :rtype: integer

.. function:: StreamLinkManager.iterate_peers(stream)

   Binds :c:func:`wp_stream_link_manager_new_peers_iterator`

   :param stream: the stream
   :returns: the streams this one is linked to
   :rtype: :ref:`Iterator <lua_iterator_api>` of Stream

.. function:: StreamLinkManager.iterate_links(stream)

   Binds :c:func:`wp_stream_link_manager_new_links_iterator`

   :param stream: the stream
   :returns: every stream link the given stream takes part in, whether it asked
      for it or not
   :rtype: :ref:`Iterator <lua_iterator_api>` of StreamLink
