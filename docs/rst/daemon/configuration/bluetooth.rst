.. _config_bluetooth:

Bluetooth configuration
=======================

Using the same format as the :ref:`ALSA monitor <config_alsa>`, the
configuration file ``wireplumber.conf.d/bluetooth.conf`` configures
the Bluetooth devices and nodes created by WirePlumber.

* *Settings*

  Example:

  .. code-block::

    monitor.bluez.properties = {
      bluez5.roles = "[ a2dp_sink a2dp_source bap_sink bap_source hsp_hs hsp_ag hfp_hf hfp_ag ]"
    }

  Enabled headset roles (default: [ a2dp_sink a2dp_source bap_sink bap_source hsp_hs hsp_ag hfp_hf hfp_ag ]).
  Some headsets (e.g. Sony WH-1000XM3) do not work with both hsp_ag and hfp_ag enabled,
  so `hsp_ag` and `hfp_ag` are disabled by default.

  Supported roles:

  - ``hsp_hs`` (HSP Headset)
  - ``hsp_ag`` (HSP Audio Gateway),
  - ``hfp_hf`` (HFP Hands-Free),
  - ``hfp_ag`` (HFP Audio Gateway)
  - ``a2dp_sink`` (A2DP Audio Sink)
  - ``a2dp_source`` (A2DP Audio Source)
  - ``bap_sink`` (LE Audio Basic Audio Profile Sink)
  - ``bap_source`` (LE Audio Basic Audio Profile Source)

  .. code-block::

    bluez5.codecs = "[ sbc sbc_xq aac ]"

  Enables the specified A2DP codecs. All codecs are enabled by default.

  Supported codecs:``sbc``, ``sbc_xq``, ``aac``, ``ldac``, ``aptx``,
  ``aptx_hd``, ``aptx_ll``, ``aptx_ll_duplex``, ``faststream``,
  ``faststream_duplex``, ``lc3plus_h3``, ``opus_05``, ``opus_05_51``, ``opus_05_71``,
  ``opus_05_duplex``, ``opus_05_pro``, ``lc3``.

  .. code-block::

    bluez5.hfphsp-backend = "native"

  HFP/HSP backend (default: native). Available values: ``any``, ``none``,
  ``hsphfpd``, ``ofono`` or ``native``.

  .. code-block::

    bluez5.hfphsp-backend-native-modem = "none"

  Modem to use for native HFP/HSP backend ModemManager support. When enabled,
  PipeWire will forward HFP commands to the specified ModemManager device.
  This corresponds to the 'Device' property of the org.freedesktop.ModemManager1.Modem
  interface. May also be ``any`` to use any available modem device.

  .. code-block::

     bluez5.hw-offload-sco = false

  HFP/HSP hardware offload SCO support (default: false).  Using this
  feature requires a custom WirePlumber script that handles audio
  routing in a platform-specific way. See
  ``tests/examples/bt-pinephone.lua`` for an example.

  .. code-block::

    bluez5.default.rate = 48000

  The bluetooth default audio rate.

  .. code-block::

    bluez5.default.channels = 2

  The Bluetooth default number of channels.

  .. code-block::

     bluez5.dummy-avrcp-player = false

  Register dummy AVRCP player. Some devices have wrongly functioning
  volume or playback controls if this is not enabled. Disabled by default.

  .. code-block::

    bluez5.enable-msbc = true,
    bluez5.enable-sbc-xq = true
    bluez5.enable-hw-volume = true

  By default MSBC and SBC-XQ codecs and hardware volume is enabled,
  except if disabled by a hardware quirk database. You can force
  them to be enabled regardless also for devices where the database disables
  it with these options.

  .. code-block::

     bluez5.a2dp.opus.pro.channels = 3
     bluez5.a2dp.opus.pro.coupled-streams = 1
     bluez5.a2dp.opus.pro.locations = "FL,FR,LFE"
     bluez5.a2dp.opus.pro.max-bitrate = 600000
     bluez5.a2dp.opus.pro.frame-dms = 50
     bluez5.a2dp.opus.pro.bidi.channels = 1
     bluez5.a2dp.opus.pro.bidi.coupled-streams = 0
     bluez5.a2dp.opus.pro.bidi.locations = "FC"
     bluez5.a2dp.opus.pro.bidi.max-bitrate = 160000
     bluez5.a2dp.opus.pro.bidi.frame-dms = 400

   Options for a custom multichannel Opus codec, which can be used to
   transport audio between devices running PipeWire.

