.. _config_bluetooth:

Bluetooth configuration
=======================

Using the same format as the :ref:`ALSA monitor <config_alsa>`, the
configuration file ``bluetooth.lua.d/50-bluez-config.lua`` is charged
to configure the Bluetooth devices and nodes created by WirePlumber.

* *bluez_monitor.properties*

  A Lua object that contains generic client configuration properties in the
  for of key pairs.

  Example:

  .. code-block:: lua

    bluez_monitor.properties = {
      ["bluez5.enable-msbc"] = true,
    }

  This example will enable the MSBC codec in connected Bluetooth devices that
  support it.

  The list of valid properties are:

  .. code-block:: lua

    ["bluez5.enable-sbc-xq"] = true

  Enables the SBC-XQ codec in connected Blueooth devices that support it

  .. code-block:: lua

    ["bluez5.enable-msbc"] = true

  Enables the MSBC codec in connected Blueooth devices that support it

  .. code-block:: lua

    ["bluez5.enable-hw-volume"] = true

  Enables hardware volume controls in Bluetooth devices that support it

  .. code-block:: lua

    ["bluez5.headset-roles"] = "[ hsp_hs hsp_ag hfp_hf hfp_ag ]"

  Enabled headset roles (default: [ hsp_hs hfp_ag ]), this property only applies
  to native backend. Currently some headsets (Sony WH-1000XM3) are not working
  with both hsp_ag and hfp_ag enabled, disable either hsp_ag or hfp_ag to work
  around it.

  Supported headset roles: ``hsp_hs`` (HSP Headset), ``hsp_ag`` (HSP Audio Gateway),
  ``hfp_hf`` (HFP Hands-Free) and ``hfp_ag`` (HFP Audio Gateway)

  .. code-block:: lua

    ["bluez5.codecs"] = "[ sbc sbc_xq aac ]"

  Enables ``sbc``, ``sbc_zq`` and ``aac`` A2DP codecs.

  Supported codecs: ``sbc``, ``sbc_xq``, ``aac``, ``ldac``, ``aptx``,
  ``aptx_hd``, ``aptx_ll``, ``aptx_ll_duplex``, ``faststream``, ``faststream_duplex``.

  All codecs are supported by default.

  .. code-block:: lua

    ["bluez5.hfphsp-backend"] = "native"

  HFP/HSP backend (default: native). Available values: ``any``, ``none``,
  ``hsphfpd``, ``ofono`` or ``native``.

  .. code-block:: lua

    ["bluez5.default.rate"] = 48000

  The bluetooth default audio rate.

  .. code-block:: lua

    ["bluez5.default.channels"] = 2

  The bluetooth default number of channels.

* *bluez_monitor.rules*

  Like in the :ref:`ALSA configuration <config_alsa>`, this is a Lua array that
  can contain objects with rules for a Bluetooth device or node.
  Those objects have 2 properties. The first one is ``matches``, which allows
  users to define rules to match a Bluetooth device or node.
  The second property is ``apply_properties``, and it is used to apply
  properties on the matched Bluetooth device or node.

  Example:

  .. code-block:: lua

    {
      matches = {
        {
          { "device.name", "matches", "bluez_card.*" },
        },
      },
      apply_properties = {
         ["bluez5.auto-connect"]  = "[ hfp_hf hsp_hs a2dp_sink ]"
      }
    }

  This will set the auto-connect property to ``hfp_hf``, ``hsp_hs`` and ``a2dp_sink``
  on bluetooth devices whose name matches the ``bluez_card.*`` pattern.

  A list of valid properties are:

  .. code-block:: lua

    ["bluez5.auto-connect"] = "[ hfp_hf hsp_hs a2dp_sink ]"

  Auto-connect device profiles on start up or when only partial profiles have
  connected. Disabled by default if the property is not specified.

  Supported values are: ``hfp_hf``, ``hsp_hs``, ``a2dp_sink``, ``hfp_ag``,
  ``hsp_ag`` and ``a2dp_source``.

  .. code-block:: lua

    ["bluez5.hw-volume"] = "[ hfp_ag hsp_ag a2dp_source ]"

  Hardware volume controls (default: ``hfp_ag``, ``hsp_ag``, and ``a2dp_source``)

  Supported values are: ``hfp_hf``, ``hsp_hs``, ``a2dp_sink``, ``hfp_ag``,
  ``hsp_ag`` and ``a2dp_source``.

  .. code-block:: lua

    ["bluez5.a2dp.ldac.quality"] = "auto"

  LDAC encoding quality.

  Available values: ``auto`` (Adaptive Bitrate, default),
  ``hq`` (High Quality, 990/909kbps), ``sq`` (Standard Quality, 660/606kbps) and
  ``mq`` (Mobile use Quality, 330/303kbps).

  .. code-block:: lua

    ["bluez5.a2dp.aac.bitratemode"] = 0

  AAC variable bitrate mode.

  Available values: 0 (cbr, default), 1-5 (quality level).

  .. code-block:: lua

    ["device.profile"] = "a2dp-sink"

  Profile connected first.

  Available values: ``a2dp-sink`` (default) or ``headset-head-unit``.
