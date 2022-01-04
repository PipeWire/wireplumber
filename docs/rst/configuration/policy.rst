.. _config_policy:

Policy Configuration
====================

policy.lua.d/10-default-policy.lua
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This file contains generic default policy properties that can be configured.

* *default_policy.policy*

  This is a Lua object that contains several properties that change the
  behavior of the default WirePlumber policy.

  Example:

  .. code-block:: lua

    default_policy.policy = {
      ["move"] = true,
    }

  The above example will set the ``move`` policy property to ``true``.

  The list of supported properties are:

  .. code-block:: lua

    ["move"] = true

  Moves session items when metadata ``target.node`` changes.

  .. code-block:: lua

    ["follow"] = true

  Moves session items to the default device when it has changed.

  .. code-block:: lua

    ["audio.no-dsp"] = false

  Set to ``true`` to disable channel splitting & merging on nodes and enable
  passthrough of audio in the same format as the format of the device. Note that
  this breaks JACK support; it is generally not recommended.

  .. code-block:: lua

    ["duck.level"] = 0.3

  How much to lower the volume of lower priority streams when ducking. Note that
  this is a linear volume modifier (not cubic as in PulseAudio).

policy.lua.d/50-endpoints-config.lua
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Endpoints are objects that can group multiple clients into different groups or
  roles. This is useful if a user wants to apply specific actions when a client
  is connected to a particular role/endpoint. This configuration file allows
  users to configure those endpoints and their actions.

* *default_policy.policy.roles*

  This is a Lua array with objects defining the actions of each role.

  Example:

  .. code-block:: lua

    default_policy.policy.roles = {
      ["Multimedia"] = {
        ["alias"] = { "Movie", "Music", "Game" },
        ["priority"] = 10,
        ["action.default"] = "mix",
      }
      ["Notification"] = {
        ["priority"] = 20,
        ["action.default"] = "duck",
        ["action.Notification"] = "mix",
      }
    }

  The above example defines actions for both ``Multimedia`` and ``Notification``
  roles. Since the Notification role has more priority than the Multimedia
  role, when a client connects to the Notification endpoint, it will ``duck``
  the volume of all Multimedia clients. If Multiple Notification clients want
  to play audio, only the Notifications audio will be mixed.

  Possible values of actions are: ``mix`` (Mixes audio),
  ``duck`` (Mixes and lowers the audio volume) or ``cork`` (Pauses audio).

* *default_policy.policy.endpoints*

  This is a Lua array with objects defining the endpoints that the user wants
  to create.

  Example:

  .. code-block:: lua

    default_policy.endpoints = {
      ["endpoint.multimedia"] = {
        ["media.class"] = "Audio/Sink",
        ["role"] = "Multimedia",
      }
    },
    ["endpoint.notifications"] = {
      ["media.class"] = "Audio/Sink",
      ["role"] = "Notification",
    }

  This example creates 2 endpoints, with names ``endpoint.multimedia`` and
  ``endpoint.notifications``; and assigned roles ``Multimedia`` and ``Notification``
  respectively. Both endpoints have ``Audio/Sink`` media class, and so are only
  used for playback.
