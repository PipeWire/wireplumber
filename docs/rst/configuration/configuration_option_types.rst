.. _configuration_option_types:

WirePlumber has different types of configuration options, based on the type of
the value they take.

Simple Configuration Options
----------------------------
Simple configuration options take boolean, integer or floating point values.
They are always defined under ``wireplumber.settings`` JSON section.
Below are few examples.

  .. code-block::

    monitor.bluetooth.enable-logind = true

  This is a simple configuration option from :ref:`bluetooth configuration file<config_bluetooth>`

  .. code-block::

    device.default-volume = 0.064

  This is an example from :ref:`device configuration file<config_device>`. The
  configuration option takes a floating point value of volume.

All the simple configuration options can be :ref:`overridden<manipulate_configuration_options>` or can be
changed :ref:`live<live_configuration_options>`. They are commented in the default location,
as they are built into WirePlumber. Below is the explanation of each of these
simple configuration options.

They can also be :ref:`accessed<access_configuration_options>`  from modules and scripts. New
configuration options can also be :ref:`defined<access_configuration_options>`.

Complex Configuration Options
-----------------------------
Complex configuration options take JSON array or JSON object values. These are the lists of
properties or rules. Below are a few examples.

  .. code-block::

    monitor.alsa.midi.node-properties = {
      ## MIDI bridge node properties

      ## Name set for the node with ALSA MIDI ports
      node.name = "Midi-Bridge"

      ## Removes longname/number from MIDI port names
      api.alsa.disable-longname = true
    }

This is a complex configuration option from :ref:`alsa configuration
file<config_alsa>`. Here this JSON object takes a list of properties.

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

This is a complex configuration option from :ref:`stream configuration
file<config_stream>`. This is an example of rule type of complex configuration
option. The properties are applied only if the `application.name` property of
the node is ``pw-play``

Below complex configuration option is an example of advanced syntax for defining
rule based configuration options. Read through the comments(lines starting with
`#`). They are taken from :ref:`alsa configuration file<config_alsa>`.

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


The complex configuration options can be either :ref:`overridden<manipulate_configuration_options>`  or
:ref:`extended<manipulate_configuration_options>` but they cannot be changed
:ref:`live<live_configuration_options>`

They can also be :ref:`accessed<access_configuration_options>`  from modules and scripts. New
configuration options can also be :ref:`defined<access_configuration_options>`.

.. note::

  Complex configuration options are the JSON section names themselves, where as simple configuration options
  are defined under the ``wireplumber.settings`` JSON section.  This is the
  subtle difference between these two types of configuration options.
