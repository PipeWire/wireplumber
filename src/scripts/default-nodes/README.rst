Default Nodes Scripts
=====================

These scripts contain logic to select default source and sink nodes and also
manage user preferences regarding those.

Hooks
-----

Management of the default source and sink nodes is implemented by scanning all
the available nodes in the graph and assigning them a priority based on certain
logic. The node with the highest priority in each category becomes the default.

Scanning is implemented using a "rescan-for-default-nodes" event.
The "default-nodes/rescan-trigger" hook is the one that monitors graph changes
and schedules "rescan-for-default-nodes". Then, the "default-nodes/rescan"
hook is executed for the "rescan-for-default-nodes" event and it pushes a
"select-default-node" event for each one of the categories where a default node
is required:

 - Audio sink
 - Audio source
 - Video source

.. list-table:: Hooks triggered by changes in the graph
   :header-rows: 1
   :width: 100%
   :widths: 25 15 30 30

   * - Hook name
     - File
     - Triggered by
     - Action

   * - default-nodes/rescan-trigger
     - rescan.lua
     - linkables added/removed or default.configured.* metadata changed
     - schedule rescan-for-default-nodes

   * - default-nodes/store-configured-default-nodes
     - state-default-nodes.lua
     - default.configured.* metadata changed
     - stores user selections in the state file

   * - default-nodes/metadata-added
     - state-default-nodes.lua
     - metadata object created
     - restores default.configured.* values from the state file

.. list-table:: Hooks for the rescan-for-default-nodes event, in order of execution
   :header-rows: 1
   :width: 100%
   :widths: 25 25 50

   * - Hook name
     - File
     - Description

   * - m-standard-event-source/rescan-done
     - module-standard-event-source.c
     - clears the rescan_scheduled flag

   * - default-nodes/rescan
     - rescan.lua
     - schedules select-default-node for each category

.. list-table:: Hooks for the select-default-node event, in order of execution
   :header-rows: 1
   :width: 100%
   :widths: 25 25 50

   * - Hook name
     - File
     - Description

   * - default-nodes/find-best-default-node
     - find-best-default-node.lua
     - prioritizes nodes based on their priority.session property

   * - default-nodes/find-selected-default-node
     - find-selected-default-node.lua
     - prioritizes the current default.configured.* node, i.e. the current user selection

   * - default-nodes/find-stored-default-node
     - state-default-nodes.lua
     - prioritizes past user selections from the state file

   * - default-nodes/apply-default-node
     - apply-default-node.lua
     - sets the highest priority selected node as the default in the metadata

.. note::

   The actual order of the "default-nodes/find-\*" hooks is not defined and doesn't matter.
   The only thing that matters is that "default-nodes/apply-default-node" is the last hook.

select-default-node event
-------------------------

High priority event to select the default node for a given category
(media.type & direction combination).

In this event, each hook is tasked to find the highest priority node of the
category it runs for. In order to do that, a list of available nodes is
calculated in advance, by the "default-nodes/rescan" hook, and passed on to
each of the "select-default-node" hooks via event data. Each hook then has
to go through this list and select a node, placing it in the "selected-node"
event data together with its priority number in "selected-node-priority".
The next hook, then, may override the "selected-node" and "selected-node-priority"
with something else, but only if the new priority is higher than the old one.

.. list-table:: Event properties
   :header-rows: 1

   * - Property name
     - Description

   * - default-node.type
     - the suffix of the metadata keys related to this default node (audio.sink, audio.source or video.source)

.. list-table:: Exchanged event data
   :header-rows: 1

   * - Name
     - Description

   * - available-nodes
     - JSON array of all selectable nodes, with each element containing all node properties

   * - selected-node
     - the selected node's node.name (string)

   * - selected-node-priority
     - the priority (integer)
