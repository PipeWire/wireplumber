.. _policies_linking:

Linking Policy
==============

Introduction
------------

The linking policy in WirePlumber is the logic charged to link a PipeWire stream
node with a PipeWire device node (most cases), or with another PipeWire stream
node (monitoring applications).

PipeWire stream nodes always have one of the following media classes:

- Stream/Output/Audio: For audio playback applications (Eg pw-play).
- Stream/Input/Audio: For audio capture applications (Eg pw-record).
- Stream/Input/Video: For video capture applications (Eg cheese).

And Pipewire device nodes always have one of the following media classes:

- Audio/Sink: For audio playback devices (Eg Speakers).
- Audio/Source: For audio capture devices (Eg Microphones).
- Video/Source: For video capture devices (Eg Cameras).

By default, since in most cases we want to link a stream node with a device
node, the linking policy logic when linking 2 nodes always follows the following
assignments:

.. graphviz::

   digraph nodes {
      rankdir=LR;
      APS [shape=box label=<audio playback stream<BR/>(Stream/Output/Audio)>];
      APD [shape=box label=<audio playback device<BR/>(Audio/Sink)>];
      ACS [shape=box label=<audio capture stream<BR/>(Stream/Input/Audio)>];
      ACD [shape=box label=<audio capture device<BR/>(Audio/Source)>];
      VCS [shape=box label=<video capture stream<BR/>(Stream/Input/Video)>];
      VCD [shape=box label=<video capture device<BR/>(Video/Source)>];
      APS -> APD;
      ACD -> ACS;
      VCD -> VCS;
   }


After that, once the media class of a device node has been select for a
particular stream node, and there are more than 1 device node matching such
media class, WirePlumber will select one based on a set of priorities:

First, it will check if there is a default configured device node for the
selected device media class. If there is one, and the node exists, it will link
the stream node with such configured default node. Users can easily configure
default device nodes for all the 3 different device media classes using tools
such as ``pavucontrol`` or ``wpctl``. The logic is implemented in the
``linking/find-default-target.lua`` Lua script.

If there isn't any default node configured, or there is a default node
configured but the node does not exist, WirePlumber will instead select the
best device node available. The best device node is the node with highest
session priority and available routes to the physical device. The logic is
implemented in the ``linking/find-best-target.lua`` Lua script.

If the best node could not be found because the system does not have any,
WirePlumber won't link the stream and will send a "no target node available"
error to the client.


Stream node linking properties
------------------------------

The above default linking logic behavior can be changed by setting specific
properties on the nodes.

.. note::

   These properties must be set in the **stream** nodes (not the device nodes),
   otherwise they won't have any effect.

- **target.object**:

  The name of the desired node for this stream to be linked with.
  If this property is present, WirePlumber will try to find such node, see if it
  can be linked with the stream, and if so, will use it instead of the default
  node or best node. The logic is implemented in the ``linking/find-defined-target.lua``
  Lua script. Since this property is not set by default, WirePlumber will always
  link stream nodes to the default or best device node found. This property can be
  easily set using tools such as ``pw-play`` with the ``--target`` flag.

  Note that any node name can be specified there, even if the name is not a device
  node name, but another stream node name. If this is the case, WirePlumber will
  link 2 stream nodes together. An example of this case is the monitoring nodes
  created by ``pavucontrol`` to monitor audio of all audio devices and streams.

- **node.dont-reconnect**:

  Boolean indicating whether the stream node should not be reconnected to a new
  node if its current linked node (target) was destroyed or not. By default it
  is set to ``false``, so if the property is not present in the stream node, WirePlumber
  will always try to reconnect the stream node to a new target instead of sending
  an error to the client. The logic is implemented in the ``linking/prepare-link.lua``
  Lua script.

- **node.dont-move**:

  Boolean indicating whether the stream node should not be movable or not at runtime
  using the metadata. If a stream node is not movable, it means that users cannot
  relink the stream node to a new target at runtime (using tools such as ``pavucontrol``
  or ``pw-metadata``) when the stream node is already linked to a different node. By
  default it is set to ``false``, so if the property is not present, WirePlumber will
  always move, and therefore link the stream node to a new target if it is defined and
  updated in the ``target.object`` metadata key.

- **node.dont-fallback**:

  Boolean indicating whether the stream node should not fallback to a different
  target if its defined target does not exist (the one defined with the ``target.object``
  property) or not. Therefore, if this property is set to ``true``, WirePlumber sends
  a "defined target not found" error to the client and will also destroy the stream
  node. By default it is set to ``false``, so if the property is not present in the
  stream node, WirePlumber will always fallback to the default or best target if
  the defined target was not found.

- **node.linger**:

  Boolean indicating whether the stream node should linger or not if its defined
  target was not found and the ``node.dont-fallback`` is set to true. Therefore, if
  this property is set to ``true``, the defined target was not found, and the
  ``node.dont-fallback`` is set to true, WirePlumber won't send a "defined target not found"
  error to the client, and won't destroy the stream node. This is useful if we want
  the stream to wait (without processing any data) until its defined target becomes
  available. By default it is set to ``false``, so if the property is not present in the
  stream node, WirePlumber will always destroy the node and send an error to the client
  if its target was not found and ``node.dont-fallback`` was set to true.


Linking settings
----------------

Apart from the above properties, there are also global settings for the linking
policy. See :ref:`config_settings` for more information, the linking settings
are prefixed with ``linking.``.
