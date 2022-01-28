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
-- Checks for the existence of media.role and if present switches the bluetooth
-- profile accordingly. Also see bluez-autoswitch in media-session.
-- The intended logic of the script is as follows.
--
-- When a stream comes in, if it has a Communication or phone role in PulseAudio
-- speak in props, we switch to the highest priority profile that has an Input
-- route available. The reason for this is that we may have microphone enabled
-- non-HFP codecs eg. Faststream.
-- We track the incoming streams with Communication role or the applications
-- specified which do not set the media.role correctly perhaps.
-- When a stream goes away if the list with which we track the streams above
-- is empty, then we revert back to the old profile.

local config = ...
local use_persistent_storage = config["use-persistent-storage"] or false
local applications = config["media-role.applications"] or {}
local use_headset_profile = config["media-role.use-headset-profile"] or false

local INVALID = -1
local app_node_ids = {}
local timeout_source = nil

local state = use_persistent_storage and State("policy-bluetooth") or nil
local state_table = state and state:load() or {}

metadata_om = ObjectManager {
  Interest {
    type = "metadata",
    Constraint { "metadata.name", "=", "default" },
  }
}

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
    -- Do not consider monitor streams
    Constraint { "stream.monitor", "!", "true" }
  }
}

local function hasValue(tab, val)
  for _, value in ipairs(tab) do
    if value == val then
      return true
    end
  end

  return false
end

local function removeValue(tab, val)
  for index, value in ipairs(tab) do
    if value == val then
      table.remove(tab, index)
    end
  end
end

local function parseParam(param_to_parse, id)
  local param = param_to_parse:parse()
  if param.pod_type == "Object" and param.object_id == id then
    return param.properties
  else
    return nil
  end
end

local function storeAfterTimeout()
  if not use_persistent_storage then
    return
  end

  if timeout_source then
    timeout_source:destroy()
  end
  timeout_source = Core.timeout_add(1000, function ()
    local saved, err = state:save(state_table)
    if not saved then
      Log.warning(err)
    end
    timeout_source = nil
  end)
end

local function saveHeadsetProfile(device, profile_index)
  local key = "saved-headset-profile:" .. device.properties["device.name"]
  state_table[key] = profile_index
  storeAfterTimeout()
end

local function getSavedHeadsetProfile(device)
  local key = "saved-headset-profile:" .. device.properties["device.name"]
  local profile_index = state_table[key]
  if profile_index then
    return tonumber(profile_index)
  else
    return INVALID
  end
end

local function saveProfile(device, profile_index, profile_switched)
  local profile_key = "saved-profile:" .. device.properties["device.name"]
  local switched_key = "switched-profile:" .. device.properties["device.name"]
  state_table[profile_key] = profile_index
  state_table[switched_key] = profile_switched
  storeAfterTimeout()
end

local function getSavedProfile(device)
  local key = "saved-profile:" .. device.properties["device.name"]
  local profile_index = state_table[key]
  return tonumber(profile_index)
end

local function isProfileSwitched(device)
  local switched_key = "switched-profile:" .. device.properties["device.name"]
  if state_table[switched_key] == nil then
    return false
  else
    return (state_table[switched_key] == true or
            state_table[switched_key] == "true")
  end
end

local function isBluez5AudioSink(sink_name)
  if string.find(sink_name, "bluez_output.") ~= nil then
    return true
  end
  return false
end

local function isBluez5DefaultAudioSink()
  local metadata = metadata_om:lookup()
  local default_audio_sink = metadata:find(0, "default.audio.sink")
  Log.info("Default audio sink: " .. default_audio_sink)
  return isBluez5AudioSink(default_audio_sink)
end

local function findProfile(device, index)
  Log.debug("Finding profile with index: " .. tostring(index))
  for p in device:iterate_params("EnumProfile") do
    local profile = parseParam(p, "EnumProfile")
    if not profile then
      goto skip_enum_profile
    end

    Log.debug("Profile name: " .. profile.name .. ", priority: "
      .. tostring(profile.priority) .. ", index: " .. tostring(profile.index)
      .. ", description: " .. profile.description)
    if tonumber(profile.index) == tonumber(index) then
      return profile.priority, profile.index, profile.description
    end

    ::skip_enum_profile::
  end

  return INVALID, INVALID, nil
end

local function getCurrentProfile(device)
  for p in device:iterate_params("Profile") do
    local profile = parseParam(p, "Profile")
    if profile then
      return profile.name, profile.index, profile.description
    end
  end

  return nil, INVALID, nil
end

