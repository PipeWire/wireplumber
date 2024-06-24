Linking Scripts
===============

These scripts contain all the logic for creating links between nodes.
This involves, to a large extent, deciding which links to create.

Hooks
-----

The hooks in this section are organized in 3 sub-categories. The first category
includes hooks that are triggered by changes in the graph. Some of them are tasked
to schedule a "rescan-for-linking" event, which is the lowest priority event and
its purpose is to scan through all the linkable session items and link them
to a particular target. The "rescan-for-linking" event is always scheduled to run
once for all the graph changes in a cycle. This is achieved by flagging the event
as already scheduled in the module-standard-event-source; this flag is then cleared
by a hook that runs on this event.

Selecting a target for each linkable and linking to it is deferred to another
set of hooks by pushing a "select-target" event for each linkable. This event
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
     - linkable SI added|removed or metadata-changed
     - schedules rescan-for-linking event

   * - linking/linkable-removed
     - rescan.lua
     - linkable SI removed
     - destroys links related to the removed linkable

   * - linking/follow
     - move-follow.lua
     - metadata-changed
     - schedules rescan-for-linking when the configured default sources/sinks are changed by the user

   * - linking/move
     - move-follow.lua
     - metadata-changed
     - schedules rescan-for-linking when node target metadata properties are changed

   * - linking/rescan-virtual-links
     - rescan-virtual-links.lua
     - link SI added, removed or metadata-changed
     -

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

   * - linking/rescan
     - rescan.lua
     - schedules select-target for each linkable session item

.. list-table:: select-target hooks, in order of execution
   :header-rows: 1
   :width: 100%
   :widths: 20 20 60

   * - Hook name
     - File
     - Description

   * - linking/find-virtual-target
     - find-virtual-target.lua
     -

   * - linking/find-defined-target
     - find-defined-target.lua
     - Select the target that has been defined explicitly by the 'target.object' property or metadata

   * - linking/find-filter-target
     - find-filter-target.lua
     - Select the target of a filter node, if the subject is a filter node

   * - linking/find-media-role-target
     - find-media-role-target.lua
     - Select the target based on the stream's media.role and the target's device.intended-roles

   * - linking/find-default-target
     - find-default-target.lua
     - Select the default source/sink as target

   * - linking/find-best-target
     - find-best-target.lua
     - Select target based on priority.session

   * - linking/get-filter-from-target
     - get-filter-from-target.lua
     - Translate the found target to a filter target that should be linked instead

   * - linking/prepare-link
     - prepare-link.lua
     - Break existing link if needed, check if the target is available for linking; send error to the client if needed

   * - linking/link-target
     - link-target.lua
     - Create si-standard-link session item to create links between the subject linkable and the selected target
