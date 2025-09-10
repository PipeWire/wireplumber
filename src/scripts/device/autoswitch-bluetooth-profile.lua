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
log = Log.open_topic ("s-device")
persistent_storage_hooks_registered = false
autoswitch_hooks_registered = false

local PROFILE_RESTORE_TIMEOUT_MSEC = 2000
local PROFILE_SWITCH_TIMEOUT_MSEC = 500

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

function highestPrioProfileWithInputRoute (device)
  local found_profile = nil
  for p in device:iterate_params ("EnumRoute") do
    local route = cutils.parseParam (p, "EnumRoute")
    if route ~= nil and route.profiles ~= nil and route.direction == "Input" then
      for _, v in pairs (route.profiles) do
        local p = findProfile (device, v)
        if p ~= nil then
          if found_profile == nil or found_profile.priority < p.priority then
            found_profile = p
          end
        end
      end
    end
  end
  return found_profile
end

function highestPrioProfileWithoutInputRoute (device)
  local found_profile = nil
  for p in device:iterate_params ("EnumRoute") do
    local route = cutils.parseParam (p, "EnumRoute")
    if route ~= nil and route.profiles ~= nil and route.direction ~= "Input" then
      for _, v in pairs (route.profiles) do
        local p = findProfile (device, v)
        if p ~= nil then
          if found_profile == nil or found_profile.priority < p.priority then
            found_profile = p
          end
        end
      end
    end
  end
  return found_profile
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

function switchDeviceToHeadsetProfile (dev_id, device_om)
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
  if cur_profile ~= nil and
      hasProfileInputRoute (device, cur_profile.index) then
    log:info (device,
        "Current profile is already a headset profile, no need to switch")
    return
  end

  -- Get saved headset profile if any, otherwise find the highest priority one
  local profile = nil
  local profile_name = getSavedHeadsetProfile (device)
  if profile_name ~= nil then
    profile = findProfile (device, nil, profile_name)
    if profile ~= nil and not hasProfileInputRoute (device, profile.index) then
      saveHeadsetProfile (device, nil, false)
    end
  end
  if profile == nil then
    profile = highestPrioProfileWithInputRoute (device)
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
  if cur_profile ~= nil and
      not hasProfileInputRoute (device, cur_profile.index) then
    log:info (device,
        "Current profile is already a non-headset profile, no need to restore")
    return
  end

  -- Get saved non-headset profile if any, otherwise find the highest priority one
  local profile = nil
  local profile_name = getSavedNonHeadsetProfile (device)
  if profile_name ~= nil then
    profile = findProfile (device, nil, profile_name)
    if profile ~= nil and hasProfileInputRoute (device, profile.index) then
      saveNonHeadsetProfile (device, nil)
    end
  end
  if profile == nil then
    profile = highestPrioProfileWithoutInputRoute (device)
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

function triggerSwitchDeviceToHeadsetProfile (dev_id, device_om)
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
  log:info ("Triggering profile switch on device " .. tostring (dev_id))
  switch_timeout_source[dev_id] = Core.timeout_add (PROFILE_SWITCH_TIMEOUT_MSEC, function ()
    switch_timeout_source[dev_id] = nil
    switchDeviceToHeadsetProfile (dev_id, device_om)
  end)
end

function triggerRestoreProfile (dev_id, device_om)
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
  log:info ("Triggering profile restore on device " .. tostring (dev_id))
  restore_timeout_source[dev_id] = Core.timeout_add (PROFILE_RESTORE_TIMEOUT_MSEC, function ()
    restore_timeout_source[dev_id] = nil
    restoreProfile (dev_id, device_om)
  end)
end

function getLinkedBluetoothLoopbackSourceNodeForStream (stream, node_om, link_om, visited_link_groups)
  local stream_id = stream["bound-id"]

  -- Make sure the node is linked
  local link = link_om:lookup {
    Constraint { "link.input.node", "=", stream_id, type = "pw-global"}
  }
  if link == nil then
    return nil
  end
  local peer_id = link.properties["link.output.node"]

  -- If the peer node is the BT loopback source node, return its Id.
  -- Otherwise recursively advance in the graph if it is linked to a filter.
  local bt_node = node_om:lookup {
      Constraint { "media.class", "matches", "Audio/Source", type = "pw-global" },
      Constraint { "bound-id", "=", peer_id, type = "gobject" },
      Constraint { "bluez5.loopback", "=", "true", type = "pw" }
  }
  if bt_node ~= nil then
    return bt_node
  else
    local filter_main_node = node_om:lookup {
      Constraint { "bound-id", "=", peer_id, type = "gobject" },
      Constraint { "node.link-group", "+", type = "pw" }
    }
    if filter_main_node ~= nil then
      local filter_link_group = filter_main_node.properties ["node.link-group"]
      if visited_link_groups == nil then
        visited_link_groups = {}
      end
      if visited_link_groups [filter_link_group] then
        return nil
      else
        visited_link_groups [filter_link_group] = true
      end
      for filter_stream_node in node_om:iterate {
          Constraint { "media.class", "matches", "Stream/Input/Audio", type = "pw-global" },
          Constraint { "stream.monitor", "!", "true", type = "pw" },
          Constraint { "bluez5.loopback", "!", "true", type = "pw" },
          Constraint { "node.link-group", "=", filter_link_group, type = "pw" }
        } do
        local filter_stream_id = filter_stream_node["bound-id"]
        local bt_node = getLinkedBluetoothLoopbackSourceNodeForStream (filter_stream_id, node_om, link_om, visited_link_groups)
        if bt_node ~= nil then
          return bt_node
        end
      end
    end
  end

  return nil
