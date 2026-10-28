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
-- capture stream is linked to the device's loopback source stream, the
-- associated BT device profile is automatically switched to HSP/HFP. If there
-- is no capture stream linked to the device's loopback source stream, the BT
-- device profile is switched back to A2DP profile.
--
-- We switch to the highest priority profile that has an Input route available.
-- The reason for this is that we may have microphone enabled with non-HFP
-- codecs eg. Faststream.
-- When a stream goes away if the list with which we track the streams above
-- is empty, then we revert back to the old profile.

-- settings file: bluetooth.conf

cutils = require ("common-utils")
log = Log.open_topic ("s-device")
persistent_storage_hooks_registered = false
autoswitch_hooks_registered = false

local state = nil
local headset_profiles = {}
local non_headset_profiles = {}
local capture_stream_links = {}
local restore_timeout_source = {}
local switch_timeout_source = {}

function saveHeadsetProfile (device, profile_name, persistent)
  local key = "saved-headset-profile:" .. device.properties ["device.name"]
  headset_profiles [key] = profile_name
  if state ~= nil and persistent then
    state:save_after_timeout (headset_profiles)
  end
end

function getSavedHeadsetProfile (device)
  local key = "saved-headset-profile:" .. device.properties ["device.name"]
  return headset_profiles [key]
end

function saveNonHeadsetProfile (device, profile_name)
  non_headset_profiles [device.properties ["device.name"]] = profile_name
end

function getSavedNonHeadsetProfile (device)
  return non_headset_profiles [device.properties ["device.name"]]
end

function findProfile (device, index, name)
  for p in device:iterate_params ("EnumProfile") do
    local profile = cutils.parseParam (p, "EnumProfile")
    if profile ~= nil then
      if (index ~= nil and profile.index == index) or
         (name ~= nil and profile.name == name) then
        return profile
      end
    end
  end

  return nil
end

function getCurrentProfile (device)
  for p in device:iterate_params ("Profile") do
    local profile = cutils.parseParam (p, "Profile")
    if profile then
      return profile
    end
  end
  return nil
end

function hasProfileInputRoute (device, profile_index)
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

function isHeadsetProfile (device, profile)
  if hasProfileInputRoute (device, profile.index) and
      (string.find (profile.name, "^headset%-head%-unit") or profile.name == "bap-duplex") then
    return true
  else
    return false
  end
end

function highestPrioHeadsetProfile (device)
  local found_profile = nil
  for p in device:iterate_params ("EnumRoute") do
    local route = cutils.parseParam (p, "EnumRoute")
    if route ~= nil and route.profiles ~= nil and route.direction == "Input" then
      for _, v in pairs (route.profiles) do
        local p = findProfile (device, v)
        if p ~= nil and isHeadsetProfile (device, p) then
          if found_profile == nil or found_profile.priority < p.priority then
            found_profile = p
          end
        end
      end
    end
  end
  return found_profile
end

function highestPrioNonHeadsetProfile (device)
  local found_profile = nil
  for p in device:iterate_params ("EnumRoute") do
    local route = cutils.parseParam (p, "EnumRoute")
    if route ~= nil and route.profiles ~= nil and route.direction ~= "Input" then
      for _, v in pairs (route.profiles) do
        local p = findProfile (device, v)
        if p ~= nil and not isHeadsetProfile (device, p) then
          if found_profile == nil or found_profile.priority < p.priority then
            found_profile = p
          end
        end
      end
    end
  end
  return found_profile
end

