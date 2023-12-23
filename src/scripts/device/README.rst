Device Profile/Route Management Scripts
=======================================

These scripts are tasked to select appropriate profiles and routes for each
device.

Hooks
-----

.. list-table:: Hooks triggered by changes in the graph
   :header-rows: 1
   :width: 100%
   :widths: 20 10 20 50

   * - Hook name
     - File
     - Triggered by
     - Action

   * - device/select-profile
     - select-profile.lua
     - device added or EnumProfiles changed
     - schedules a 'select-profile' event

   * - device/select-route
     - select-routes.lua
     - device added or EnumRoute changed
     - updates the device info cache with the latest routes and schedules a 'select-routes' event, if needed

   * - device/store-user-selected-profile
     - select-profile.lua
     - device Profile param changed
     - stores profile into the state file if it was selected by the user (profile.save == true)

   * - device/store-or-restore-routes
     - select-routes.lua
     - device Route param changed
     - stores or restores Route selections based on the current state; may push a 'select-routes' event to update properties

.. list-table:: Hooks for the select-profile event, in order of execution
   :header-rows: 1
   :width: 100%
   :widths: 20 20 60

   * - Hook name
     - File
     - Description

   * - device/find-stored-profile
     - state-profile.lua
     - selects the profile that has been stored in the state file (user's explicit selection)

   * - device/find-best-profile
     - find-best-profile.lua
     - finds the best profile for a device based on profile priorities and availability

   * - device/apply-profile
     - apply-profile.lua
     - applies the selected profile to the device

.. list-table:: Hooks for the select-routes event, in order of execution
   :header-rows: 1
   :width: 100%
   :widths: 20 20 60

   * - Hook name
     - File
     - Description

   * - device/find-stored-routes
     - state-routes.lua
     - restores routes selection for a newly selected profile

   * - device/find-best-routes
     - find-best-routes.lua
     - finds the best routes based on availability and priority

   * - device/apply-route-props
     - state-routes.lua
     - augments the selected routes to include properties stored in the state file (volume, channel map, codecs, ...)"

   * - device/apply-routes
     - apply-routes.lua
     - applies the selected routes to the device

select-profile event
--------------------

High priority event to select a profile for a given device. The event hooks
must also apply the profile.

The event "subject" is the device (`WpDevice`) object.

This event has no special properties.

.. list-table:: Exchanged event data
   :header-rows: 1

   * - Name
     - Description

   * - selected-profile
     - The selected profile to be set:
        - Type: string, containing a JSON object
        - The JSON object should contain the properties of the Profile param

select-routes event
-------------------

High priority event to select routes for a given profile. The event hooks
must also apply the routes.

The event "subject" is the device (`WpDevice`) object.

.. list-table:: Event Properties
   :header-rows: 1

   * - Property name
     - Description

   * - profile.changed
     - true if a new profile has been selected / false if only the available routes changed

   * - profile.name
     - the active profile's name

   * - profile.active-device-ids
     - json array of integers containing the active device IDs for which to select routes


.. list-table:: Exchanged event data
   :header-rows: 1

   * - Name
     - Description

   * - selected-routes
     - The selected routes to be set:
        - Type: map<string, string>
        - The keys are device IDs (as represented in EnumRoute)
        - The values are JSON objects like this: { index: <a route index>, props: { <object with route props> } }
