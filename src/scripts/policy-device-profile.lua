-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

local self = {}
self.config = ... or {}
self.active_profiles = {}
self.best_profiles = {}
self.default_profile_plugin = Plugin.find("default-profile")

function parseParam(param, id)
  local parsed = param:parse()
  if parsed.pod_type == "Object" and parsed.object_id == id then
    return parsed.properties
  else
    return nil
  end
end

function setDeviceProfile (device, dev_id, dev_name, profile)
  if self.active_profiles[dev_id] and
      self.active_profiles[dev_id].index == profile.index then
    Log.info ("Profile " .. profile.name .. " is already set in " .. dev_name)
    return
  end

  local param = Pod.Object {
    "Spa:Pod:Object:Param:Profile", "Profile",
    index = profile.index,
  }
  Log.info ("Setting profile " .. profile.name .. " on " .. dev_name)
  device:set_param("Profile", param)
end

function findDefaultProfile (device)
  local def_name = nil

  if self.default_profile_plugin ~= nil then
    def_name = self.default_profile_plugin:call ("get-profile", device)
  end
  if def_name == nil then
    return nil
  end

  for p in device:iterate_params("EnumProfile") do
    local profile = parseParam(p, "EnumProfile")
    if profile.name == def_name then
      return profile
    end
  end

  return nil
end

function findBestProfile (device)
  local off_profile = nil
  local best_profile = nil
  local unk_profile = nil

  for p in device:iterate_params("EnumProfile") do
    profile = parseParam(p, "EnumProfile")
    if profile and profile.name ~= "pro-audio" then
      if profile.name == "off" then
        off_profile = profile
      elseif profile.available == "yes" then
        if best_profile == nil or profile.priority > best_profile.priority then
          best_profile = profile
        end
      elseif profile.available ~= "no" then
        if unk_profile == nil or profile.priority > unk_profile.priority then
          unk_profile = profile
        end
      end
    end
  end

  if best_profile ~= nil then
    return best_profile
  elseif unk_profile ~= nil then
    return unk_profile
  elseif off_profile ~= nil then
    return off_profile
  end

  return nil
end

function handleActiveProfile (device, dev_id, dev_name)
  -- Get active profile
  local profile = nil
  for p in device:iterate_params("Profile") do
    profile = parseParam(p, "Profile")
  end
  if profile == nil then
    Log.info ("Cannot find active profile for device " .. dev_name)
    return false
  end

  -- Update if it has changed
  if self.active_profiles[dev_id] == nil or
      self.active_profiles[dev_id].index ~= profile.index then
    self.active_profiles[dev_id] = profile
    Log.info ("Active profile changed to " .. profile.name .. " in " .. dev_name)
    return true
  end

  return false
end

function handleBestProfile (device, dev_id, dev_name)
  -- Find best profile
  local profile = findBestProfile (device)
  if profile == nil then
    Log.info ("Cannot find best profile for device " .. dev_name)
    return false
  end

  -- Update if it has changed
  if self.best_profiles[dev_id] == nil or
      self.best_profiles[dev_id].index ~= profile.index then
    self.best_profiles[dev_id] = profile
    Log.info ("Best profile changed to " .. profile.name .. " in " .. dev_name)
    return true
  end

  return false
end

function handleProfiles (device)
  local dev_id = device["bound-id"]
  local dev_name = device.properties["device.name"]

  -- Set default device if active profile changed to off
  local active_changed = handleActiveProfile (device, dev_id, dev_name)
  if active_changed and self.active_profiles[dev_id] ~= nil and
      self.active_profiles[dev_id].name == "off" then
    local def_profile = findDefaultProfile (device)
    if def_profile ~= nil then
      if def_profile.available == "no" then
        Log.info ("Default profile " .. def_profile.name .. " unavailable for " .. dev_name)
      else
        Log.info ("Found default profile " .. def_profile.name .. " for " .. dev_name)
        setDeviceProfile (device, dev_id, dev_name, def_profile)
        return
      end
    else
      Log.info ("Default profile not found for " .. dev_name)
    end
  end

  -- Otherwise just set the best profile if changed
  local best_changed = handleBestProfile (device, dev_id, dev_name)
  local best_profile = self.best_profiles[dev_id]
  if best_changed and best_profile ~= nil then
    setDeviceProfile (device, dev_id, dev_name, best_profile)
  elseif best_profile ~= nil then
    Log.info ("Best profile " .. best_profile.name .. " did not change on " .. dev_name)
  else
    Log.info ("Best profile not found on " .. dev_name)
  end
end

function onDeviceParamsChanged (device, param_name)
  if param_name == "EnumProfile" then
    handleProfiles (device)
  end
end

self.om = ObjectManager {
  Interest {
    type = "device",
    Constraint { "device.name", "is-present", type = "pw-global" },
  }
}

self.om:connect("object-added", function (_, device)
  device:connect ("params-changed", onDeviceParamsChanged)
  handleProfiles (device)
end)

self.om:activate()