function switchDeviceToHeadsetProfile (dev_id, device_om, stream_om)
  -- Find the actual device
  local device = device_om:lookup {
      Constraint { "bound-id", "=", dev_id, type = "gobject" }
  }
  if device == nil then
    log:info ("Device with id " .. tostring(dev_id).. " not found")
    return
  end

  -- Do not switch if the current profile is already a headset profile
  local cur_profile = getCurrentProfile (device)
  if cur_profile ~= nil and isHeadsetProfile (device, cur_profile) then
    log:info (device,
        "Current profile is already a headset profile, no need to switch")
    return
  elseif cur_profile == nil then
    log:info (device, "Could not get current profile, not switching")
    return
  end

  -- Do not switch if the current profile already has an A2DP duplex source
  local duplex_source = stream_om:lookup {
      type = "AudioStream",
      Constraint { "device.id", "=", tostring (dev_id), type = "wp" },
      Constraint { "shape", "=", StreamShape.SOURCE, type = "gobject" },
      Constraint { "api.bluez5.a2dp-duplex", "=", "true", type = "wp" }
  }
  if duplex_source ~= nil then
    log:info (device,
        "Current profile already has an A2DP duplex source, no need to switch")
    return
  end

  -- Get saved headset profile if any, otherwise find the highest priority one
  local profile = nil
  local profile_name = getSavedHeadsetProfile (device)
  if profile_name ~= nil then
    profile = findProfile (device, nil, profile_name)
    if profile ~= nil and not isHeadsetProfile (device, profile) then
      saveHeadsetProfile (device, nil, false)
      profile = nil
    end
  end
  if profile == nil then
    profile = highestPrioHeadsetProfile (device)
  end

  -- Switch if headset profile was found
  if profile ~= nil then
    local pod = Pod.Object {
      "Spa:Pod:Object:Param:Profile", "Profile",
      index = profile.index,
      save = false
    }
    log:info (device, "Switching profile from: " .. cur_profile.name
          .. " to: " .. profile.name)
    device:set_params ("Profile", pod)
  else
    log:warning ("Could not find valid headset profile, not switching")
  end
end

function restoreProfile (dev_id, device_om)
  -- Find the actual device
  local device = device_om:lookup {
      Constraint { "bound-id", "=", dev_id, type = "gobject" }
  }
  if device == nil then
    log:info ("Device with id " .. tostring(dev_id).. " not found")
    return
  end

  -- Do not restore if the current profile is already a non-headset profile
  local cur_profile = getCurrentProfile (device)
  if cur_profile ~= nil and not isHeadsetProfile (device, cur_profile) then
    log:info (device,
        "Current profile is already a non-headset profile, no need to restore")
    return
  elseif cur_profile == nil then
    log:info (device, "Could not get current profile, not switching")
    return
  end

  -- Get saved non-headset profile if any, otherwise find the highest priority one
  local profile = nil
  local profile_name = getSavedNonHeadsetProfile (device)
  if profile_name ~= nil then
    profile = findProfile (device, nil, profile_name)
    if profile ~= nil and isHeadsetProfile (device, profile) then
      saveNonHeadsetProfile (device, nil)
      profile = nil
    end
  end
  if profile == nil then
    profile = highestPrioNonHeadsetProfile (device)
  end

  -- Restore if non-headset profile was found
  if profile ~= nil then
    local pod = Pod.Object {
      "Spa:Pod:Object:Param:Profile", "Profile",
      index = profile.index,
      save = false
    }
    log:info (device, "Restoring profile from: " .. cur_profile.name
          .. " to: " .. profile.name)
    device:set_params ("Profile", pod)
  else
    log:warning ("Could not find valid non-headset profile, not switching")
  end
end

function triggerSwitchDeviceToHeadsetProfile (source, dev_id)
  -- Always clear any pending restore/switch callbacks when triggering a new switch
  if restore_timeout_source[dev_id] ~= nil then
    restore_timeout_source[dev_id]:destroy ()
    restore_timeout_source[dev_id] = nil
    log:info ("Cancelled profile restore on device " .. tostring (dev_id))
  end
  if switch_timeout_source[dev_id] ~= nil then
    switch_timeout_source[dev_id]:destroy ()
    switch_timeout_source[dev_id] = nil
    log:info ("Cancelled profile switch on device " .. tostring (dev_id))
  end

  -- create new switch callback
  local timeout = Settings.get_int ("bluetooth.autoswitch-switch-timeout")
  log:info ("Triggering profile switch on device " .. tostring (dev_id))
  switch_timeout_source[dev_id] = Core.timeout_add (timeout, function ()
    switch_timeout_source[dev_id] = nil

    local e = source:call ("create-event", "autoswitch-bluez-headset-profile", nil, nil)
    e:set_data ("device-id", dev_id)
    EventDispatcher.push_event (e)
  end)
end

