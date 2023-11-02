.. _config_filters:

Filters
=======

Introduction
------------

A pair of nodes will be considered filter nodes by wireplumber if they have the
"node.link-group" property set to a common value. This property is always set by
PipeWire when creating filter nodes if they are defined in the PipeWire's
configuration file. The pair of nodes always consist of a stream node, and a
main node. When using the filter nodes, the main node acts as a virtual device,
where the audio is sent or captured to/from; and the stream node acts as a
virtual stream, where the audio is sent or received to/from the next node in the
graph.

For example, the media class of the nodes for a input filter would be:

- main node: Audio/Sink
- stream node: Stream/Output/Audio

And, if this filter is used between an application stream, and the default audio
device, the graph would look like this:

 application stream node  ->  filter main node
 (Stream/Output/Audio)        (Audio/Sink)

 filter stream node       ->  default device node
 (Stream/Output/Audio)        (Audio/Sink)


On the other hand, the media class of the nodes for an output filter would be:

- main node: Audio/Source
- stream node: Stream/Input/Audio

And the same logic is applied if they are used, but in the opposite direction.
This is how the graph would look like if an application wants to capture audio
from a device that uses an input filter.

 application stream node  <-  filter main node
 (Stream/Input/Audio)        (Audio/Source)

 filter stream node       <-  default device node
 (Stream/Input/Audio)        (Audio/Source)

Finally, if multiple filters have the same direction, they can also be chained
together so that the audio of a filter is sent to the input of the next filter.

Example of existing filters in PipeWire are echo-cancel, filter-chain and
loopback nodes.

The next section will describe how we can define filter properties so that they
are automatically linked by the wireplumber policy in any way we want.


Filter properties
-----------------

Currently, if a filter node is created, wireplumber will check the following
optional node properties on the main node:

.. code-block::

  filter.smart

  Boolean indicating whether smart policy will be used in the filter nodes or
  not. This is disabled by default, therefore filter nodes will be treated as
  regular nodes, without applying any kid of extra logic. On the other hand, if
  this property is set to true, automatic (smart) filter policy will be used
  when linking filters. The properties below will instruct the smart policy how
  to link the filters automatically.

.. code-block::

  filter.smart.name:

  The unique name of the filter. WirePlumber will use the "node.link-group"
  property as filter name if this property is not set.

.. code-block::

  filter.smart.disabled:

  Boolean indicating whether the filter should be disabled at all or not. A
  disabled filter will never be used in any circumstances. If the property is
  not set, wireplumber will consider the filter not disabled by default.

.. code-block::

  filter.smart.target:

  A JSON object that defines the matching properties of the filter's target node.
  A filter target can never be another filter node (wireplumber will ignore it),
  and must always be a device node. If this property is not set, WirePlumber will
  use the default node as target.

.. code-block::

  filter.smart.before:

  A JSON array with the filters names that are supposed to be used before this
  filter. If not set, wireplumber will link the filters by order of creation.

.. code-block::

  filter.smart.after:

  A JSON array with the filters names that are supposed to be used after this
  filter. If not set, wireplumber will link the filters by order of creation.

Note that these properties must be set in the filter's main node, not the
filter's stream node.

As an example, we will describe here how to create 2 loopback filters in the
PipeWire's configuration, with names loopback-1 and loopback-2, that will be
linked with the default audio device, and use loopback-2 filter as the last
filter in the chain.

The PipeWire configuration files for the 2 filters should be like this:

- /usr/share/pipewire/pipewire.conf.d/loopback-1.conf:

.. code-block::

    context.modules = [
        {   name = libpipewire-module-loopback
            args = {
                node.name = loopback-1-sink
                node.description = "Loopback 1 Sink"
                capture.props = {
                    audio.position = [ FL FR ]
                    media.class = Audio/Sink
                    filter.name = loopback-1
                    filter.enabled = true
                    filter.before = [ loopback-2 ]
                }
                playback.props = {
                    audio.position = [ FL FR ]
                    node.passive = true
                    node.dont-remix = true
                }
            }
        }
    ]

- /usr/share/pipewire/pipewire.conf.d/loopback-2.conf:

