-- WirePlumber
--
-- Copyright © 2021 Asymptotic Inc.
--    @author Sanchayan Maity <sanchayan@asymptotic.io>
--
-- Based on bt-profile-switch.lua in tests/examples
-- Copyright © 2021 George Kiagiadakis
--
-- Based on bluez-autoswitch in media-session
-- Copyright © 2021 Pauli Virtanen
--
-- SPDX-License-Identifier: MIT
--
-- This script is charged to automatically change BT profiles on a device. If a
-- client is linked to the device's loopback source node, the associated BT
-- device profile is automatically switched to HSP/HFP. If there is no clients
-- linked to the device's loopback source node, the BT device profile is
-- switched back to A2DP profile.
--
-- We switch to the highest priority profile that has an Input route available.
-- The reason for this is that we may have microphone enabled with non-HFP
-- codecs eg. Faststream.
-- When a stream goes away if the list with which we track the streams above
-- is empty, then we revert back to the old profile.

-- settings file: bluetooth.conf

lutils = require ("linking-utils")
cutils = require ("common-utils")

state = nil
headset_profiles = nil
device_loopback_sources = {}

local profile_restore_timeout_msec = 2000

local INVALID = -1
local timeout_source = {}
local restore_timeout_source = {}

local last_profiles = {}

local active_streams = {}
local previous_streams = {}

function handlePersistentSetting (enable)
  if enable and state == nil then
    -- the state storage
    state = Settings.get_boolean ("bluetooth.autoswitch-to-headset-profile")
        and State ("bluetooth-autoswitch") or nil
    headset_profiles = state and state:load () or {}
  else
    state = nil
    headset_profiles = nil
  end
end

handlePersistentSetting (Settings.get_boolean ("bluetooth.use-persistent-storage"))
Settings.subscribe ("bluetooth.use-persistent-storage", function ()
  handlePersistentSetting (Settings.get_boolean ("bluetooth.use-persistent-storage"))
end)

devices_om = ObjectManager {
  Interest {
    type = "device",
    Constraint { "device.api", "=", "bluez5" },
  }
}

streams_om = ObjectManager {
  Interest {
    type = "node",
    Constraint { "media.class", "matches", "Stream/Input/Audio", type = "pw-global" },
    Constraint { "stream.monitor", "!", "true", type = "pw" },
    Constraint { "bluez5.loopback", "!", "true", type = "pw" }
  }
}

loopback_nodes_om = ObjectManager {
  Interest {
    type = "node",
    Constraint { "media.class", "matches", "Audio/Source", type = "pw-global" },
    Constraint { "bluez5.loopback", "=", "true", type = "pw" },
  }
}

local function saveHeadsetProfile (device, profile_name)
  local key = "saved-headset-profile:" .. device.properties ["device.name"]
  headset_profiles [key] = profile_name
  state:save_after_timeout (headset_profiles)
end

local function getSavedHeadsetProfile (device)
  local key = "saved-headset-profile:" .. device.properties ["device.name"]
  return headset_profiles [key]
end

local function saveLastProfile (device, profile_name)
  last_profiles [device.properties ["device.name"]] = profile_name
end

local function getSavedLastProfile (device)
  return last_profiles [device.properties ["device.name"]]
end

local function isSwitchedToHeadsetProfile (device)
  return getSavedLastProfile (device) ~= nil
end

local function findProfile (device, index, name)
  for p in device:iterate_params ("EnumProfile") do
    local profile = cutils.parseParam (p, "EnumProfile")
    if not profile then
      goto skip_enum_profile
    end

    Log.debug ("Profile name: " .. profile.name .. ", priority: "
              .. tostring (profile.priority) .. ", index: " .. tostring (profile.index))
    if (index ~= nil and profile.index == index) or
       (name ~= nil and profile.name == name) then
      return profile.priority, profile.index, profile.name
    end

    ::skip_enum_profile::
  end

  return INVALID, INVALID, nil
end

local function getCurrentProfile (device)
  for p in device:iterate_params ("Profile") do
    local profile = cutils.parseParam (p, "Profile")
    if profile then
      return profile.name
    end
  end

  return nil