function triggerRestoreProfile (source, dev_id)
  -- Always clear any pending restore/switch callbacks when triggering a new restore
  if switch_timeout_source[dev_id] ~= nil then
    switch_timeout_source[dev_id]:destroy ()
    switch_timeout_source[dev_id] = nil
    log:info ("Cancelled profile switch on device " .. tostring (dev_id))
  end
  if restore_timeout_source[dev_id] ~= nil then
    restore_timeout_source[dev_id]:destroy ()
    restore_timeout_source[dev_id] = nil
    log:info ("Cancelled profile restore on device " .. tostring (dev_id))
  end

  -- create new restore callback
  local timeout = Settings.get_int ("bluetooth.autoswitch-restore-timeout")
  log:info ("Triggering profile restore on device " .. tostring (dev_id))
  restore_timeout_source[dev_id] = Core.timeout_add (timeout, function ()
    restore_timeout_source[dev_id] = nil

    local e = source:call ("create-event", "autoswitch-bluez-a2dp-profile", nil, nil)
    e:set_data ("device-id", dev_id)
    EventDispatcher.push_event (e)
  end)
end

local bt_loopback_source_interest = Interest {
  type = "AudioStream",
  Constraint { "shape", "=", StreamShape.SOURCE, type = "gobject" },
  Constraint { "device.id", "+", type = "wp" },
  Constraint { "bluez5.loopback", "=", "true", type = "wp" }
}

local capture_stream_interest = Interest {
  type = "AudioStream",
  Constraint { "shape", "=", StreamShape.INPUT, type = "gobject" },
  Constraint { "node.link-group", "-", type = "wp" },
  Constraint { "stream.monitor", "!", "true", type = "wp" },
  Constraint { "bluez5.loopback", "!", "true", type = "wp" }
}

-- Checks if the stream is a capture stream, which is an audio input stream
-- whose node has no link group and no output stream
function isCaptureStream (stream, stream_om)
  if not capture_stream_interest:matches (stream) then
    return false
  end
  local node_id = stream:get_node_bound_id ()
  for output_stream in stream_om:iterate {
      type = "AudioStream",
      Constraint { "direction", "=", Direction.OUTPUT, type = "gobject" }
    } do
    if output_stream:get_node_bound_id () == node_id then
      return false
    end
  end
  return true
end

function getLinkedBluetoothLoopbackSourceStream (stream, stream_om, visited_streams)
  if visited_streams == nil then
    visited_streams = {}
  end
  if visited_streams [stream.id] then
    return nil
  end
  visited_streams [stream.id] = true

  for stream_link in StreamLinkManager.iterate_links (stream) do
    -- Only follow the established links that feed this stream
    if stream_link:get_input_stream () ~= stream or
        stream_link:get_links_count () == 0 then
      goto continue
    end

    -- If the peer stream is the BT loopback source stream, return it.
    -- Otherwise recursively advance in the graph if it is linked to a filter.
    local peer = stream_link:get_output_stream ()
    if bt_loopback_source_interest:matches (peer) then
      return peer
    end

    -- A filter is either several nodes in the same link group, or one node
    -- with several streams, so continue from the input streams of the peer's
    -- link group if it has one, otherwise from the input streams of its node
    local peer_node_id = peer:get_node_bound_id ()
    local filter_link_group = peer:get_property ("node.link-group")
    for filter_stream in stream_om:iterate {
        type = "AudioStream",
        Constraint { "direction", "=", Direction.INPUT, type = "gobject" },
        Constraint { "stream.monitor", "!", "true", type = "wp" },
        Constraint { "bluez5.loopback", "!", "true", type = "wp" }
      } do
      local is_filter_stream
      if filter_link_group ~= nil then
        is_filter_stream =
            filter_stream:get_property ("node.link-group") == filter_link_group
      else
        is_filter_stream = filter_stream:get_node_bound_id () == peer_node_id
      end
      if is_filter_stream then
        local bt_stream =
            getLinkedBluetoothLoopbackSourceStream (filter_stream, stream_om, visited_streams)
        if bt_stream ~= nil then
          return bt_stream
        end
      end
    end

    ::continue::
  end

  return nil
end

function isBluetoothLoopbackSourceStreamLinked (bt_stream, stream_om)
  for stream in stream_om:iterate (capture_stream_interest) do
    if isCaptureStream (stream, stream_om) and
        getLinkedBluetoothLoopbackSourceStream (stream, stream_om) == bt_stream then
      return true
    end
  end
  return false