.. code-block::

    context.modules = [
        {   name = libpipewire-module-loopback
            args = {
                node.name = loopback-2-sink
                node.description = "Loopback 2 Sink"
                capture.props = {
                    audio.position = [ FL FR ]
                    media.class = Audio/Sink
                    filter.name = loopback-2
                    filter.enabled = true
                }
                playback.props = {
                    audio.position = [ FL FR ]
                    node.passive = true
                    node.dont-remix = true
                }
            }
        }
    ]

Finally, if we restart PipeWire and WirePlumber to apply the configuration
changes, and play a test.wave audio file with paplay to see if wireplumber links
the filter nodes properly, the graph should look like this:


 paplay node             ->  loopback-1 main node
 (Stream/Output/Audio)       (Audio/Sink)

 loopback-1 stream node  ->  loopback-2 main node
 (Stream/Output/Audio)       (Audio/Sink)

 loopback-2 stream node  ->  default device node
 (Stream/Output/Audio)       (Audio/Sink)


If we remove `filter.before = [ loopback-2 ]` property from the loopback-1 filter,
and add a `filter.before = [ loopback-1 ]` property in the loopback-2 filter
configuration file. WirePlumber should link the loopback-1 filter as the last filter
in the chain, like this:

 paplay node             ->  loopback-2 main node
 (Stream/Output/Audio)       (Audio/Sink)

 loopback-2 stream node  ->  loopback-1 main node
 (Stream/Output/Audio)       (Audio/Sink)

 loopback-1 stream node  ->  default device node
 (Stream/Output/Audio)       (Audio/Sink)


On the other hand, the filters can have different targets. For example, we can
define the filters like this:

- `/usr/share/pipewire/pipewire.conf.d/loopback-1.conf`:

.. code-block::

    context.modules = [
        {   name = libpipewire-module-loopback
            args = {
                node.name = loopback-1-sink
                node.description = "Loopback 1 Sink"
                capture.props = {
                    audio.position = [ FL FR ]
                    media.class = Audio/Sink
                    filter.name = loopback-1
                    filter.enabled = true
                    filter.before = [ loopback-2 ]
                    filter.target = { node.name = "not-default-audio-device-name" }
                }
                playback.props = {
                    audio.position = [ FL FR ]
                    node.passive = true
                    node.dont-remix = true
                }
            }
        }
    ]

- `/usr/share/pipewire/pipewire.conf.d/loopback-2.conf`:

.. code-block::

    context.modules = [
        {   name = libpipewire-module-loopback
            args = {
                node.name = loopback-2-sink
                node.description = "Loopback 2 Sink"
                capture.props = {
                    audio.position = [ FL FR ]
                    media.class = Audio/Sink
                    filter.name = loopback-2
                    filter.enabled = true
                }
                playback.props = {
                    audio.position = [ FL FR ]
                    node.passive = true
                    node.dont-remix = true
                }
            }
        }
    ]

If this is the case, WirePlumber will link the filters like this when using
paplay:

 paplay node             ->  loopback-2 main node
 (Stream/Output/Audio)       (Audio/Sink)

 loopback-2 stream node  ->  default device node
 (Stream/Output/Audio)       (Audio/Sink)

 loopback-1 stream node  ->  not-default-audio-device-name device node
 (Stream/Output/Audio)       (Audio/Sink)

The loopback-1 main node will only be used if an application wants to play audio
on the device node with node name "not-default-audio-device-name".


Filters metadata
----------------

Similar to the default metadata, it is also possible to override the filter
properties by using the "filters" metadata. This allow users to change the filters
policy at runtime.

For example, if loopback-1 main node Id is `40`, we can disable the filter by
setting its "filter.enabled" metadata key to false using the `pw-metadata` tool:

    $ pw-metadata -n filters 40 "filter.enabled" false Spa:String:JSON

We can also change the target of a filter at runtime:

    $ pw-metadata -n filters 40 "filter.target" { node.name = "new-target-node-name" } Spa:String:JSON

Every time a key in the filters metadata changes, all filters are unlinked and
re-linked properly by the policy.