end

local function highestPrioProfileWithInputRoute (device)
  local profile_priority = INVALID
  local profile_index = INVALID
  local profile_name = nil

  for p in device:iterate_params ("EnumRoute") do
    local route = cutils.parseParam (p, "EnumRoute")
    -- Parse pod
    if not route then
      goto skip_enum_route
    end

    if route.direction ~= "Input" then
      goto skip_enum_route
    end

    Log.debug ("Route with index: " .. tostring (route.index) .. ", direction: "
          .. route.direction .. ", name: " .. route.name .. ", description: "
          .. route.description .. ", priority: " .. route.priority)
    if route.profiles then
      for _, v in pairs (route.profiles) do
        local priority, index, name = findProfile (device, v)
        if priority ~= INVALID then
          if profile_priority < priority then
            profile_priority = priority
            profile_index = index
            profile_name = name
          end
        end
      end
    end

    ::skip_enum_route::
  end

  return profile_priority, profile_index, profile_name
end

local function hasProfileInputRoute (device, profile_index)
  for p in device:iterate_params ("EnumRoute") do
    local route = cutils.parseParam (p, "EnumRoute")
    if route and route.direction == "Input" and route.profiles then
      for _, v in pairs (route.profiles) do
        if v == profile_index then
          return true
        end
      end
    end
  end
  return false
end

local function switchDeviceToHeadsetProfile (dev_id)
  -- Find the actual device
  local device = devices_om:lookup {
      Constraint { "bound-id", "=", dev_id, type = "gobject" }
  }
  if device == nil then
    Log.info ("Device with id " .. tostring(dev_id).. " not found")
    return
  end

  if isSwitchedToHeadsetProfile (device) then
    Log.info ("Device with id " .. tostring(dev_id).. " is already switched to HSP/HFP")
    return
  end

  local cur_profile_name = getCurrentProfile (device)
  local priority, index, name = findProfile (device, nil, cur_profile_name)
  if hasProfileInputRoute (device, index) then
    Log.info ("Current profile has input route, not switching")
    return
  end

  -- clear restore callback, if any
  if restore_timeout_source[dev_id] ~= nil then
    restore_timeout_source[dev_id]:destroy ()
    restore_timeout_source[dev_id] = nil
  end

  local saved_headset_profile = getSavedHeadsetProfile (device)

  index = INVALID
  if saved_headset_profile then
    priority, index, name = findProfile (device, nil, saved_headset_profile)
    if index ~= INVALID and not hasProfileInputRoute (device, index) then
      index = INVALID
      saveHeadsetProfile (device, nil)
    end
  end
  if index == INVALID then
    priority, index, name = highestPrioProfileWithInputRoute (device)
  end

  if index ~= INVALID then
    local pod = Pod.Object {
      "Spa:Pod:Object:Param:Profile", "Profile",
      index = index
    }

    -- store the current profile (needed when restoring)
    saveLastProfile (device, cur_profile_name)

    -- switch to headset profile
    Log.info ("Setting profile of '"
          .. device.properties ["device.description"]
          .. "' from: " .. cur_profile_name
          .. " to: " .. name)
    device:set_params ("Profile", pod)
  else
    Log.warning ("Got invalid index when switching profile")
  end
end

local function restoreProfile (dev_id)
  -- Find the actual device
  local device = devices_om:lookup {
      Constraint { "bound-id", "=", dev_id, type = "gobject" }
  }
  if device == nil then
    Log.info ("Device with id " .. tostring(dev_id).. " not found")
    return
  end

  if not isSwitchedToHeadsetProfile (device) then
    Log.info ("Device with id " .. tostring(dev_id).. " is already not switched to HSP/HFP")
    return
  end

  local profile_name = getSavedLastProfile (device)
  local cur_profile_name = getCurrentProfile (device)
  local priority, index, name

  if cur_profile_name then
    priority, index, name = findProfile (device, nil, cur_profile_name)

    if index ~= INVALID and hasProfileInputRoute (device, index) then
      Log.info ("Setting saved headset profile to: " .. cur_profile_name)
      saveHeadsetProfile (device, cur_profile_name)
    end
  end

  if profile_name then
    priority, index, name = findProfile (device, nil, profile_name)

    if index ~= INVALID then
      local pod = Pod.Object {
        "Spa:Pod:Object:Param:Profile", "Profile",
        index = index
      }

      -- clear last profile as we will restore it now
      saveLastProfile (device, nil)

      -- restore previous profile
      Log.info ("Restoring profile of '"
            .. device.properties ["device.description"]
            .. "' from: " .. cur_profile_name
            .. " to: " .. name)
      device:set_params ("Profile", pod)
    else
      Log.warning ("Failed to restore profile")
    end
  end
