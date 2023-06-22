-- WirePlumber
--
-- Copyright © 2021-2022 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- Based on default-routes.c from pipewire-media-session
-- Copyright © 2020 Wim Taymans
--
-- SPDX-License-Identifier: MIT
--
-- Update the device info cache with the latest information from EnumRoute(all
-- the device routes) and trigger a "select-routes" event to select new routes
-- for the given device configuration, if it has changed

cutils = require ("common-utils")
config = require ("device-config")
devinfo = require ("device-info-cache")
log = Log.open_topic ("s-device")

SimpleEventHook {
  name = "device/select-route",
  after = "device/select-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-added" },
    },
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "c", "EnumRoute" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local device = event:get_subject ()

    local dev_info = devinfo:get_device_info (device)
    if not dev_info then
      return
    end

    local new_route_infos = {}
    local avail_routes_changed = false
    local profile = nil

    -- get current profile
    for p in device:iterate_params ("Profile") do
      profile = cutils.parseParam (p, "Profile")
    end

    -- look at all the routes and update/reset cached information
    for p in device:iterate_params ("EnumRoute") do
      -- parse pod
      local route = cutils.parseParam (p, "EnumRoute")
      if not route then
        goto skip_enum_route
      end

      -- find cached route information
      local route_info = devinfo.find_route_info (dev_info, route, true)

      -- update properties
      route_info.prev_available = route_info.available
      if route_info.available ~= route.available then
        log:info (device, "route " .. route.name .. " available changed " ..
                         route_info.available .. " -> " .. route.available)
        route_info.available = route.available
        if profile and cutils.arrayContains (route.profiles, profile.index) then
          avail_routes_changed = true
        end
      end
      route_info.prev_active = route_info.active
      route_info.active = false
      route_info.save = false

      -- store
      new_route_infos [route.index] = route_info

      ::skip_enum_route::
    end

    -- replace old route_infos to lose old routes
    -- that no longer exist on the device
    dev_info.route_infos = new_route_infos
    new_route_infos = nil

    -- restore routes for profile
    if profile then
      local profile_changed = (dev_info.active_profile ~= profile.index)
      dev_info.active_profile = profile.index

      -- if the profile changed, restore routes for that profile
      -- if any of the routes of the current profile changed in availability,
      -- then try to select a new "best" route for each device and ignore
      -- what was stored
      if profile_changed or avail_routes_changed then
        log:info (device,
            string.format ("restore routes for profile(%s) of device(%s)",
            profile.name, dev_info.name))

        -- find the active device IDs for which to select routes
        local active_ids = findActiveDeviceIDs (profile)
        active_ids = Json.Array (active_ids):to_string ()

        -- push select-routes event and let the hooks select the appropriate routes
        local props = {
          ["profile.changed"] = profile_changed,
          ["profile.name"] = profile.name,
          ["profile.active-device-ids"] = active_ids,
        }
        source:call ("push-event", "select-routes", device, props)
      end
    end
  end
}:register()

-- These device ids are like routes(speaker, mic, headset etc) or sub-devices or
-- paths with in the pipewire devices/soundcards.
function findActiveDeviceIDs (profile)
  -- parses the classes from the profile and returns the device IDs
  ----- sample structure, should return { 0, 8 } -----
  -- classes:
  --  1: 2
  --  2:
  --    1: Audio/Source
  --    2: 1
  --    3: card.profile.devices
  --    4:
  --      1: 0
  --      pod_type: Array
  --      value_type: Spa:Int
  --    pod_type: Struct
  --  3:
  --    1: Audio/Sink
  --    2: 1
  --    3: card.profile.devices
  --    4:
  --      1: 8
  --      pod_type: Array
  --      value_type: Spa:Int
  --    pod_type: Struct
  --  pod_type: Struct
  local active_ids = {}
  if type (profile.classes) == "table" and profile.classes.pod_type == "Struct" then
    for _, p in ipairs (profile.classes) do
      if type (p) == "table" and p.pod_type == "Struct" then
        local i = 1
        while true do
          local k, v = p [i], p [i+1]
          i = i + 2
          if not k or not v then
            break
          end
          if k == "card.profile.devices" and
              type (v) == "table" and v.pod_type == "Array" then
            for _, dev_id in ipairs (v) do
              table.insert (active_ids, dev_id)
            end
          end
        end
      end
    end
  end
  return active_ids
end
