.. _config_link:

Linking Configuration
=====================

``wireplumber.conf.d/link.conf`` configuration file deals with the linking configuration.

Simple Configuration Options
----------------------------

All the :ref:`simple configuration options<configuration_option_types>` can be
:ref:`overridden<manipulate_configuration_options>` or can be changed
:ref:`live<live_configuration_options>`. They are commented in the default location, as they
are built into WirePlumber. Below is the explanation of each of these simple
configuration options.

.. code-block::

  linking.default.move = true

Moves session items when metadata ``target.object`` changes. Also responds to
`target.node` key. But `target.object` is the canonical key.

.. code-block::

  linking.default.follow = true

Moves session items to the default device when it has changed.

.. code-block::

  linking.default.filter-forward-format = false

Whether to forward the ports format of filter stream nodes to their
associated filter device nodes. This is needed for application to stream
surround audio if echo-cancel is enabled.

.. code-block::

  linking.default.audio-no-dsp = false

Set to ``true`` to disable channel splitting & merging on nodes and enable
passthrough of audio in the same format as the format of the device. Note that
this breaks JACK support; it is generally not recommended.

.. code-block::

  linking.default.duck-level = 0.3

How much to lower the volume of lower priority streams when ducking. Note that
this is a linear volume modifier (not cubic as in PulseAudio).

.. code-block::

  linking.bluetooth.use-persistent-storage = true

Whether to store state on the filesystem.

.. code-block::

  linking.bluetooth.media-role.use-headset-profile = true

Whether to use headset profile in the presence of an input stream.

Complex Configuration Options
-----------------------------

The :ref:`complex configuration options<configuration_option_types>`  can be either
:ref:`overridden<manipulate_configuration_options>`  or :ref:`extended<manipulate_configuration_options>`
but they cannot be changed :ref:`live<live_configuration_options>`. Below is the explanation of each
of these complex configuration options.

.. code-block::

  linking.bluetooth.media-role.applications = [
    "Firefox", "Chromium input", "Google Chrome input", "Brave input",
    "Microsoft Edge input", "Vivaldi input", "ZOOM VoiceEngine",
    "Telegram Desktop", "telegram-desktop", "linphone", "Mumble",
    "WEBRTC VoiceEngine", "Skype"
  ]

Application names correspond to application.name in stream properties.
Applications which do not set media.role but which should be considered for
role based profile switching can be specified here.
