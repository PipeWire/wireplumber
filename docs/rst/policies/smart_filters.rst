.. _policies_smart_filters:

Smart Filters
=============

Introduction
------------

The smart filters policy allows automatically linking filters together, in a
chain, and tied to a specific target node. This is useful when we want to apply
a specific processing chain to a specific device, for example. When a stream is
about to be linked to a target node that is associated with a smart filter
chain, the policy will automatically link the stream with the first filter in
the chain, and the last filter in the chain with the target node. This is done
transparently to the client, allowing users to define a specific processing
chain for a specific device without having to create setups with virtual sinks
(or sources) that must be explicitly targeted by the clients.

Filters, in general, are nodes that are placed in the middle of the graph and
are used to modify the data that passes through them. For example, the
*echo-cancel*, the *filter-chain*, or the *loopback* nodes are filters.
Filters can be implemented either as a single node or as a pair of nodes with
opposite directions. For example, the *null-audio-sink* node can be configured
to be a single-node filter. On the other hand, the *filter-chain* is a pair of
nodes with opposite directions, where one node captures the audio from the graph
and the other node sends the modified audio back to the graph.

For the purpose of the **smart filters** policy, WirePlumber will only consider
pairs of nodes as filters, not single-node ones. More specifically, a pair of
nodes will be considered to be a filter by WirePlumber if they have the
``node.link-group`` property set to a common value. This property is always set
on pairs of nodes that are internally linked together and is a good indicator
that the nodes are implementing a filter.

That pair of nodes **must** always consist of a *stream* node and a *main* node.
The main node acts as a virtual device, where the data is sent or captured
to/from, and the stream node acts as a regular stream, where the data is sent
or received to/from the next node in the graph. This is designated by their
media class, as shown in the table below:

.. list-table::
   :widths: 30 35 35
   :header-rows: 1
   :stub-columns: 1

   * -
     - Input filter (virtual sink)
     - Output filter (virtual source)
   * - Main node
     - ``Audio/Sink`` (capture)
     - ``Audio/Source`` (playback)
   * - Stream node
     - ``Stream/Output/Audio`` (playback)
     - ``Stream/Input/Audio`` (capture)

For instance, if a smart filter is used between an application playback stream
and the default audio sink, the graph would look like this:

.. graphviz::

   digraph nodes {
      rankdir=LR;
      A [shape=box label=<application stream node<BR/>(Stream/Output/Audio)>];
      FM [shape=box label=<filter main node<BR/>(Audio/Sink)>];
      FS [shape=box label=<filter stream node<BR/>(Stream/Output/Audio)>];
      D [shape=box label=<default device node<BR/>(Audio/Sink)>];
      A -> FM;
      FS -> D;
      subgraph cluster_filter {
        style="dotted";
        FM; FS;
      }
   }

The same logic is applied if the smart filter is used between an application
capture stream and the default audio source, it is just all in the opposite
direction. This is how the graph would look like in this case:

.. graphviz::

   digraph nodes {
      rankdir=LR;
      A [shape=box label=<application stream node<BR/>(Stream/Input/Audio)>];
      FM [shape=box label=<filter main node<BR/>(Audio/Source)>];
      FS [shape=box label=<filter stream node<BR/>(Stream/Input/Audio)>];
      D [shape=box label=<default device node<BR/>(Audio/Source)>];
      D -> FS;
      FM -> A;
      subgraph cluster_filter {
        style="dotted";
        FM; FS;
      }
   }

When multiple filters have the same direction, they can also be chained together
so that the output of one filter is sent to the input of the next filter. The
next section describes how these chains can be described with properties so that
they are automatically linked by WirePlumber in any way we want.

Filter properties
-----------------

When a filter node is created, WirePlumber will check for the presence of the
following optional node properties on the **main** node:

- **filter.smart**

  Boolean indicating whether smart policy will be used for these filter nodes or
  not. This is disabled by default, therefore filter nodes will be treated as
  regular nodes, without applying any kind of extra logic. On the other hand, if
  this property is set to ``true``, automatic (smart) filter policy will be used
  when linking them. The properties below will then also apply, providing
  further instructions.

- **filter.smart.name**

  The unique name of the filter. WirePlumber will use the value of the
  ``node.link-group`` property as the filter name if this property is not set.

