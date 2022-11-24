-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- Based on default-routes.c from pipewire-media-session
-- Copyright © 2020 Wim Taymans
--
-- SPDX-License-Identifier: MIT

-- Script monitors changes in devices(new device addition, removal etc) and
-- device profiles. It selects and enables the routes(Route here is a path on
-- soundcard/Pipewire Device(PipeWire:Interface:Device), for example: speaker,
-- mic, headset with in a soundcard) needed for a given profile. It also caches
-- the route specific properties(Volume, Mute, channelVolumes, channelMap etc)
-- and restores them when the route appears afresh. The cached properties are
-- remembered across reboots if persistancy(use_persistent_storage) is enabled.

-- settings file: device.conf

local cutils = require ("common-utils")

local defaults = {}
defaults.use_persistent_storage = true
defaults.default_volume = 0.4 ^ 3
defaults.default_input_volume = 1.0

local config = {}
config.use_persistent_storage = Settings.parse_boolean_safe (
    "device.use-persistent-storage", defaults.use_persistent_storage)
config.default_volume = Settings.parse_float_safe (
    "device.default-volume", defaults.default_volume)
config.default_input_volume = Settings.parse_float_safe (
    "device.default-input-volume", defaults.default_input_volume)

-- table of device info
dev_infos = {}

state = nil
state_table = nil

function handlePersistantSetting (enable)
  if enable and state == nil then
    -- the state storage
    state = config.use_persistent_storage and State ("default-routes") or nil
    state_table = state and state:load () or {}
  else
    state = nil
    state_table = nil
  end
end

local function settingsChangedCallback (_, setting, _)
  local value = Settings.get (setting):parse ()

  if setting == "device.use-persistent-storage" then
    config.use_persistent_storage = Settings.parse_boolean_safe
        ("device.use-persistent-storage", config.use_persistent_storage)
    handlePersistantSetting (config.use_persistent_storage)
  elseif setting == "device.default-volume" then
    config.default_volume = Settings.parse_float_safe ("device.default-volume",
        config.default_volume)
  elseif setting == "device.default-input-volume" then
    config.default_input_volume = Settings.parse_float_safe
        ("device.default-input-volume", config.default_input_volume)
  end
end

Settings.subscribe ("device*", settingsChangedCallback)

handlePersistantSetting (config.use_persistent_storage)

function saveProfile (dev_info, profile_name)
  if not config.use_persistent_storage then
    return
  end

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

function saveRouteProps (dev_info, route)
  if not config.use_persistent_storage or not route.props then
    return
  end

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

function restoreRoute (device, dev_info, device_id, route)
  -- default props
  local props = {
    "Spa:Pod:Object:Param:Props", "Route",
    mute = false,
  }

  if route.direction == "Input" then
    props.channelVolumes = { config.default_input_volume }
  else
    props.channelVolumes = { config.default_volume }
  end

  -- restore props from persistent storage
  if config.use_persistent_storage then
    local key_base = dev_info.name .. ":" ..
                     route.direction:lower () .. ":" ..
                     route.name .. ":"

    local str = state_table [key_base .. "volume"]
    props.volume = str and tonumber (str) or props.volume

    local str = state_table [key_base .. "mute"]
    props.mute = str and (str == "true") or false

    local str = state_table [key_base .. "channelVolumes"]
    props.channelVolumes =
        str and cutils.parseArray (str, tonumber) or props.channelVolumes

    local str = state_table [key_base .. "channelMap"]
    props.channelMap = str and cutils.parseArray (str) or props.channelMap

    local str = state_table [key_base .. "latencyOffsetNsec"]
    props.latencyOffsetNsec = str and math.tointeger (str) or props.latencyOffsetNsec

    local str = state_table [key_base .. "iec958Codecs"]
    props.iec958Codecs = str and cutils.parseArray (str) or props.iec958Codecs
  end

  -- convert arrays to Spa Pod
  if props.channelVolumes then
    table.insert (props.channelVolumes, 1, "Spa:Float")
    props.channelVolumes = Pod.Array (props.channelVolumes)
  end
  if props.channelMap then
    table.insert (props.channelMap, 1, "Spa:Enum:AudioChannel")
    props.channelMap = Pod.Array (props.channelMap)
  end
  if props.iec958Codecs then
    table.insert (props.iec958Codecs, 1, "Spa:Enum:AudioIEC958Codec")
    props.iec958Codecs = Pod.Array (props.iec958Codecs)
  end

  -- construct Route param
  local param = Pod.Object {
    "Spa:Pod:Object:Param:Route", "Route",
    index = route.index,
    device = device_id,
    props = Pod.Object (props),
    save = route.save,
  }

  Log.debug (param,
    string.format ("setting route(%s) on for device(%s)(%s)",
      route.name, dev_info.nick, tostring (device)))

  device:set_param ("Route", param)

  route.prev_active = true
  route.active = true
end

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

-- returns an array of the route names that were previously selected
-- for the given device and profile
function getStoredProfileRoutes (dev_name, profile_name)
  local key = dev_name .. ":profile:" .. profile_name
  local str = state_table [key]
  return str and cutils.parseArray (str) or {}
end

-- find a route that was previously stored for a device_id
-- spr needs to be the array returned from getStoredProfileRoutes()
function findSavedRoute(dev_info, device_id, spr)
  for idx, ri in pairs(dev_info.route_infos) do
    if cutils.arrayContains (ri.devices, device_id) and
        (ri.profiles == nil or cutils.arrayContains (ri.profiles, dev_info.active_profile)) and
        cutils.arrayContains (spr, ri.name) then
      return ri
    end
  end
  return nil
end