* *MIDI Settings*

  Example:

  .. code-block::

     monitor.bluez-midi.servers = [ "bluez_midi.server" ]

  List of MIDI server node names. Each node name given will create a new instance
  of a BLE MIDI service. Typical BLE MIDI instruments have on service instance,
  so adding more than one here may confuse some clients. The node property matching
  rules below apply also to these servers.

* *Rules*

  Example:

  .. code-block::

      monitor.bluez.rules = [
          {
            matches = [
              {
                # This matches the needed sound card.
                device.name = "<bluez_sound_card_name>"
              }
            ]
            actions = {
              update-props = {
                # Apply all the desired device settings here.
                bluez5.auto-connect  = "[ hfp_hf hsp_hs a2dp_sink ]"
              }
            }
          }
          {
            matches = [
              # This matches the needed node.
              {
                node.name = "<node_name>"
              }
            ]
            actions = {
              # Apply all the desired node specific settings here.
              update-props = {
                node.nick              = "My Node"
                priority.driver        = 100
                session.suspend-timeout-seconds = 5
              }
            }
          }
        ]

  This will set the auto-connect property to ``hfp_hf``, ``hsp_hs`` and
  ``a2dp_sink`` on bluetooth devices whose name matches the ``bluez_card.*``
  pattern.

  A list of valid properties are:

  .. code-block::

    bluez5.auto-connect = "[ hfp_hf hsp_hs a2dp_sink ]"

  Auto-connect device profiles on start up or when only partial profiles have
  connected. Disabled by default if the property is not specified.

  Supported values are: ``hfp_hf``, ``hsp_hs``, ``a2dp_sink``, ``hfp_ag``,
  ``hsp_ag`` and ``a2dp_source``.

  .. code-block::

    bluez5.hw-volume = "[ hfp_ag hsp_ag a2dp_source ]"

  Hardware volume controls (default: ``hfp_ag``, ``hsp_ag``, and ``a2dp_source``)

  Supported values are: ``hfp_hf``, ``hsp_hs``, ``a2dp_sink``, ``hfp_ag``,
  ``hsp_ag`` and ``a2dp_source``.

  .. code-block::

    bluez5.a2dp.ldac.quality = "auto"

  LDAC encoding quality.

  Available values: ``auto`` (Adaptive Bitrate, default),
  ``hq`` (High Quality, 990/909kbps), ``sq`` (Standard Quality, 660/606kbps) and
  ``mq`` (Mobile use Quality, 330/303kbps).

  .. code-block::

    bluez5.a2dp.aac.bitratemode = 0

  AAC variable bitrate mode.

  Available values: 0 (cbr, default), 1-5 (quality level).

  .. code-block::

    device.profile = "a2dp-sink"

  Profile connected first.

  Available values: ``a2dp-sink`` (default) or ``headset-head-unit``.

* *MIDI Rules*

  Example:

  .. code-block::

     monitor.bluez-midi.rules = [
       {
         matches = [
           {
             node.name = "~bluez_midi*"
           }
         ]
         actions = {
           update-props = {
             node.nick = "My Node"
             priority.driver = 100
             priority.session = 100
             node.pause-on-idle = false
             session.suspend-timeout-seconds = 5
             monitor.channel-volumes = false
             node.latency-offset-msec = 0
           }
         }
       }
     ]

  Allows changing well-known node settings.

  In addition, allows changing some MIDI-specific settings:

  .. code-block::

     node.latency-offset-msec = 0

  Latency adjustment to apply on the node. Larger values add a
  constant latency, but reduces timing jitter caused by Bluetooth
  transport.
