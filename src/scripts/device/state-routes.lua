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
-- This file contains all the logic related to saving device routes and their
-- properties to a state file and restoring both the routes selection and
-- the properties of routes later on.
--

cutils = require ("common-utils")
config = require ("device-config")
devinfo = require ("device-info-cache")

-- the state storage
state = nil
state_table = nil

-- hook to restore routes selection for a newly selected profile
find_stored_routes_hook = SimpleEventHook {
  name = "find-stored-routes@device",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-routes" },
      Constraint { "profile.changed", "=", "true" },
      Constraint { "profile.active-device-ids", "is-present" },
    },
  },
  execute = function (event)
    local device = event:get_subject ()
    local event_properties = event:get_properties ()
    local profile_name = event_properties ["profile.name"]
    local active_ids = event_properties ["profile.active-device-ids"]
    local selected_routes = event:get_data ("selected-routes") or {}

    local dev_info = devinfo:get_device_info (device)
    assert (dev_info)

    -- get the stored routes for this profile
    -- skip the hook if there are no stored routes, there is no point
    local spr = getStoredProfileRoutes (dev_info, profile_name)
    if #spr == 0 then
      return
    end

    -- active IDs are exchanged in JSON format
    active_ids = Json.Raw (active_ids):parse ()

    for _, device_id in ipairs (active_ids) do
      -- if a previous hook already selected a route for this device_id, skip it
      if selected_routes [tostring (device_id)] then
        goto next_device_id
      end

      Log.info (device, "restoring route for device ID " .. tostring (device_id));

      local route_info = nil

      -- find a route that was previously stored for a device_id
      for _, ri in pairs (dev_info.route_infos) do
        if cutils.arrayContains (ri.devices, tonumber (device_id)) and
            (ri.profiles == nil or cutils.arrayContains (ri.profiles, dev_info.active_profile)) and
            cutils.arrayContains (spr, ri.name) then
          route = ri
          break
        end
      end

      if route_info then
        -- we found a stored route
        if route_info.available == "no" then
          Log.info (device, "stored route '" .. route_info.name .. "' not available")
          -- not available, try to find next best
          route_info = nil
        else
          Log.info (device, "found stored route: " .. route_info.name)
          -- make sure we save it again
          route_info.save = true
        end
      end

      if route_info then
        selected_routes [tostring (device_id)] =
            Json.Object { index = route_info.index }:to_string ()
      end

      ::next_device_id::
    end

    -- save the selected routes for the apply-routes hook
    event:set_data ("selected-routes", selected_routes)
  end
}

-- extract the "selected-routes" event data and augment it to include
-- the route properties, as they were stored in the state file;
-- this is the last step before applying the routes
apply_route_props_hook = SimpleEventHook {
  name = "apply-route-props@device",
  after = { "find-stored-routes@device", "find-best-routes@device" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-routes" },
    },
  },
  execute = function (event)
    local device = event:get_subject ()
    local selected_routes = event:get_data ("selected-routes") or {}
    local new_selected_routes = {}

    local dev_info = devinfo:get_device_info (device)
    assert (dev_info)

    if not selected_routes then
      Log.info (device, "No routes selected to set on " .. dev_info.name)
      return
    end

    for device_id, route in pairs (selected_routes) do
       -- JSON to lua table
      route = Json.Raw (route):parse ()

      local route_info = devinfo.find_route_info (dev_info, route, false)
      local props = getStoredRouteProps (dev_info, route_info)

      -- convert arrays to Json
      if props.channelVolumes then
        props.channelVolumes = Json.Array (props.channelVolumes)
      end
      if props.channelMap then
        props.channelMap = Json.Array (props.channelMap)
      end
      if props.iec958Codecs then
        props.iec958Codecs = Json.Array (props.iec958Codecs)
      end

      local json = Json.Object {
        index = route_info.index,
        props = Json.Object (props),
      }
      new_selected_routes [device_id] = json:to_string ()
    end

    -- save the selected routes for the apply-routes hook
    event:set_data ("selected-routes", new_selected_routes)
  end
}