-- find the best route for a given device_id, based on availability and priority
function findBestRoute (dev_info, device_id)
  local best_avail = nil
  local best_unk = nil
  for idx, ri in pairs(dev_info.route_infos) do
    if cutils.arrayContains (ri.devices, device_id) and
          (ri.profiles == nil or cutils.arrayContains (ri.profiles, dev_info.active_profile)) then
      if ri.available == "yes" or ri.available == "unknown" then
        if ri.direction == "Output" and ri.available ~= ri.prev_available then
          best_avail = ri
          ri.save = true
          break
        elseif ri.available == "yes" then
          if (best_avail == nil or ri.priority > best_avail.priority) then
            best_avail = ri
          end
        elseif best_unk == nil or ri.priority > best_unk.priority then
            best_unk = ri
        end
      end
    end
  end
  return best_avail or best_unk
end

function restoreProfileRoutes (device, dev_info, profile, profile_changed)
  Log.info (device,
    string.format ("restore routes for profile(%s) of device(%s)",
        profile.name, dev_info.nick))

  local active_ids = findActiveDeviceIDs (profile)
  local found_active_route = false;
  local spr = getStoredProfileRoutes (dev_info.name, profile.name)

  for _, device_id in ipairs (active_ids) do
    Log.info (device, "restoring device " .. device_id);

    local route = nil

    -- restore routes selection for the newly selected profile
    -- don't bother if spr is empty, there is no point
    if profile_changed and #spr > 0 then
      route = findSavedRoute (dev_info, device_id, spr)
      if route then
        -- we found a saved route
        if route.available == "no" then
          Log.info (device, "saved route '" .. route.name .. "' not available")
          -- not available, try to find next best
          route = nil
        else
          Log.info (device, "found saved route: " .. route.name)
          -- make sure we save it again
          route.save = true
        end
      end
    end

    -- we could not find a saved route, try to find a new best
    if not route then
      route = findBestRoute (dev_info, device_id)
      if not route then
        Log.info (device, "can't find best route")
      else
        Log.info (device, "found best route: " .. route.name)
      end
    end

    -- restore route
    if route then
      if not found_active_route then
        found_active_route = true;
      end
      restoreRoute (device, dev_info, device_id, route)
    end
  end

  if not found_active_route then
    Log.info (device,
      string.format ("routes not available for profile(%s) of device (%s)",
          profile.name, dev_info.nick))
  end
end

function findRouteInfo (dev_info, route, return_new)
  local ri = dev_info.route_infos [route.index]
  if not ri and return_new then
    ri = {
      index = route.index,
      name = route.name,
      direction = route.direction,
      devices = route.devices or {},
      profiles = route.profiles,
      priority = route.priority or 0,
      available = route.available or "unknown",
      prev_available = route.available or "unknown",
      active = false,
      prev_active = false,
      save = false,
    }
  end
  return ri
end

function handleDevice (device)
  if not dev_infos [device ["bound-id"]] then
    return
  end

  local dev_info = dev_infos [device ["bound-id"]]
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
    local route_info = findRouteInfo (dev_info, route, true)

    -- update properties
    route_info.prev_available = route_info.available
    if route_info.available ~= route.available then
      Log.info (device, "route " .. route.name .. " available changed " ..
                       route_info.available .. " -> " .. route.available)
      route_info.available = route.available
      if profile and cutils.arrayContains  (route.profiles, profile.index) then
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

  -- check for changes in the active routes
  for p in device:iterate_params ("Route") do
    local route = cutils.parseParam (p, "Route")
    if not route then
      goto skip_route
    end

    -- get cached route info and at the same time
    -- ensure that the route is also in EnumRoute
    local route_info = findRouteInfo (dev_info, route, false)
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
              route.name, dev_info.nick))

      restoreRoute (device, dev_info, route.device, route_info)
    elseif route.save then
      -- just save route properties
      Log.info (device,
          string.format ("storing route(%s) props of device(%s)",
            route.name, dev_info.nick))

      saveRouteProps (dev_info, route)
    end

    ::skip_route::
  end

  -- restore routes for profile
  if profile then
    local profile_changed = (dev_info.active_profile ~= profile.index)

    -- if the profile changed, restore routes for that profile
    -- if any of the routes of the current profile changed in availability,
    -- then try to select a new "best" route for each device and ignore
    -- what was stored
    if profile_changed or avail_routes_changed then
      dev_info.active_profile = profile.index
      restoreProfileRoutes (device, dev_info, profile, profile_changed)
    end

    saveProfile (dev_info, profile.name)
  end
end

SimpleEventHook {
  name = "handle-device@device-routes",
  priority = HookPriority.ULTRA_LOW,
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "objects-changed" },
      Constraint { "event.subject.type", "=", "device" },
    },
  },
  execute = function (event)
    local new_dev_infos = {}
    local om = event:get_subject()
    for device in om:iterate () do
      -- skip devices with out name
      if not device.properties ["device.name"] then
        goto skip_device
      end

      local dev_info = dev_infos [device ["bound-id"]]
      -- new device appeared
      if not dev_info then
        dev_info = {
          name = device.properties ["device.name"],
          nick = device.properties ["device.nick"],
          active_profile = -1,
          route_infos = {},
        }
        dev_infos [device ["bound-id"]] = dev_info

        handleDevice (device)
      end

      new_dev_infos [device ["bound-id"]] = dev_info

      ::skip_device::
    end
    -- replace list to get rid of dev_info for devices that no longer exist
    dev_infos = new_dev_infos
  end
}:register()

SimpleEventHook {
  name = "handle-device@device-routes",
  priority = HookPriority.ULTRA_LOW,
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "Route" },
    },
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "EnumRoute" },
    },
  },
  execute = function (event)
    local props = event:get_properties ()
    handleDevice (event:get_subject ())
  end
}:register()
