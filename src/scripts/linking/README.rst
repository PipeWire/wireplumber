Linking Scripts
===============

These scripts contain all the logic for creating links between nodes.
This involves, to a large extent, deciding which links to create.

Hooks
-----

The hooks in this section are organized in 3 sub-categories. The first category
includes hooks that are triggered by changes in the graph. Some of them are tasked
to schedule a "rescan-for-linking" event, which is the lowest priority linking event and
its purpose is to scan through all the subject streams and link them
to a particular target. The "rescan-for-linking" event is always scheduled to run
once for all the graph changes in a cycle. This is achieved by flagging the event
as already scheduled in the module-standard-event-source; this flag is then cleared
by a hook that runs on this event.

Selecting a target for each stream and linking to it is deferred to another
set of hooks by pushing a "select-target" event for each stream. This event
is the highest priority event and therefore no other changes in the graph are
processed while targets are being selected.

.. list-table:: Hooks triggered by changes in the graph
   :header-rows: 1

   * - Hook name
     - File
     - Triggered by
     - Action

   * - linking/rescan-trigger
     - rescan.lua
     - stream added|removed or metadata-changed
     - schedules rescan-for-linking event

   * - linking/rescan-trigger-on-stream-added-removed
     - rescan-on-stream.lua
     - stream added|removed
     - schedules rescan-for-linking; can be disabled by setting ``hooks.linking.rescan-on-stream = disabled`` in ``wireplumber.profiles``

   * - linking/rescan-trigger-on-target-metadata-changed
     - rescan.lua
     - metadata-changed on ``target.object`` or ``target.node``
     - schedules rescan-for-linking when node target metadata properties are changed; registered only while the ``linking.allow-moving-streams`` setting is enabled

   * - linking/stream-added
     - rescan.lua
     - stream-added
     - re-enables rescanning (see linking/bluez-stream-removed)

   * - linking/bluez-stream-removed
     - rescan.lua
     - stream-removed on a ``bluez5`` device
     - disables rescanning for 2 seconds, to avoid audio being played on internal nodes while the BT device switches profiles

   * - linking/handle-media-role-links-on-added
     - handle-media-role-links.lua
     - stream link added
     - activates or deactivates role-based links based on role priorities and actions

   * - linking/handle-media-role-links-on-removed
     - handle-media-role-links.lua
     - stream link removed
     - activates or deactivates role-based links based on role priorities and actions

   * - linking/handle-media-role-links-on-metadata-changed
     - handle-media-role-links.lua
     - stream link metadata changed
     - activates or deactivates role-based links based on role priorities and actions

   * - linking/pause-mpris@track-links
     - pause-mpris.lua
     - link added|removed
     - tracks which MPRIS media players are linked to which sinks

   * - linking/pause-mpris
     - pause-mpris.lua
     - stream-removed on an input stream
     - pauses the MPRIS media applications whose streams were linked to the sink that is going away

.. note::

  The ``linking/pause-mpris*`` hooks are only registered while the
   ``linking.pause-playback`` setting is enabled.

.. list-table:: rescan-for-linking hooks, in order of execution
   :header-rows: 1
   :width: 100%
   :widths: 20 20 60

   * - Hook name
     - File
     - Description

   * - m-standard-event-source/rescan-done
     - module-standard-event-source.c
     - clears the rescan_scheduled flag

   * - linking/pause-mpris-disable-rescan
     - pause-mpris.lua
     - stops the rescan while MPRIS media players are still being paused, and re-schedules it afterwards; only registered while ``linking.pause-playback`` is enabled

   * - linking/rescan
     - rescan.lua
     - schedules select-target for each subject stream

.. list-table:: select-target hooks, in order of execution
   :header-rows: 1
   :width: 100%
   :widths: 20 20 60

   * - Hook name
     - File
     - Description

   * - linking/find-defined-target
     - find-defined-target.lua
     - Select the target that has been defined explicitly by the 'target.object' property or metadata

   * - linking/find-audio-group-target
     - find-audio-group-target.lua
     - Select the audio group loopback node as target, if the subject belongs to an audio group

   * - linking/find-filter-target
     - find-filter-target.lua
     - Select the target of a filter node, if the subject is a filter node

   * - linking/find-media-role-target
     - find-media-role-target.lua
     - Select the target based on the stream's media.role and the target's device.intended-roles

   * - linking/find-media-role-sink-target
     - find-media-role-sink-target.lua
     - Pick up the preferred target node for the output stream of a role-based loopback

   * - linking/find-default-target
     - find-default-target.lua
     - Select the default source/sink as target

   * - linking/find-best-target
     - find-best-target.lua
     - Select target based on priority.session

   * - linking/get-filter-from-target
     - get-filter-from-target.lua
     - Translate the found target to a filter target that should be linked instead

   * - linking/prepare-stream-link
     - prepare-stream-link.lua
     - Break existing link if needed, check if the target is available for linking; send error to the client if needed

   * - linking/create-stream-link
     - create-stream-link.lua
     - Create a stream link between the subject stream and the selected target

Extending target selection
--------------------------

The ``select-target`` hooks above form a chain: each one bypasses itself if a
target has already been picked, so the first hook that finds a target wins.
A custom script can therefore participate in target selection simply by
registering another hook on the ``select-target`` event and ordering it
relative to the stock hooks with ``before`` / ``after``.

:ref:`scripting_custom_scripts_target_selection` walks through such a hook in
full: the script itself, the ``wireplumber.conf.d`` fragment that defines a
component for it and enables it in the ``main`` profile, and the reasoning
behind the ordering key and the bypass guard.
