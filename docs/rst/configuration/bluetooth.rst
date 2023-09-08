.. _config_bluetooth:

Bluetooth Configuration
=======================

Bluetooth devices are created and managed by the session manager with the *bluez.lua*
monitor script. ``wireplumber.conf.d/bluetooth.conf`` contains :ref:`default
configs<manipulate_config>`  which control the bluez monitor.


Simple Configs
--------------

  All the :ref:`simple configs<configs_types>` can be
  :ref:`overridden<manipulate_config>` or can be changed
  :ref:`live<live_configs>`. They are commented in the default location as they
  are built into WirePlumber. Below is the explanation of each of these simple
  configs.

  .. code-block::

    monitor.bluetooth.enable-logind = true

  Enables the logind module, which arbitrates which user will be allowed to have
  bluetooth audio enabled at any given time (particularly useful if you are
  using GDM as a display manager, as the gdm user also launches pipewire and
  wireplumber). This requires access to the D-Bus user session; disable if you
  are running a system-wide instance of wireplumber.

Complex Configs
---------------

  The :ref:`complex configs<configs_types>`  can be either
  :ref:`overridden<manipulate_config>`  or :ref:`extended<manipulate_config>` but they
  cannot be changed :ref:`live<live_configs>`

  .. code-block::

    monitor.bluetooth.properties = {
    }

  The properties used when constructing the 'api.bluez5.enum.dbus' plugin can go
  into this section.

  The list of all the valid properties are below

  The below three features do not work on all headsets, so they are enabled
  by default based on the hardware database. They can also be
  forced on/off for all devices by the following options:

  .. code-block::

    bluez5.enable-sbc-xq = true

  Enables the SBC-XQ codec in connected Blueooth devices that support it

  .. code-block::

    bluez5.enable-msbc = true

  Enables the MSBC codec in connected Blueooth devices that support it

  .. code-block::

    bluez5.enable-hw-volume = true

  Enables hardware volume controls in Bluetooth devices that support it

  .. code-block::

    bluez5.roles = "[ a2dp_sink a2dp_source bap_sink bap_source hsp_hs hsp_ag hfp_hf hfp_ag ]"

  Enabled roles (default: [ a2dp_sink a2dp_source bap_sink bap_source hfp_hf hfp_ag ])

  Currently some headsets (Sony WH-1000XM3) are not working
  with both hsp_ag and hfp_ag enabled, so by default we enable only HFP.

  Supported headset roles: ``hsp_hs`` (HSP Headset), ``hsp_ag`` (HSP Audio
  Gateway), ``hfp_hf`` (HFP Hands-Free), ``hfp_ag`` (HFP Audio Gateway),
  ``a2dp_sink`` (A2DP Audio Sink), ``a2dp_source`` (A2DP Audio Source),
  ``bap_sink`` (LE Audio Basic Audio Profile Sink) and ``bap_source`` (LE Audio
  Basic Audio Profile Source)

  .. code-block::

    bluez5.codecs = "[ sbc sbc_xq aac ldac aptx aptx_hd aptx_ll aptx_ll_duplex faststream faststream_duplex ]"

  Enables ``sbc``, ``sbc_xq``, ``aac``, ``ldac``, ``aptx``, ``aptx_hd``,
  ``aptx_ll``, ``aptx_ll_duplex``, ``faststream``, and  ``faststream_duplex`` A2DP
  codecs.

  Supported codecs: ``sbc``, ``sbc_xq``, ``aac``, ``ldac``, ``aptx``,
  ``aptx_hd``, ``aptx_ll``, ``aptx_ll_duplex``, ``faststream``,
  ``faststream_duplex``.

  All codecs are supported by default.

  .. code-block::

    bluez5.hfphsp-backend = "native"

  HFP/HSP backend (default: native). Available values: ``any``, ``none``,
  ``hsphfpd``, ``ofono`` or ``native``.

  .. code-block::

    bluez5.hfphsp-backend-native-modem = "none"

  HFP/HSP native backend modem (default: none). Available values: ``none``,
  ``any`` or the modem device string as found in 'Device' property of
  ``org.freedesktop.ModemManager1.Modem`` interface

  .. code-block::

    bluez5.default.rate = 48000

  The bluetooth default audio rate.

  .. code-block::

    bluez5.hw-offload-sco = false

  HFP/HSP hardware offload SCO support (default: false).

  .. code-block::

    bluez5.default.channels = 2

  The bluetooth default number of channels.

  .. code-block::

    bluez5.dummy-avrcp-player = true

  Register dummy AVRCP player, required for AVRCP volume function. Disable if
  you are running mpris-proxy or equivalent.



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

  Opus Pro Audio mode settings

Device settings
^^^^^^^^^^^^^^^
The following settings can be configured on devices created by the Blueooth monitor.


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

  .. code-block::

    bluez5.a2dp.opus.pro.application = "audio"
    bluez5.a2dp.opus.pro.bidi.application = "audio"

  Opus Pro Audio encoding mode:
  Available values: ``audio``, ``voip`` and ``lowdelay``


  .. code-block::

    monitor.bluetooth-midi.rules = [
        {
          matches = [
            {
              # Matches all bluez midi nodes.
              node.name = "~bluez_midi*"
            }
          ]
          update-props = {
            node.nick = "My Node"
            priority.driver = 100
            priority.session = 100
            node.pause-on-idle = false
            session.suspend-timeout-seconds = 5
            monitor.channel-volumes = false
          }
       }
    ]

The rules to construct the bluetooth midi nodes can go here.


Examples
^^^^^^^^

The below examples contain rules configuring properties on both devices and
device nodes.

  .. code-block::

    monitor.bluetooth.rules = [
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
            # This will set the auto-connect property to ``hfp_hf``, ``hsp_hs`` and
            # ``a2dp_sink`` on bluetooth devices whose name matches the ``bluez_card.*``
            # pattern.
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
       update-props = {
         node.nick              = "My Node"
         priority.driver        = 100
         priority.session       = 100
         node.pause-on-idle     = false
         resample.quality       = 4
         channelmix.normalize   = false
         channelmix.mix-lfe     = false
         session.suspend-timeout-seconds = 5
         monitor.channel-volumes = false
         ## Media source role, "input" or "playback"
         ## Defaults to "playback", playing stream to speakers
         ## Set to "input" to use as an input for apps
         bluez5.media-source-role = "input"
       }
     }
    ]

.. note::

    Bluetooth Device and Node settings go into monitor.bluetooth-midi.rules.
    monitor.bluetooth.rules JSON sections and they are also called rule based
    configs in that the device or node will have to be filtered first using the
    match rules. Settings can be set either on all the devices/nodes or on
    specific devices/nodes, depending on how the match rules are setup.

.. note::

    The properties set in the update-props section, can be PipeWire properties
    which trigger some action or they can be new properties that the devices or
    nodes will be created with. These new properties can be read or written from
    scripts or modules. After the creation of the devices and nodes new
    properties cannot be created on them.

