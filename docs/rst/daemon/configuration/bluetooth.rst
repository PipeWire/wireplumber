.. _config_bluetooth:

Bluetooth configuration
=======================

Bluetooth audio and MIDI devices are managed by the BlueZ and BlueZ-MIDI
monitors, respectively.

Both monitors are enabled by default and can be disabled using the
``monitor.bluez`` and ``monitor.bluez-midi`` :ref:`features <config_features>`
in the configuration file.

As with all device monitors, both of these monitors are implemented as SPA
plugins and are part of PipeWire. WirePlumber merely loads the plugins and lets
them do their work. These plugins then monitor the BlueZ system-wide D-Bus
service and create device and node objects for all the connected Bluetooth audio
and MIDI devices.

Logind integration
------------------

The BlueZ monitors are integrated with logind to ensure that only one user at a
time can use the Bluetooth audio devices. This is because on most Linux desktop
systems, the graphical login manager (GDM, SDDM, etc.) is running as a separate
user and runs its own instance of PipeWire and Wireplumber. This means that if a
user logs in graphically, the Bluetooth audio devices will be automatically
grabbed by the PipeWire/WirePlumber instance of the graphical login manager,
and the user that logs in will not get access to them.

To overcome this, the BlueZ monitors are integrated with logind and are only
allowed to create device and node objects for Bluetooth audio devices if the
user is currently on the "active" logind session.

In some cases, however, this behavior is not desired. For example, if you
manually switch to a TTY and log in there, you may want to keep the Bluetooth
audio devices connected to the now inactive graphical session. Or you may want
to have a dedicated user that is always allowed to use the Bluetooth audio
devices, regardless of the active logind session, for example for a (possibly
headless) music player daemon.

To disable this behavior, you can set the ``monitor.bluez.seat-monitoring``
:ref:`feature <config_features>` to ``disabled``.

Example configuration :ref:`fragment <config_conf_file_fragments>` file:

.. code-block::

   wireplumber.profiles = {
     main = {
       monitor.bluez.seat-monitoring = disabled
     }
   }

.. note::

   If logind is not installed on the system, this functionality is disabled
   automatically.

Monitor Properties
------------------

The BlueZ monitor SPA plugin (``api.bluez5.enum.dbus``) supports properties that
can be used to configure it when it is loaded. These properties can be set in
the ``monitor.bluez.properties`` section of the WirePlumber configuration file.

Example:

.. code-block::

  monitor.bluez.properties = {
    bluez5.roles = [ a2dp_sink a2dp_source bap_sink bap_source hsp_hs hsp_ag hfp_hf hfp_ag ]
    bluez5.codecs = [ sbc sbc_xq aac ]
    bluez5.enable-sbc-xq = true
    bluez5.hfphsp-backend = "native"
  }

.. describe:: bluez5.roles

   Enabled roles.

   Currently some headsets (e.g. Sony WH-1000XM3) do not work with both
   ``hsp_ag`` and ``hfp_ag`` enabled, so by default we enable only HFP.

   Supported roles:

   - ``hsp_hs`` (HSP Headset)
   - ``hsp_ag`` (HSP Audio Gateway),
   - ``hfp_hf`` (HFP Hands-Free),
   - ``hfp_ag`` (HFP Audio Gateway)
   - ``a2dp_sink`` (A2DP Audio Sink)
   - ``a2dp_source`` (A2DP Audio Source)
   - ``bap_sink`` (LE Audio Basic Audio Profile Sink)
   - ``bap_source`` (LE Audio Basic Audio Profile Source)

   :Default value: ``[ a2dp_sink a2dp_source bap_sink bap_source hfp_hf hfp_ag ]``
   :Type: array of strings

.. describe:: bluez5.codecs

   Enabled A2DP codecs.

   Supported codecs: ``sbc``, ``sbc_xq``, ``aac``, ``ldac``, ``aptx``,
   ``aptx_hd``, ``aptx_ll``, ``aptx_ll_duplex``, ``faststream``,
   ``faststream_duplex``, ``lc3plus_h3``, ``opus_05``, ``opus_05_51``,
   ``opus_05_71``, ``opus_05_duplex``, ``opus_05_pro``, ``lc3``.

   :Default value: all available codecs
   :Type: array of strings

.. describe:: bluez5.enable-msbc

   Enable mSBC codec (wideband speech codec for HFP/HSP).

   This does not work on all headsets, so it is enabled based on the hardware
   quirks database. By explicitly setting this option you can force it to be
   enabled or disabled regardless.

   :Default value: ``true``
   :Type: boolean

