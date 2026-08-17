.. _config_settings:

Well-known settings
===================

This section describes the settings that can be configured on WirePlumber.

Settings can be either configured statically in the configuration file
by setting them under the ``wireplumber.settings`` section, or they can be
configured dynamically at runtime by using metadata.

For more information on what "settings" are and how they work, refer to the
previous section: :ref:`config_configuration_option_types`.

.. tip::

   ``wpctl settings`` lists all the settings together with their description,
   type, valid range and current value, and can also change, save and reset
   them at runtime. The authoritative list is the
   ``wireplumber.settings.schema`` section of ``wireplumber.conf``.

.. describe:: bluetooth.use-persistent-storage

   When enabled, WirePlumber remembers whether a Bluetooth device was switched
   to headset mode, and restores that state the next time the device connects.

   :Default value: ``true``
   :See also: ``bluetooth.autoswitch-to-headset-profile``

.. describe:: bluetooth.autoswitch-to-headset-profile

   When enabled, Bluetooth headsets always expose a microphone, and the device
   is automatically switched to headset mode (HSP/HFP) when an application
   starts recording. When the recording stream goes away, the previous profile
   is restored.

   Headset mode has considerably lower audio quality than A2DP, which is why
   the switch is only made while a capture stream is actually present.

   :Default value: ``true``

.. describe:: bluetooth.profile-preference

   Controls which A2DP codec configuration is preferred when a profile is
   selected automatically for a Bluetooth device. Only two values are accepted:

   ``quality``
      prefer the codec with the best audio quality

   ``latency``
      prefer the codec with the lowest latency

   Any other value logs a warning and is treated as ``quality``.

   :Default value: ``"quality"``

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

   It is possible to override the value on a per-device basis with a property
   (*not* a setting, so this would go into a configuration file) on the device
   named ``device.routes.default-sink-volume``.

   :Default value: ``0.4 ^ 3`` (40% on the cubic scale)

.. describe:: device.routes.default-source-volume

   This option allows to set the default volume for sources that are part of a
   device route (e.g. ALSA PCM sources). This is used when the route is restored
   and the source does not have a previously stored volume.

   It is possible to override the value on a per-device basis with a property
   (*not* a setting, so this would go into a configuration file) on the device
   named ``device.routes.default-source-volume``.

   :Default value: ``1.0`` (100%)

.. describe:: device.routes.mute-on-alsa-playback-removed

   When enabled, all audio devices are muted when an active wired playback
   route (headphones, speakers) is unplugged. This prevents sound from
   unexpectedly coming out of the built-in speakers when headphones are
   removed.

   The mute is applied to all devices, not just the one whose route
   disappeared, and is not undone automatically; the user has to unmute
   explicitly.

   :Default value: ``false``
   :See also: ``device.routes.mute-on-bluetooth-playback-removed``

.. describe:: device.routes.mute-on-bluetooth-playback-removed

   The same as ``device.routes.mute-on-alsa-playback-removed``, but triggered
   when an active Bluetooth playback device disconnects instead.

   :Default value: ``false``

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

.. describe:: linking.pause-playback

   When an audio sink is removed, pause media players that have streams
   playing to it. Pausing is done via MPRIS interface.

   :Default value: ``true``

.. describe:: linking.role-based.duck-level

   The volume to which a stream is reduced when it is *ducked* by the
   role-based linking policy, i.e. when a higher priority role becomes active
   and lower priority roles should remain audible but quieter.

   This only has an effect when the role-based policy is enabled; see the
   ``policy.linking.role-based`` feature in :ref:`config_features`.

   :Default value: ``0.3``
   :Range: ``0.0`` to ``1.0``

.. describe:: monitor.camera-discovery-timeout

   How long to wait, in milliseconds, after a camera device is reported before
   creating nodes for the discovered cameras.

   The same camera may be reported by both the V4L2 and the libcamera monitor.
   This timeout gives both monitors a chance to report it, so that WirePlumber
   can decide which backend to use instead of exposing the camera twice. The
   timer is restarted every time another device is reported.

   :Default value: ``1000``
   :Range: ``0`` to ``60000``
   :See also: :ref:`config_video`

.. describe:: monitor.alsa.autodetect-hdmi-channels

   When enabled, the ALSA monitor tries to detect the channel count and channel
   positions supported by HDMI devices, instead of using the values declared by
   the ALSA profile.

   .. warning::

      This is experimental.

   :Default value: ``false``

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

.. describe:: node.features.audio.mono

   When enabled, audio device *sink* nodes are configured with a single mono
   channel instead of their native channel layout.

   This only applies to ALSA PCM sinks and Bluetooth A2DP sinks; it does not
   affect sources or streams.

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

.. describe:: node.stream.default-media-role

   A ``media.role`` to assign to streams that do not declare one themselves.
   Streams that already have a ``media.role`` property are left alone.

   This is mainly useful together with the role-based linking policy, which
   groups and prioritizes streams by their role: without a default, streams
   from applications that do not set a role would not be handled by that
   policy at all.

   :Default value: ``null`` (no role is assigned)

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