end

local function triggerRestoreProfile (dev_id)
  -- we never restore the device profiles if there are active streams
  for _, v in pairs (active_streams) do
    if v == dev_id then
      return
    end
  end

  restore_timeout_source[dev_id] = nil
  restore_timeout_source[dev_id] = Core.timeout_add (profile_restore_timeout_msec, function ()
    restore_timeout_source[dev_id] = nil
    restoreProfile (dev_id)
  end)
end

-- We consider a Stream of interest if it is linked to a bluetooth loopback
-- source filter
local function checkStreamStatus (stream)
  -- check if the stream is linked to a bluetooth loopback source
  local stream_id = tonumber(stream["bound-id"])
  local peer_id = lutils.getNodePeerId (stream_id)
  if peer_id ~= nil then
    local bt_node = loopback_nodes_om:lookup {
        Constraint { "bound-id", "=", peer_id, type = "gobject" }
    }
    if bt_node ~= nil then
      local dev_id = bt_node.properties["device.id"]
      if dev_id ~= nil then
        -- If a stream we previously saw stops running, we consider it
        -- inactive, because some applications (Teams) just cork input
        -- streams, but don't close them.
        if previous_streams [stream.id] == dev_id and
            stream.state ~= "running" then
          return nil
        end

        return dev_id
      end
    end
  end

  return nil
end

local function handleStream (stream)
  if not Settings.get_boolean ("bluetooth.autoswitch-to-headset-profile") then
    return
  end

  local dev_id = checkStreamStatus (stream)
  if dev_id ~= nil then
    active_streams [stream.id] = dev_id
    previous_streams [stream.id] = dev_id
    switchDeviceToHeadsetProfile (dev_id)
  else
    dev_id = active_streams [stream.id]
    active_streams [stream.id] = nil
    if dev_id ~= nil then
      triggerRestoreProfile (dev_id)
    end
  end
end

local function handleAllStreams ()
  for stream in streams_om:iterate() do
    handleStream (stream)
  end
end

SimpleEventHook {
  name = "node-removed@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-removed" },
      Constraint { "media.class", "matches", "Stream/Input/Audio", type = "pw-global" },
      Constraint { "bluez5.loopback", "!", "true", type = "pw" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local dev_id = active_streams[stream.id]
    active_streams[stream.id] = nil
    previous_streams[stream.id] = nil
    if dev_id ~= nil then
      triggerRestoreProfile (dev_id)
    end
  end
}:register ()

SimpleEventHook {
  name = "link-added@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "link-added" },
    },
  },
  execute = function (event)
    local link = event:get_subject ()
    local p = link.properties
    for stream in streams_om:iterate () do
      local in_id = tonumber(p["link.input.node"])
      local out_id = tonumber(p["link.output.node"])
      local stream_id = tonumber(stream["bound-id"])
      local bt_node = loopback_nodes_om:lookup {
          Constraint { "bound-id", "=", out_id, type = "gobject" }
      }
      if in_id == stream_id and bt_node ~= nil then
        handleStream (stream)
      end
    end
  end
}:register ()

SimpleEventHook {
  name = "bluez-device-added@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-added" },
      Constraint { "device.api", "=", "bluez5" },
    },
  },
  execute = function (event)
    local device = event:get_subject ()
    -- Devices are unswitched initially
    saveLastProfile (device, nil)
    handleAllStreams ()
  end
}:register ()

devices_om:activate ()
streams_om:activate ()
loopback_nodes_om:activate()

