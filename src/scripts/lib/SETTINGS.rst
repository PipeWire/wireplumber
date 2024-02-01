
.. describe:: device.restore-profile

   When a device profile is changed manually (e.g. via pavucontrol), WirePlumber
   stores the selected profile and restores it when the device appears again
   (e.g. after a reboot). If this setting is disabled, WirePlumber will always
   pick the best profile for the device based on profile priorities and
   availability (or custom rules, if any).

   :Default value: ``true``

.. describe:: device.restore-routes

   When a device route is changed manually (e.g. via pavucontrol), WirePlumber
   stores the selected route and restores it when the same profile is
   selected for this device. If this setting is disabled, WirePlumber will
   always pick the best route for this device profile based on route priorities
   and availability (or custom rules, if any).

   This setting also enables WirePlumber to restore properties of the device
   route when the route is restored. This includes the volume levels of sources
   and sinks, as well as the IEC958 codecs selected (for routes that support
   encoded streams, such as HDMI).

   :Default value: ``true``

.. describe:: device.routes.default-sink-volume

   This option allows to set the default volume for sinks that are part of a
   device route (e.g. ALSA PCM sinks). This is used when the route is restored
   and the sink does not have a previously stored volume.

   :Default value: ``0.4 ^ 3`` (40% on the cubic scale)

.. describe:: device.routes.default-source-volume

   This option allows to set the default volume for sources that are part of a
   device route (e.g. ALSA PCM sources). This is used when the route is restored
   and the source does not have a previously stored volume.

   :Default value: ``1.0`` (100%)

.. describe:: linking.allow-moving-streams

   This option allows moving streams by overriding their target via metadata.
   When enabled, WirePlumber monitors the "default" metadata for changes in the
   ``target.object`` key of streams and if this key is set to a valid node name
   (``node.name``) or serial (``object.serial``), the stream is moved to that
   target node.

   This is used by applications such as pavucontrol and is recommended for
   compatibility with PulseAudio.

    .. note::

       On the metadata, the ``target.node`` key is also supported for
       compatibility with older versions of PipeWire, but it is deprecated.
       Please use the ``target.object`` key instead.

   :Default value: ``true``
   :See also: ``node.stream.restore-target``

.. describe:: linking.follow-default-target

   When a stream was started with the ``target.object`` property, WirePlumber
   normally links that stream to that target node and ignores the "default"
   target for that direction. However, if this option is enabled, WirePlumber
   will check if the designated target node *is* the "default" target and if so,
   it will act as if the stream did not have that property.

   In practice, this means that if the "default" target changes at runtime,
   the stream will be moved to the new "default" target.

   This is what Pulseaudio does and is implemented here for compatibility
   with some applications that do start with a ``target.object`` property
   set to the "default" target and expect the stream to be moved when the
   "default" target changes.

   Note that this logic is only applied on client (i.e. application) streams
   and *not* on filters.

   :Default value: ``true``

.. describe:: node.features.audio.no-dsp

   When this option is set to ``true``, audio nodes will not be configured
   in dsp mode, meaning that their channels will *not* be split into separate
   ports and that the audio data will *not* be converted to the float 32 format
   (F32P). Instead, devices will be configured in passthrough mode and streams
   will be configured in convert mode, so that their audio data is converted
   directly to the format that the device is expecting.

   This may be useful if you are trying to minimize audio processing for an
   embedded system, but it is not recommended for general use.

   .. warning::

      This option **will break** compatibility with JACK applications
      and may also break certain patchbay applications. Do not enable, unless
      you understand what you are doing.

   :Default value: ``false``

.. describe:: node.features.audio.monitor-ports

   This enables the creation of "monitor" ports for audio nodes. Monitor ports
   are created on nodes that have input ports (i.e. sinks and capture streams)
   and allow monitoring of the audio data that is being sent to the node.

   This is mostly used by monitoring applications, such as pavucontrol.

   :Default value: ``true``

.. describe:: node.features.audio.control-port

   This enables the creation of a "control" port for audio nodes. Control ports
   allow sending MIDI data to the node, allowing for control of certain node's
   parameters (such as volume) via external controllers.

   :Default value: ``false``

.. describe:: node.stream.restore-props

   WirePlumber stores stream parameters such as volume and mute status for each
   client (i.e. application) stream. If this setting is enabled, WirePlumber
   will restore the previously stored stream parameters when the stream is
   activated. If it is disabled, stream parameters will be initialized to their
   default values.

   :Default value: ``true``

.. describe:: node.stream.restore-target

   When a client (i.e. application) stream is manually moved to a different
   target node (e.g. via pavucontrol), the target node is stored by WirePlumber.
   If this setting is enabled, WirePlumber will restore the previously stored
   target node when the stream is activated.

   .. note::

      This does not restore manual links made by patchbay applications. This
      is only meant to restore the ``target.object`` property in the "default"
      metadata, which is manipulated by applications such as pavucontrol when
      a stream is moved to a different target.

   :Default value: ``true``
   :See also: ``linking.allow-moving-streams``

.. describe:: node.stream.default-playback-volume

   The default volume for playback streams to be applied when the stream is
   activated. This is only applied when ``node.stream.restore-props`` is
   ``true`` and the stream does not have a previously stored volume.

   :Default value: ``1.0``
   :Range: ``0.0`` to ``1.0``

.. describe:: node.stream.default-capture-volume

   The default volume for capture streams to be applied when the stream is
   activated. This is only applied when ``node.stream.restore-props`` is
   ``true`` and the stream does not have a previously stored volume.

   :Default value: ``1.0``
   :Range: ``0.0`` to ``1.0``

.. describe:: node.filter.forward-format

   When a "filter" pair of nodes (such as echo-cancel or filter-chain) is
   linked to a device node that has a different channel map than the filter
   nodes, this option allows the channel map of the filter nodes to be changed
   to match the channel map of the device node. The change is applied to both
   ends of the "filter", so that any streams linked to the filter are also
   reconfigured to match the target channel map.

   This is useful, for instance, to make sure that an application will be
   properly configured to output surround audio to a surround device, even
   when going through a filter that was not explicitly configured to have
   a surround channel map.

   :Default value: ``false``

.. describe:: node.restore-default-targets

   This setting enables WirePlumber to store and restore the "default" source
   and sink targets of the graph. In PulseAudio terminology, this is also known
   as the "fallback" source and sink.

   When this setting is enabled, WirePlumber will store the "default" source
   and sink targets when they are changed manually (e.g. via pavucontrol) and
   restore them when the available nodes change or after a reload/restart.
   It will also store a history of past selected "default" targets and restore
   previously selected ones if the currently selected are not available.

   If this is disabled, WirePlumber will pick the best available source
   and sink targets based on their priorities, but it will also respect
   manual user selections that are done at runtime - it will just not remember
   them so that it can restore them at a later time.

   :Default value: ``true``