end

local switch_profile_hook = AsyncEventHook {
  name = "switch-profile@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "autoswitch-bluez-headset-profile" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local source = event:get_source ()
        local device_om = source:call ("get-object-manager", "device")
        local stream_om = source:call ("get-object-manager", "stream")
        local device_id = event:get_data ("device-id")

        -- Switch profile
        switchDeviceToHeadsetProfile (device_id, device_om, stream_om)

        -- Wait until the profile is applied
        Core.sync (function ()
          transition:advance ()
        end)
      end
    },
  }
}

local restore_profile_hook = AsyncEventHook {
  name = "restore-profile@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "autoswitch-bluez-a2dp-profile" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local source = event:get_source ()
        local device_om = source:call ("get-object-manager", "device")
        local device_id = event:get_data ("device-id")

        -- Restore profile
        restoreProfile (device_id, device_om)

        -- Wait until the profile is applied
        Core.sync (function ()
          transition:advance ()
        end)
      end
    },
  }
}

local evaluate_bluetooth_profiles_hook = SimpleEventHook {
  name = "evaluate-bluetooth-profiles@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "evaluate-bluetooth-profiles" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local stream_om = source:call ("get-object-manager", "stream")
    local needs_headset_profile = {}

    -- Evaluate all bluetooth loopback source streams, and switch to headset
    -- profile only if the node is running and the stream is linked to a
    -- capture stream that is not a monitor, otherwise just restore the profile.
    --
    -- If the bluetooth stream is linked to a stream that is a monitor, its
    -- node state will be 'running', so we cannot just rely on the state to know
    -- if we have to switch or not, we also need to check if the stream is
    -- linked to a stream that is not a monitor.

    -- Check which devices need to switch to headset profile
    for bt_stream in stream_om:iterate (bt_loopback_source_interest) do
      local bt_node = bt_stream:get_node ()
      local bt_dev_id = bt_stream:get_property ("device.id")
      if not needs_headset_profile [bt_dev_id] then
        local is_running = bt_node ~= nil and bt_node["state"] == "running"
        local is_linked = isBluetoothLoopbackSourceStreamLinked (bt_stream, stream_om)
        needs_headset_profile [bt_dev_id] = is_running and is_linked
      end
    end

    -- Switch or retores the profile
    for bt_dev_id, needs_headset in pairs (needs_headset_profile) do
      if needs_headset then
        triggerSwitchDeviceToHeadsetProfile (source, bt_dev_id)
      else
        triggerRestoreProfile (source, bt_dev_id)
      end
    end
  end
}

local stream_link_added_hook = SimpleEventHook {
  name = "link-added@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-link-added" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local stream_om = source:call ("get-object-manager", "stream")
    local stream_link = event:get_subject ()
    local in_stream = stream_link:get_input_stream ()
    local out_stream = stream_link:get_output_stream ()

    -- Only evaluate bluetooth profiles if a capture stream was linked, or if
    -- the BT loopback source stream was linked to anything (e.g. a filter
    -- chain that gets linked after the capture stream was linked to the filter)
    if isCaptureStream (in_stream, stream_om) or
        bt_loopback_source_interest:matches (out_stream) then
      capture_stream_links [stream_link.id] = true
      source:call ("push-event", "evaluate-bluetooth-profiles", nil, nil)
    end
  end
}

local stream_link_removed_hook = SimpleEventHook {
  name = "link-removed@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-link-removed" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local stream_link = event:get_subject ()

    -- Only evaluate bluetooth profiles if a capture stream was unlinked
    if capture_stream_links [stream_link.id] then
      capture_stream_links [stream_link.id] = nil
      source:call ("push-event", "evaluate-bluetooth-profiles", nil, nil)
    end
  end
}

local state_changed_hook = SimpleEventHook {
  name = "bluez-loopback-state-changed@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-state-changed" },
      Constraint { "media.class", "matches", "Audio/Source" },
      Constraint { "device.id", "+" },
      Constraint { "bluez5.loopback", "=", "true", type = "pw" }
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local node = event:get_subject ()
    local old_state = event:get_properties ()["event.subject.old-state"]
    local new_state = event:get_properties ()["event.subject.new-state"]

    log:info (node, "state changed from '" .. old_state .. "' to '" .. new_state .. "'")

    -- Dont evaluate if the state changed from idle to suspended
    if old_state == "idle" and new_state == "suspended" then
      return
    end

    source:call ("push-event", "evaluate-bluetooth-profiles", nil, nil)
  end
}