- **filter.smart.disabled**

  Boolean indicating whether the filter should be disabled or not. A disabled
  filter will never be used under any circumstances. If the property is not set,
  WirePlumber will consider the filter as enabled (i.e. disabled = false).

- **filter.smart.targetable**

  Boolean indicating whether the filter can be directly linked with clients that
  have it defined as a target (Eg: ``pw-play --target <filter-name>``) or not.
  This can be useful when a client wants to be linked with a filter that is in
  the middle of the chain in order to bypass the filters that are placed before
  the selected one. If the property is not set, WirePlumber will consider the
  filter not targetable by default, meaning filters will never by bypassed by
  clients, and clients will always be linked with the first filter in the chain.

- **filter.smart.target**

  A JSON object that defines the matching properties of the filter's target
  node. A filter target can never be another filter node (WirePlumber will
  ignore it), it must be a device or virtual sink (or source, depending on the
  direction of the filter). If this property is not set, WirePlumber will use
  the default sink/source as the target.

- **filter.smart.before**

  A JSON array containing the names of the filters that are supposed to be
  chained after this filter (i.e. this filter here should be chained *before*
  those). If not set, WirePlumber will link the filters by order of creation.

- **filter.smart.after**

  A JSON array containing the names of the filters that are supposed to be
  chained before this filter (i.e. this filter here should be chained *after*
  those). If not set, WirePlumber will link the filters by order of creation.

.. note::

   These properties must be set on the filter's **main** node, not the stream
   node.

As an example, we will describe here how to create 2 loopback filters in
PipeWire's configuration, with names loopback-1 and loopback-2, that will be
linked with the default audio device, and use loopback-2 filter as the last
filter in the chain.

The PipeWire configuration files for the 2 filters should be like this:

- ~/.config/pipewire/pipewire.conf.d/loopback-1.conf:

  .. code-block::
     :emphasize-lines: 8-11

     context.modules = [
         {   name = libpipewire-module-loopback
             args = {
                 node.name = loopback-1-sink
                 node.description = "Loopback 1 Sink"
                 capture.props = {
                     audio.position = [ FL FR ]
                     media.class = Audio/Sink
                     filter.smart = true
                     filter.smart.name = loopback-1
                     filter.smart.before = [ loopback-2 ]
                 }
                 playback.props = {
                     audio.position = [ FL FR ]
                     node.passive = true
                     stream.dont-remix = true
                 }
             }
         }
     ]

- ~/.config/pipewire/pipewire.conf.d/loopback-2.conf:

  .. code-block::
     :emphasize-lines: 8-10

     context.modules = [
         {   name = libpipewire-module-loopback
             args = {
                 node.name = loopback-2-sink
                 node.description = "Loopback 2 Sink"
                 capture.props = {
                     audio.position = [ FL FR ]
                     media.class = Audio/Sink
                     filter.smart = true
                     filter.smart.name = loopback-2
                 }
                 playback.props = {
                     audio.position = [ FL FR ]
                     node.passive = true
                     stream.dont-remix = true
                 }
             }
         }
     ]

After restarting PipeWire to apply the configuration changes, playing a test
wave audio file with paplay to the default device should result in the following
graph:

.. graphviz::

   digraph nodes {
      rankdir=LR;
      paplay [shape=box label=<paplay node<BR/>(Stream/Output/Audio)>];
      L1M [shape=box label=<loopback-1 main node<BR/>(Audio/Sink)>];
      L1S [shape=box label=<loopback-1 stream node<BR/>(Stream/Output/Audio)>];
      L2M [shape=box label=<loopback-2 main node<BR/>(Audio/Sink)>];
      L2S [shape=box label=<loopback-2 stream node<BR/>(Stream/Output/Audio)>];
      device [shape=box label=<default device node<BR/>(Audio/Sink)>];
      paplay -> L1M;
      L1S -> L2M;
      L2S -> device;
      subgraph cluster_filter1 {
        style="dotted";
        L1M; L1S;
      }
      subgraph cluster_filter2 {
        style="dotted";
        L2M; L2S;
      }
   }

Now, if we remove the ``filter.smart.before = [ loopback-2 ]`` property from the
loopback-1 filter, and add a ``filter.smart.before = [ loopback-1 ]`` property
in the loopback-2 filter configuration file, WirePlumber should link the
loopback-1 filter as the last filter in the chain, like this:

.. graphviz::

   digraph nodes {
      rankdir=LR;
      paplay [shape=box label=<paplay node<BR/>(Stream/Output/Audio)>];
      L1M [shape=box label=<loopback-1 main node<BR/>(Audio/Sink)>];
      L1S [shape=box label=<loopback-1 stream node<BR/>(Stream/Output/Audio)>];
      L2M [shape=box label=<loopback-2 main node<BR/>(Audio/Sink)>];
      L2S [shape=box label=<loopback-2 stream node<BR/>(Stream/Output/Audio)>];
      device [shape=box label=<default device node<BR/>(Audio/Sink)>];
      paplay -> L2M;
      L2S -> L1M;
      L1S -> device;
      subgraph cluster_filter1 {
        style="dotted";
        L1M; L1S;
      }
      subgraph cluster_filter2 {
        style="dotted";
        L2M; L2S;
      }
   }

In addition, the filters can have different targets. For example, we can define
the filters like this:

- ~/.config/pipewire/pipewire.conf.d/loopback-1.conf:

  .. code-block::
     :emphasize-lines: 12

     context.modules = [
         {   name = libpipewire-module-loopback
             args = {
                 node.name = loopback-1-sink
                 node.description = "Loopback 1 Sink"
                 capture.props = {
                     audio.position = [ FL FR ]
                     media.class = Audio/Sink
                     filter.smart = true
                     filter.smart.name = loopback-1
                     filter.smart.after = [ loopback-2 ]
                     filter.smart.target = { node.name = "not-default-audio-device" }
                 }
                 playback.props = {
                     audio.position = [ FL FR ]
                     node.passive = true
                     stream.dont-remix = true
                 }
             }
         }
     ]

- ~/.config/pipewire/pipewire.conf.d/loopback-2.conf:

  .. code-block::

     context.modules = [
         {   name = libpipewire-module-loopback
             args = {
                 node.name = loopback-2-sink
                 node.description = "Loopback 2 Sink"
                 capture.props = {
                     audio.position = [ FL FR ]
                     media.class = Audio/Sink
                     filter.smart = true
                     filter.smart.name = loopback-2
                 }
                 playback.props = {
                     audio.position = [ FL FR ]
                     node.passive = true
                     stream.dont-remix = true
                 }
             }
         }
     ]

In this case, playing a test wave audio file with paplay to the
``not-default-audio-device`` device should result in the following graph:

.. graphviz::

   digraph nodes {
      rankdir=LR;
      paplay [shape=box label=<paplay node<BR/>(Stream/Output/Audio)>];
      L1M [shape=box label=<loopback-1 main node<BR/>(Audio/Sink)>];
      L1S [shape=box label=<loopback-1 stream node<BR/>(Stream/Output/Audio)>];
      L2M [shape=box label=<loopback-2 main node<BR/>(Audio/Sink)>];
      L2S [shape=box label=<loopback-2 stream node<BR/>(Stream/Output/Audio)>];
      device [shape=box label=<not-default-audio-device node<BR/>(Audio/Sink)>];
      paplay -> L2M;
      L2S -> L1M;
      L1S -> device;
      subgraph cluster_filter1 {
        style="dotted";
        L1M; L1S;
      }
      subgraph cluster_filter2 {
        style="dotted";
        L2M; L2S;
      }
   }

In this configuration, the loopback-1 filter will only be linked if the
application stream is targeting the device node called
"not-default-audio-device".

Filters metadata
----------------

Similar to the default metadata, it is also possible to override the filter
properties using the "filters" metadata object. This allow users to change the
filters policy at runtime.

For example, assuming the id of the *loopback-1* main node is ``40``, we can
disable the filter by setting its ``filter.smart.disabled`` metadata key to
``true`` using the ``pw-metadata`` tool like this:

.. code-block:: bash

   $ pw-metadata -n filters 40 "filter.smart.disabled" true Spa:String:JSON

We can also change the target of a filter at runtime:

.. code-block:: bash

   $ pw-metadata -n filters 40 "filter.smart.target" "{ node.name = new-target-node-name }" Spa:String:JSON

Every time a key in the filters metadata changes, all filters are unlinked and
re-linked properly, following the new policy.