end

function isBluetoothLoopbackSourceNodeLinkedToStream (bt_node, node_om, link_om)
  local bt_node_id = bt_node["bound-id"]
  for stream in node_om:iterate {
    Constraint { "media.class", "matches", "Stream/Input/Audio", type = "pw-global" },
    Constraint { "node.link-group", "-", type = "pw" },
    Constraint { "stream.monitor", "!", "true", type = "pw" },
    Constraint { "bluez5.loopback", "!", "true", type = "pw" }
  } do
    local linked_bt_node = getLinkedBluetoothLoopbackSourceNodeForStream (stream, node_om, link_om)
    if linked_bt_node ~= nil then
      local linked_bt_node_id = linked_bt_node ["bound-id"]
      if tonumber (linked_bt_node_id) == tonumber (bt_node_id) then
        return true
      end
    end
  end
  return false
end

local evaluate_bluetooth_profiles_hook = SimpleEventHook {
  name = "evaluate-bluetooth-profiles@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "evaluate-bluetooth-profiles" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local node_om = source:call ("get-object-manager", "node")
    local device_om = source:call ("get-object-manager", "device")
    local link_om = source:call ("get-object-manager", "link")

    -- Evaluate all bluetooth loopback source nodes, and switch to headset
    -- profile only if the node is running and linked to a stream that is not a
    -- monitor, otherwise just restore the profile.
    --
    -- If the bluetooth node is linked to a stream that is a monitor, its state
    -- will be 'running', so we cannot just rely on the state to know if we
    -- have to switch or not, we also need to check if the node is linked to
    -- a stream that is not a monitor.
    for bt_node in node_om:iterate {
        Constraint { "media.class", "matches", "Audio/Source" },
        Constraint { "device.id", "+" },
        Constraint { "bluez5.loopback", "=", "true", type = "pw" }
    } do
      local bt_node_state = bt_node["state"]
      local bt_dev_id = bt_node.properties ["device.id"]

      if bt_node_state == "running" and
          isBluetoothLoopbackSourceNodeLinkedToStream (bt_node, node_om, link_om) then
        triggerSwitchDeviceToHeadsetProfile (bt_dev_id, device_om)
      else
        triggerRestoreProfile (bt_dev_id, device_om)
      end
    end
  end
}

local link_added_hook = SimpleEventHook {
  name = "link-added@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "link-added" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local node_om = source:call ("get-object-manager", "node")
    local link = event:get_subject ()
    local in_stream_id = link.properties["link.input.node"]

    -- Only evaluate bluetooth profiles if a capture stream was linked
    local stream = node_om:lookup {
      Constraint { "media.class", "matches", "Stream/Input/Audio", type = "pw-global" },
      Constraint { "node.link-group", "-", type = "pw" },
      Constraint { "stream.monitor", "!", "true", type = "pw" },
      Constraint { "bluez5.loopback", "!", "true", type = "pw" },
      Constraint { "bound-id", "=", in_stream_id, type = "gobject" },
    }
    if stream ~= nil then
      capture_stream_links [link.id] = true
      source:call ("push-event", "evaluate-bluetooth-profiles", nil, nil)
    end
  end
}

local link_removed_hook = SimpleEventHook {
  name = "link-removed@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "link-removed" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local link = event:get_subject ()

    -- Only evaluate bluetooth profiles if a capture stream was unlinked
    if capture_stream_links [link.id] then
      capture_stream_links [link.id] = nil
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
    source:call ("push-event", "evaluate-bluetooth-profiles", nil, nil)
  end
}

local node_added_hook = SimpleEventHook {
  name = "bluez-loopback-added@autoswitch-bluetooth-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "media.class", "matches", "Audio/Source" },
      Constraint { "device.id", "+" },
      Constraint { "bluez5.loopback", "=", "true", type = "pw" }
    },
  },
  execute = function (event)
    local source = event:get_source ()
    source:call ("push-event", "evaluate-bluetooth-profiles", nil, nil)
  end
}

local device_profile_changed_hook = SimpleEventHook {
  name = "device/store-user-selected-profile",
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
      if hasProfileInputRoute (device, cur_profile.index) then
        log:info (device, "Saving headset profile " .. cur_profile.name)
        saveHeadsetProfile (device, cur_profile.name, cur_profile.save)
      else
        log:info (device, "Saving non-headset profile " .. cur_profile.name)
        saveNonHeadsetProfile (device, cur_profile.name)
      end
    end
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
    evaluate_bluetooth_profiles_hook:register ()
    link_added_hook:register ()
    link_removed_hook:register ()
    state_changed_hook:register ()
    node_added_hook:register ()
    device_profile_changed_hook:register ()
    autoswitch_hooks_registered = true
  elseif autoswitch_hooks_registered then
    capture_stream_links = nil
    restore_timeout_source = nil
    switch_timeout_source = nil
    evaluate_bluetooth_profiles_hook:remove ()
    link_added_hook:remove ()
    link_removed_hook:remove ()
    state_changed_hook:remove ()
    node_added_hook:remove ()
    device_profile_changed_hook:remove ()
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
