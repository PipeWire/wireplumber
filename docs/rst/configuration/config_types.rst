.. _configs_types:

Simple Configs
--------------
Simple configs take boolean, integer or floating point values. They are They are
always defined under ``wireplumber.settings`` JSON section. Below are few
examples.

  .. code-block::

    monitor.bluetooth.enable-logind = true

  This is a simple config from :ref:`bluetooth config<config_bluetooth>`

  .. code-block::

    device.default-volume = 0.064

  This is an example from :ref:`device config<config_device>`. The config takes
  a floating point value of volume.

All the simple configs can be :ref:`overridden<manipulate_config>` or can be
changed :ref:`live<live_configs>`. They are commented in the default location,
as they are built into WirePlumber. Below is the explanation of each of these
simple configs.

They can also be :ref:`accessed<access_configs>`  from modules and scripts. New
configs can also be :ref:`defined<access_configs>`.

Complex Configs
---------------
Complex configs take JSON array or JSON object values. These are the lists of
properties or rules. Below are a few examples.

  .. code-block::

    monitor.alsa.properties = {
    }

  This is a complex config from :ref:`alsa config<config_alsa>`. Here this JSON
  object takes a list of properties.


  .. code-block::

    stream.rules = [
      # The list of stream rules

      # This rule example allows setting properties on the "pw-play" stream.
       {
         matches = [
             ## Matches all devices
             { application.name = "pw-play" }
         ]
         update-props = {
           state.restore-props = false
           state.restore-target = false
           state.default-channel-volume = 0.5
         }
       }
    ]

  This is a complex config from :ref:`stream config<config_stream>`. This is an
  example of rule type of complex config. The properties are applied only if the
  `application.name` property of the node is ``pw-play``

  Below complex config is an example of advanced syntax for defining rule based
  configs. Read through the comments(lines starting with `#`). They are taken
  from :ref:`alsa<config_alsa>` config.

  .. code-block::

    monitor.alsa.rules = [
      {
        matches = [
          {
            # This matches the needed sound card.
            device.name = "<sound_card_name>"
          }
        ]
        actions = {
          update-props = {
            # Apply all the desired device settings here.
            api.alsa.use-acp = true
          }
        }
      }
      # multiple matches are possible
      {
        matches = [
          {
            # "~" triggers wild card evaluation, only "*" is supported.
            # Logical AND behavior with the JSON object
            device.name = "~my-sound-card*"
            device.product.name = "~Tiger*"
          }
        ]
        actions = {
          update-props = {
            # Apply all the desired device settings here.
            device.nick = "my-card"
          }
        }
      }
      {
        matches = [
          {
            # This matches all the input device nodes.
            # "~" triggers wild card evaluation, only "*" is supported.
            node.name = "~alsa_input.*"
          }
          # Logical OR behavior across the JSON objects, with in a match. So, either input or output nodes
          {
            # This matches all the output device nodes.
            node.name = "~alsa_output.*"
          }
        ]
        actions = {
          update-props = {
            # Apply all the desired node settings here.
            node.nick              = "My Node"
            node.description       = "My Node Description"
            api.alsa.period-size   = 1024
            api.alsa.period-num    = 2
            api.alsa.headroom      = 0

          }
        }
      }
      {
        matches = [
          {
            # "~" triggers wild card evaluation, only "*" is supported.
            node.name = "~libcamera*"
            device.api = "libcamera"
          }
          # all the conditions should be met with in the curly braces for the
          # match to evaluate to true
        ]
        actions = {
          update-props = {
            # Apply all the desired node settings here.
            node.nick = "my-libcam"
          }
        }
      }
    ]


The complex configs can be either :ref:`overridden<manipulate_config>`  or
:ref:`extended<manipulate_config>` but they cannot be changed
:ref:`live<live_configs>`

They can also be :ref:`accessed<access_configs>`  from modules and scripts. New
configs can also be :ref:`defined<access_configs>`.

.. note::

  Complex configs are the JSON section names themselves, where as simple configs
  are defined under the ``wireplumber.settings`` JSON section.  This is the
  subtle difference between these two types of configs.
