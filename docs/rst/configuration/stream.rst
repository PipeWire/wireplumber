.. _config_stream:

Stream Configuration
====================

``wireplumber.conf.d/stream.conf`` configuration file deals with the stream
configuration, streams are the playback or capture streams coming from/going to
PipeWire clients or apps.

Simple Configuration Options
----------------------------

All the :ref:`simple configuration options<configuration_option_types>` can be
:ref:`overridden<manipulate_configuration_options>` or can be changed
:ref:`live<live_configuration_options>`. They are commented in the default location, as they
are built into WirePlumber. Below is the explanation of each of these simple
configuration options.

.. code-block::

  stream.restore-props = true

WirePlumber recognizes the client/app from which the stream is originating and
always stores the stream priorities like volume, channel volumes, mute status
and channel map. If this configuration option is true, WirePlumber will restore
the previously stored stream properties.

When set to `false`, the above stream properties will be initialized to
default values irrespective of the previous values.


.. code-block::

  stream.restore-target = true

WirePlumber recognizes the client/app from which the stream is originating and
always stores the target to which the stream is linked and when the stream
shows up for the second time. WirePlumber will try to link it to the this
stored target.

.. code-block::

  stream.default-channel-volume = 1.0

The default channel volume for new streams whose props were never saved
previously. This is only used if "stream.restore-props" is set to true.

Complex Configuration Options
-----------------------------

The :ref:`complex configuration options<configuration_option_types>`  can be either
:ref:`overridden<manipulate_configuration_options>`  or :ref:`extended<manipulate_configuration_options>`
but they cannot be changed :ref:`live<live_configuration_options>`. Below is the explanation of each
of these complex configuration options.

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

The stream specific rules go into ``stream.rules`` section. For example with the
above rule the stream props and target will NOT be restored for pw-play app streams.