.. describe:: bluez5.enable-sbc-xq

   Enable SBC-XQ codec (high quality SBC codec for A2DP).

   This does not work on all headsets, so it is enabled based on the hardware
   quirks database. By explicitly setting this option you can force it to be
   enabled or disabled regardless.

   :Default value: ``true``
   :Type: boolean

.. describe:: bluez5.enable-hw-volume

   Enable hardware volume controls.

   This does not work on all headsets, so it is enabled based on the hardware
   quirks database. By explicitly setting this option you can force it to be
   enabled or disabled regardless.

   :Default value: ``true``
   :Type: boolean

.. describe:: bluez5.hfphsp-backend

   HFP/HSP backend.

   Available values: ``any``, ``none``, ``hsphfpd``, ``ofono`` or ``native``.

   :Default value: ``native``
   :Type: string

.. describe:: bluez5.hfphsp-backend-native-modem

   Modem to use for native HFP/HSP backend ModemManager support. When enabled,
   PipeWire will forward HFP commands to the specified ModemManager device.
   This corresponds to the 'Device' property of the
   ``org.freedesktop.ModemManager1.Modem`` interface. May also be ``any`` to
   use any available modem device.

   :Default value: ``none``
   :Type: string

.. describe:: bluez5.hw-offload-sco

   HFP/HSP hardware offload SCO support.

   Using this feature requires a custom WirePlumber script that handles audio
   routing in a platform-specific way. See ``tests/examples/bt-pinephone.lua``
   for an example.

   :Default value: ``false``
   :Type: boolean

.. describe:: bluez5.default.rate

   The default audio rate for the A2DP codec configuration.

   :Default value: ``48000``
   :Type: integer

.. describe:: bluez5.default.channels

   The default number of channels for the A2DP codec configuration.

   :Default value: ``2``
   :Type: integer

.. describe:: bluez5.dummy-avrcp-player

   Register dummy AVRCP player. Some devices have wrongly functioning volume or
   playback controls if this is not enabled. Disabled by default.

   :Default value: ``false``
   :Type: boolean

.. describe:: Opus Pro Audio mode settings

   .. code-block::

      bluez5.a2dp.opus.pro.channels = 3
      bluez5.a2dp.opus.pro.coupled-streams = 1
      bluez5.a2dp.opus.pro.locations = [ FL,FR,LFE ]
      bluez5.a2dp.opus.pro.max-bitrate = 600000
      bluez5.a2dp.opus.pro.frame-dms = 50
      bluez5.a2dp.opus.pro.bidi.channels = 1
      bluez5.a2dp.opus.pro.bidi.coupled-streams = 0
      bluez5.a2dp.opus.pro.bidi.locations = [ FC ]
      bluez5.a2dp.opus.pro.bidi.max-bitrate = 160000
      bluez5.a2dp.opus.pro.bidi.frame-dms = 400

   Options for the PipeWire-specific multichannel Opus codec, which can be used
   to transport audio over Bluetooth between devices running PipeWire.

MIDI Monitor Properties
-----------------------

The BlueZ MIDI monitor SPA plugin (``api.bluez5.midi.enum``) may, in the future,
support properties that can be used to configure it when it is loaded. These
properties can be set in the ``monitor.bluez-midi.properties`` section of the
WirePlumber configuration file. At the moment of writing, there are no
properties that can be set there.

In addition, the BlueZ MIDI monitor supports a list of MIDI server node names
that can be used to create Bluetooth LE MIDI service instances. These
server node names can be set in the ``monitor.bluez-midi.servers`` section of
the WirePlumber configuration file.

Example:

.. code-block::

   monitor.bluez-midi.servers = [ "bluez_midi.server" ]

.. note::

   Typical BLE MIDI instruments have one service instance, so adding more than
   one here may confuse some clients.

Rules
-----

When device and node objects are created by the BlueZ monitor, they can be
configured using rules. These rules allow matching the existing properties of
these objects and updating them with new values. This is the main way of
configuring Bluetooth device settings.

These rules can be set in the ``monitor.bluez.rules`` section of the WirePlumber
configuration file.

Example:

