.. _config_link:

Linking Configuration
=====================

``wireplumber.conf.d/link.conf`` deals with the linking configuration.

Simple Configs
--------------

All the :ref:`simple configs<config_types>` can be
:ref:`overridden<manipulate_config>` or can be changed
:ref:`live<live_configs>`. They are commented in the default location, as they
are built into WirePlumber. Below is the explanation of each of these simple
configs.

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

Complex Configs
---------------

The :ref:`complex configs<config_types>`  can be either
:ref:`overridden<manipulate_config>`  or :ref:`extended<manipulate_config>`
but they cannot be changed :ref:`live<live_configs>`

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
