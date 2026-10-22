Default Streams Scripts
=======================

These scripts contain logic to select default source and sink streams and also
manage user preferences regarding those.

Hooks
-----

Management of the default source and sink streams is implemented by scanning
all the available streams in the graph and assigning them a priority based on
certain logic. The stream with the highest priority in each category becomes
the default, which is stored in the default metadata as the name of its node
and its port group.

Scanning is implemented using a "rescan-for-default-streams" event.
The "default-streams/rescan-trigger" hook is the one that monitors graph changes
and schedules "rescan-for-default-streams". Then, the "default-streams/rescan"
hook is executed for the "rescan-for-default-streams" event and it pushes a
"select-default-stream" event for each one of the categories where a default
stream is required:

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

   * - default-streams/rescan-trigger
     - rescan.lua
     - streams added/removed or default.configured.* metadata changed
     - schedule rescan-for-default-streams

   * - default-streams/store-configured-default-streams
     - state-default-streams.lua
     - default.configured.* metadata changed
     - stores user selections in the state file

   * - default-streams/metadata-added
     - state-default-streams.lua
     - metadata object created
     - restores default.configured.* values from the state file

.. list-table:: Hooks for the rescan-for-default-streams event, in order of execution
   :header-rows: 1
   :width: 100%
   :widths: 25 25 50

   * - Hook name
     - File
     - Description

   * - m-standard-event-source/rescan-done
     - module-standard-event-source.c
     - clears the rescan_scheduled flag

   * - default-streams/rescan
     - rescan.lua
     - schedules select-default-stream for each category

.. list-table:: Hooks for the select-default-stream event, in order of execution
   :header-rows: 1
   :width: 100%
   :widths: 25 25 50

   * - Hook name
     - File
     - Description

   * - default-streams/find-configured-default-stream
     - find-configured-default-stream.lua
     - prioritizes the current default.configured.* stream, i.e. the current user selection

   * - default-streams/find-stored-default-stream
     - state-default-streams.lua
     - prioritizes past user selections from the state file

   * - default-streams/find-best-default-stream
     - find-best-default-stream.lua
     - prioritizes streams based on their priority.session property & their associated Route priority

   * - default-streams/apply-default-stream
     - apply-default-stream.lua
     - sets the highest priority selected stream as the default in the metadata

select-default-stream event
---------------------------

High priority event to select the default stream for a given category
(media.type & direction combination).

In this event, each hook is tasked to find the highest priority stream of the
category it runs for. In order to do that, a list of available streams is
calculated in advance, by the "default-streams/rescan" hook, and passed on to
each of the "select-default-stream" hooks via event data. Each hook then has
to go through this list and select a stream, placing it in the
"selected-stream" event data together with its priority number in
"selected-stream-priority". The next hook, then, may override the
"selected-stream" and "selected-stream-priority" with something else, but only
if the new priority is higher than the old one.

.. list-table:: Event properties
   :header-rows: 1

   * - Property name
     - Description

   * - default-stream.type
     - the suffix of the metadata keys related to this default stream (audio.sink, audio.source or video.source)

.. list-table:: Exchanged event data
   :header-rows: 1

   * - Name
     - Description

   * - available-streams
     - JSON array with the ids of all the selectable stream objects

   * - selected-stream
     - the selected stream object

   * - selected-stream-priority
     - the priority (integer)