local stream_added_hook = SimpleEventHook {
  name = "bluez-loopback-added@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "shape", "=", StreamShape.SOURCE, type = "gobject" },
      Constraint { "device.id", "+" },
      Constraint { "bluez5.loopback", "=", "true" }
    },
  },
  execute = function (event)
    local source = event:get_source ()
    source:call ("push-event", "evaluate-bluetooth-profiles", nil, nil)
  end
}

local device_profile_changed_hook = SimpleEventHook {
  name = "bluez-profile-changed@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "Profile" },
      Constraint { "device.api", "=", "bluez5" },
    },
  },
  execute = function (event)
    local device = event:get_subject ()

    -- Always save the current profile when it changes
    local cur_profile = getCurrentProfile (device)
    if cur_profile ~= nil then
      if isHeadsetProfile (device, cur_profile) then
        log:info (device, "Saving headset profile " .. cur_profile.name)
        saveHeadsetProfile (device, cur_profile.name, cur_profile.save)
      else
        log:info (device, "Saving non-headset profile " .. cur_profile.name)
        saveNonHeadsetProfile (device, cur_profile.name)
      end
    end
  end
}

-- select-profile.lua ignores EnumProfile changes while autoswitch is enabled,
-- so restore here a non-headset profile that became available, unless a
-- capture stream is switching to or using the headset profile
local device_enumprofile_changed_hook = SimpleEventHook {
  name = "bluez-enumprofile-changed@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "EnumProfile" },
      Constraint { "device.api", "=", "bluez5" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local device = event:get_subject ()
    local stream_om = source:call ("get-object-manager", "stream")
    local dev_id = tostring (device["bound-id"])

    if switch_timeout_source[dev_id] ~= nil then
      return
    end

    for bt_stream in stream_om:iterate (bt_loopback_source_interest) do
      if bt_stream:get_property ("device.id") == dev_id and
          isBluetoothLoopbackSourceStreamLinked (bt_stream, stream_om) then
        return
      end
    end

    triggerRestoreProfile (source, dev_id)
  end
}

function evaluatePersistentStorage ()
  if Settings.get_boolean ("bluetooth.use-persistent-storage") and
      not persistent_storage_hooks_registered then
    state = State ("bluetooth-autoswitch")
    headset_profiles = state:load ()
    persistent_storage_hooks_registered = true
  elseif persistent_storage_hooks_registered then
    state = nil
    headset_profiles = {}
    persistent_storage_hooks_registered = false
  end
end

function evaluateAutoswitch ()
  if Settings.get_boolean ("bluetooth.autoswitch-to-headset-profile") and
      not autoswitch_hooks_registered then
    capture_stream_links = {}
    restore_timeout_source = {}
    switch_timeout_source = {}
    switch_profile_hook:register ()
    restore_profile_hook:register ()
    evaluate_bluetooth_profiles_hook:register ()
    stream_link_added_hook:register ()
    stream_link_removed_hook:register ()
    state_changed_hook:register ()
    stream_added_hook:register ()
    device_profile_changed_hook:register ()
    device_enumprofile_changed_hook:register ()
    autoswitch_hooks_registered = true
  elseif autoswitch_hooks_registered then
    capture_stream_links = nil
    restore_timeout_source = nil
    switch_timeout_source = nil
    switch_profile_hook:remove ()
    restore_profile_hook:remove ()
    evaluate_bluetooth_profiles_hook:remove ()
    stream_link_added_hook:remove ()
    stream_link_removed_hook:remove ()
    state_changed_hook:remove ()
    stream_added_hook:remove ()
    device_profile_changed_hook:remove ()
    device_enumprofile_changed_hook:remove ()
    autoswitch_hooks_registered = false
  end
end

Settings.subscribe ("bluetooth.use-persistent-storage", function ()
  evaluatePersistentStorage ()
end)
evaluatePersistentStorage ()

Settings.subscribe ("bluetooth.autoswitch-to-headset-profile", function ()
  evaluateAutoswitch ()
end)
evaluateAutoswitch ()
