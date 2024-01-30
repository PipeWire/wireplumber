.. _config_policy:

Policy Configuration
====================

wireplumber.conf.d/policy.conf
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This file contains generic default policy properties that can be configured.

* *Settings*

  Example:

  .. code-block::

    wireplumber.properties = {
      default-policy-move = true
    }

  The above example will set the ``move`` policy property to ``true``.

  The list of supported properties are:

  .. code-block::

    default-policy-move = true

  Moves session items when metadata ``target.node`` changes.

  .. code-block::

    default-policy-follow = true

  Moves session items to the default device when it has changed.

  .. code-block::

    default-policy-audio.no-dsp = false

  Set to ``true`` to disable channel splitting & merging on nodes and enable
  passthrough of audio in the same format as the format of the device. Note that
  this breaks JACK support; it is generally not recommended.

  .. code-block::

    default-policy-duck.level = 0.3

  How much to lower the volume of lower priority streams when ducking. Note that
  this is a linear volume modifier (not cubic as in PulseAudio).