.. code-block::

   monitor.bluez.rules = [
     {
       matches = [
         {
           ## This matches all bluetooth devices.
           device.name = "~bluez_card.*"
         }
       ]
       actions = {
         update-props = {
           bluez5.auto-connect = [ hfp_hf hsp_hs a2dp_sink hfp_ag hsp_ag a2dp_source ]
           bluez5.hw-volume = [ hfp_hf hsp_hs a2dp_sink hfp_ag hsp_ag a2dp_source ]
           bluez5.a2dp.ldac.quality = "auto"
           bluez5.a2dp.aac.bitratemode = 0
           bluez5.a2dp.opus.pro.application = "audio"
           bluez5.a2dp.opus.pro.bidi.application = "audio"
         }
       }
     }
     {
       matches = [
         {
           ## Matches all sources.
           node.name = "~bluez_input.*"
         }
         {
           ## Matches all sinks.
           node.name = "~bluez_output.*"
         }
       ]
       actions = {
         update-props = {
           bluez5.media-source-role = "input"

           # Common node & audio adapter properties may also be set here
           node.nick              = "My Node"
           priority.driver        = 100
           priority.session       = 100
           node.pause-on-idle     = false
           resample.quality       = 4
           channelmix.normalize   = false
           channelmix.mix-lfe     = false
           session.suspend-timeout-seconds = 5
           monitor.channel-volumes = false
         }
       }
     }
   ]

Device properties
^^^^^^^^^^^^^^^^^

The following properties can be set on device objects:

.. describe:: bluez5.auto-connect

   Auto-connect device profiles on start up or when only partial profiles have
   connected. Disabled by default if the property is not specified.

   Supported values are: ``hfp_hf``, ``hsp_hs``, ``a2dp_sink``, ``hfp_ag``,
   ``hsp_ag`` and ``a2dp_source``.

   :Default value: ``[]``
   :Type: array of strings

.. describe:: bluez5.hw-volume

   Enable hardware volume controls on these profiles.

   Supported values are: ``hfp_hf``, ``hsp_hs``, ``a2dp_sink``, ``hfp_ag``,
   ``hsp_ag`` and ``a2dp_source``.

   :Default value: ``[ hfp_ag hsp_ag a2dp_source ]``
   :Type: array of strings

.. describe:: bluez5.a2dp.ldac.quality

   LDAC encoding quality.

   Available values: ``auto`` (Adaptive Bitrate, default), ``hq`` (High
   Quality, 990/909kbps), ``sq`` (Standard Quality, 660/606kbps) and ``mq``
   (Mobile use Quality, 330/303kbps).

   :Default value: ``auto``
   :Type: string

.. describe:: bluez5.a2dp.aac.bitratemode

   AAC variable bitrate mode.

   Available values: 0 (cbr, default), 1-5 (quality level).

   :Default value: ``0``
   :Type: integer

.. describe:: bluez5.a2dp.opus.pro.application

   Opus Pro Audio encoding mode.

   Available values: ``audio``, ``voip``, ``lowdelay``.

   :Default value: ``audio``
   :Type: string

.. describe:: bluez5.a2dp.opus.pro.bidi.application

   Opus Pro Audio encoding mode for bidirectional audio.

   Available values: ``audio``, ``voip``, ``lowdelay``.

   :Default value: ``audio``
   :Type: string

.. describe:: device.profile

   The profile that is activated initially when the device is connected.

   Available values: ``a2dp-sink`` (default) or ``headset-head-unit``.

   :Default value: ``a2dp-sink``
   :Type: string

Node properties
^^^^^^^^^^^^^^^

The following properties can be set on node objects:

.. describe:: bluez5.media-source-role

   Media source role, ``input`` or ``playback``. This controls how a media
   source device, such as a smartphone, is used by the system. Defaults to
   ``playback``, playing the incoming stream out to speakers. Set to ``input``
   to use the smartphone as an input for apps (like a microphone).

   :Default value: ``playback``
   :Type: string

MIDI Rules
----------

Similarly to the above rules, the BlueZ MIDI monitor also supports rules that
can be used to configure MIDI nodes when they are created.

These rules can be set in the ``monitor.bluez-midi.rules`` section of the
WirePlumber configuration file.

Example:

.. code-block::

   monitor.bluez-midi.rules = [
     {
       matches = [
         {
           node.name = "~bluez_midi.*"
         }
       ]
       actions = {
         update-props = {
           node.nick = "My Node"
           priority.driver = 100
           priority.session = 100
           node.pause-on-idle = false
           session.suspend-timeout-seconds = 5
           node.latency-offset-msec = 0
         }
       }
     }
   ]

.. note::

   It is possible to also match MIDI server nodes by testing the ``node.name``
   property against the server node names that were set in the
   ``monitor.bluez-midi.servers`` section of the WirePlumber configuration file.

MIDI-specific properties
^^^^^^^^^^^^^^^^^^^^^^^^

.. describe:: node.latency-offset-msec

   Latency adjustment to apply on the node. Larger values add a
   constant latency, but reduces timing jitter caused by Bluetooth
   transport.

   :Default value: ``0``
   :Type: integer (milliseconds)