store_or_restore_routes_hook = SimpleEventHook {
  name = "store-or-restore-routes@device",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "Route" },
    },
  },
  execute = function (event)
    local device = event:get_subject ()
    local selected_routes = {}
    local push_select_routes = false

    local dev_info = devinfo:get_device_info (device)
    if not dev_info then
      return
    end

    -- check for changes in the active routes
    for p in device:iterate_params ("Route") do
      local route = cutils.parseParam (p, "Route")
      if not route then
        goto skip_route
      end

      -- get cached route info and at the same time
      -- ensure that the route is also in EnumRoute
      local route_info = devinfo.find_route_info (dev_info, route, false)
      if not route_info then
        goto skip_route
      end

      -- update state
      route_info.active = true
      route_info.save = route.save

      if not route_info.prev_active then
        -- a new route is now active, restore the volume and
        -- make sure we save this as a preferred route
        Log.info (device,
            string.format ("new active route(%s) found of device(%s)",
                route.name, dev_info.name))

        selected_routes [tostring (device_id)] =
            Json.Object { index = route_info.index }:to_string ()
        push_select_routes = true

      elseif route.save and route.props then
        -- just save route properties
        Log.info (device,
            string.format ("storing route(%s) props of device(%s)",
              route.name, dev_info.name))

        saveRouteProps (dev_info, route)
      end

      ::skip_route::
    end

    -- save selected routes for the active profile
    for p in device:iterate_params ("Profile") do
      local profile = cutils.parseParam (p, "Profile")
      saveProfileRoutes (dev_info, profile.name)
    end

    -- push a select-routes event to re-apply the routes with new properties
    if push_select_routes then
      local e = source:call ("create-event", "select-routes", device, nil)
      e:set_data ("selected-routes", selected_routes)
      EventDispatcher.push_event (e)
    end
  end
}

function saveRouteProps (dev_info, route)
  local props = route.props.properties
  local key_base = dev_info.name .. ":" ..
                   route.direction:lower () .. ":" ..
                   route.name .. ":"

  state_table [key_base .. "volume"] =
    props.volume and tostring (props.volume) or nil
  state_table [key_base .. "mute"] =
    props.mute and tostring (props.mute) or nil
  state_table [key_base .. "channelVolumes"] =
    props.channelVolumes and cutils.serializeArray (props.channelVolumes) or nil
  state_table [key_base .. "channelMap"] =
    props.channelMap and cutils.serializeArray (props.channelMap) or nil
  state_table [key_base .. "latencyOffsetNsec"] =
    props.latencyOffsetNsec and tostring (props.latencyOffsetNsec) or nil
  state_table [key_base .. "iec958Codecs"] =
    props.iec958Codecs and cutils.serializeArray (props.iec958Codecs) or nil

  cutils.storeAfterTimeout (state, state_table)
end

function getStoredRouteProps (dev_info, route)
  local props = {}
  local key_base = dev_info.name .. ":" ..
                   route.direction:lower () .. ":" ..
                   route.name .. ":"

  local str = state_table [key_base .. "volume"]
  props.volume = str and tonumber (str) or nil

  local str = state_table [key_base .. "mute"]
  props.mute = str and (str == "true") or nil

  local str = state_table [key_base .. "channelVolumes"]
  props.channelVolumes =
      str and cutils.parseArray (str, tonumber) or nil

  local str = state_table [key_base .. "channelMap"]
  props.channelMap = str and cutils.parseArray (str) or nil

  local str = state_table [key_base .. "latencyOffsetNsec"]
  props.latencyOffsetNsec = str and math.tointeger (str) or nil

  local str = state_table [key_base .. "iec958Codecs"]
  props.iec958Codecs = str and cutils.parseArray (str) or nil

  return props
end

-- stores an array with the route names that are selected
-- for the given device and profile
function saveProfileRoutes (dev_info, profile_name)
  -- select only routes with save == true
  local routes = {}
  for idx, ri in pairs (dev_info.route_infos) do
    if ri.save then
      table.insert (routes, ri.name)
    end
  end

  if #routes > 0 then
    local key = dev_info.name .. ":profile:" .. profile_name
    state_table [key] = cutils.serializeArray (routes)
    cutils.storeAfterTimeout (state, state_table)
  end
end

-- returns an array of the route names that were previously selected
-- for the given device and profile
function getStoredProfileRoutes (dev_info, profile_name)
  local key = dev_info.name .. ":profile:" .. profile_name
  local str = state_table [key]
  return str and cutils.parseArray (str) or {}
end

function handlePersistentSetting (enable)
  if enable and not state then
    state = State ("default-routes")
    state_table = state:load ()
    find_stored_routes_hook:register ()
    apply_route_props_hook:register ()
    store_or_restore_routes_hook:register ()
  elseif not enable and state then
    state = nil
    state_table = nil
    find_stored_routes_hook:remove ()
    apply_route_props_hook:remove ()
    store_or_restore_routes_hook:remove ()
  end
end

config:subscribe ("use-persistent-storage", handlePersistentSetting)
handlePersistentSetting (config.use_persistent_storage)
