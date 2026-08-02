.. _config_stream:

Stream configuration
====================

*Streams* are the nodes that applications create to play back or capture audio.
WirePlumber remembers their volume, mute state, channel map and target device,
and restores them the next time the same application appears.

The settings that control this globally — ``node.stream.restore-props``,
``node.stream.restore-target`` and the default volumes — are described in
:ref:`config_settings`. This page describes how to override that behaviour for
individual streams.

.. describe:: stream.rules

   Rules that are matched against a stream's properties when it appears,
   allowing its properties to be modified. The syntax is the same as for the
   monitor rules; see :ref:`config_modifying_configuration`.

   Because matching happens on the stream's PipeWire properties, rules can
   target a specific application:

   .. code-block::

      stream.rules = [
        {
          matches = [
            { application.name = "pw-play" }
          ]
          actions = {
            update-props = {
              state.restore-props  = false
              state.restore-target = false
              state.default-volume = 1.0
            }
          }
        }
      ]

Per-stream state properties
---------------------------

The following properties are read by the state restoring logic. They are most
useful when set through ``stream.rules``, but an application may also set them
on its own nodes.

.. describe:: state.restore-props

   Set to ``false`` to prevent WirePlumber from restoring this stream's volume,
   mute state and channel map, even when ``node.stream.restore-props`` is
   enabled globally.

.. describe:: state.restore-target

   Set to ``false`` to prevent WirePlumber from restoring the target device
   this stream was last linked to, even when ``node.stream.restore-target`` is
   enabled globally.

.. describe:: state.default-volume

   The volume to give this stream when it has no previously stored volume.
   Overrides ``node.stream.default-playback-volume`` and
   ``node.stream.default-capture-volume`` for this stream.

Examples
--------

A commented example fragment ships as ``stream.conf``; see
:ref:`config_example_fragments`.