local function highestPrioProfileWithInputRoute(device)
  local profile_priority = INVALID
  local profile_index = INVALID
  local profile_description = nil

  for p in device:iterate_params("EnumRoute") do
    local route = parseParam(p, "EnumRoute")
    -- Parse pod
    if not route then
      goto skip_enum_route
    end

    if route.direction ~= "Input" then
      goto skip_enum_route
    end

    Log.debug("Route with index: " .. tostring(route.index) .. ", direction: "
          .. route.direction .. ", name: " .. route.name .. ", description: "
          .. route.description .. ", priority: " .. route.priority)
    if route.profiles then
      for _, v in pairs(route.profiles) do
        local priority, index, desc = findProfile(device, v)
        if priority ~= INVALID then
          if profile_priority < priority then
            profile_priority = priority
            profile_index = index
            profile_description = desc
          end
        end
      end
    end

    ::skip_enum_route::
  end

  return profile_priority, profile_index, profile_description
end

local function switchProfile()
  local index
  local desc

  Log.debug("Switching profile, if needed")

  for device in devices_om:iterate() do
    if isProfileSwitched(device) then
      Log.debug("Device already switched:" .. device.properties["device.name"])
      goto skip_device
    end

    local _, cur_profile_index, cur_profile_desc = getCurrentProfile(device)
    saveProfile(device, cur_profile_index, true)

    local saved_headset_profile_idx = getSavedHeadsetProfile(device)
    if saved_headset_profile_idx ~= INVALID then
      _, index, desc = findProfile(device, saved_headset_profile_idx)
    else
      _, index, desc = highestPrioProfileWithInputRoute(device)
    end

    if index ~= INVALID then
      if index == cur_profile_index then
        Log.info("Current profile is saved profile, not switching")
        goto skip_device
      end

      local pod = Pod.Object {
        "Spa:Pod:Object:Param:Profile", "Profile",
        index = index
      }

      Log.info("Setting profile of '"
            .. device.properties["device.description"]
            .. "' from: " .. cur_profile_desc
            .. "' to: " .. desc)
      device:set_params("Profile", pod)
    else
      Log.warning("Got invalid index when switching profile")
    end
    ::skip_device::
  end
end

local function restoreProfile()
  for device in devices_om:iterate() do
    if isProfileSwitched(device) then
      local profile_index = getSavedProfile(device)
      local _, cur_profile_index, cur_profile_desc = getCurrentProfile(device)

      saveProfile(device, INVALID, false)

      if cur_profile_index ~= INVALID then
        Log.info("Setting saved headset profile to: " .. cur_profile_desc)
        saveHeadsetProfile(device, cur_profile_index)
      end

      if profile_index ~= INVALID then
        local _, index, desc = findProfile(device, profile_index)

        if index ~= INVALID then
          if index == cur_profile_index then
            Log.info("Profile to be restored is current")
            saveProfile(device, INVALID, false)
            return
          end

          local pod = Pod.Object {
            "Spa:Pod:Object:Param:Profile", "Profile",
            index = profile_index
          }

          Log.info("Restoring profile of '"
                .. device.properties["device.description"]
                .. "' from: " .. cur_profile_desc
                .. "' to: " .. desc)
          device:set_params("Profile", pod)
        else
          Log.warning("Failed to restore profile")
        end
      end
    end
  end
end

-- We consider a Stream of interest to have role Communication if it has
-- media.role set to Communication in props or it is in our list of
-- applications as these applications do not set media.role correctly or at
-- all.
local function isStreamRoleCommunication(stream)
  local app_name = stream.properties["application.name"]
  local stream_role = stream.properties["media.role"]

  if stream_role == "Communication" or hasValue(applications, app_name) then
    return true
  end

  return false
end

streams_om:connect("object-added", function (_, stream)
  if use_headset_profile then
    if isStreamRoleCommunication(stream) and isBluez5DefaultAudioSink() then
      table.insert(app_node_ids, stream["bound-id"])
      switchProfile()
    end
  end
end)

streams_om:connect("object-removed", function (_, stream)
  if use_headset_profile then
    if isStreamRoleCommunication(stream) then
      removeValue(app_node_ids, stream["bound-id"])
      if next(app_node_ids) == nil then
        restoreProfile()
      end
    end
  end
end)

metadata_om:connect("object-added", function (_, metadata)
  metadata:connect("changed", function (m, subject, key, t, value)
    if (use_headset_profile and subject == 0 and key == "default.audio.sink"
        and isBluez5AudioSink(value)) then
      -- If bluez sink is set as default, rescan for active input streams
      for stream in streams_om:iterate {
        Constraint { "media.class", "matches", "Stream/Input/Audio", type = "pw-global" },
        Constraint { "stream.monitor", "!", "true" }
      } do
        if isStreamRoleCommunication(stream) then
          removeValue(app_node_ids, stream["bound-id"])
          table.insert(app_node_ids, stream["bound-id"])
          switchProfile()
        end
      end
    end
  end)
end)

metadata_om:activate()
devices_om:activate()
streams_om:activate()
