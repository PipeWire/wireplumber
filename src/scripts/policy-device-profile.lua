-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- Script selects and enables a profile for a device. It implements the
-- persistant profiles funtionality.

-- Settings file: device.conf

local cutils = require ("common-utils")

local self = {}
self.active_profiles = {}
self.default_profile_plugin = Plugin.find ("default-profile")

-- Checks whether a device profile is persistent or not
function isProfilePersistent (device_props, profile_name)
  local matched, mprops = Settings.apply_rule ("device.rules", device_props)

  if (matched and mprops) then
    if string.find (mprops ["persistent_profile_names"], profile_name) then
      return true
    end
  else
    if profile_name == "off" or profile_name == "pro-audio" then
      return true
    end
  end

  return false
end

function setDeviceProfile (device, dev_id, dev_name, profile)
  if self.active_profiles [dev_id] and
      self.active_profiles [dev_id].index == profile.index then
    Log.info ("Profile " .. profile.name .. " is already set in " .. dev_name)
    return
  end

  local param = Pod.Object {
    "Spa:Pod:Object:Param:Profile", "Profile",
    index = profile.index,
  }
  Log.info ("Setting profile " .. profile.name .. " on " .. dev_name)
  device:set_param ("Profile", param)
end

function findDefaultProfile (device)
  local def_name = nil

  if self.default_profile_plugin ~= nil then
    def_name = self.default_profile_plugin:call ("get-profile", device)
  end
  if def_name == nil then
    return nil
  end

  for p in device:iterate_params ("EnumProfile") do
    local profile = cutils.parseParam (p, "EnumProfile")
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

  for p in device:iterate_params ("EnumProfile") do
    profile = cutils.parseParam (p, "EnumProfile")
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

function handleProfiles (device, new_device)
  local dev_id = device ["bound-id"]
  local dev_name = device.properties ["device.name"]

  if not dev_name then
    return
  end

  local def_profile = findDefaultProfile (device)

  -- Do not do anything if active profile is both persistent and default
  if not new_device and
      self.active_profiles [dev_id] ~= nil and
      isProfilePersistent (device.properties,
          self.active_profiles [dev_id].name) and
      def_profile ~= nil and
      self.active_profiles [dev_id].name == def_profile.name
      then
    local active_profile = self.active_profiles [dev_id].name
    Log.info ("Device profile " .. active_profile .. " is persistent for "
        .. dev_name)
    return
  end

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

  local best_profile = findBestProfile (device)
  if best_profile ~= nil then
    Log.info ("Found best profile " .. best_profile.name .. " for " .. dev_name)
    setDeviceProfile (device, dev_id, dev_name, best_profile)
  else
    Log.info ("Best profile not found on " .. dev_name)
  end
end

function onDeviceParamsChanged (device)
  handleProfiles (device, false)
end

SimpleEventHook {
  name = "device-added@policy-device-profile",
  type = "on-event",
  priority = "device-added-policy-device-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "object-added" },
      Constraint { "event.subject.type", "=", "device" },
    },
  },
  execute = function (event)
    handleProfiles (event:get_subject (), true)
  end
}:register ()

SimpleEventHook {
  name = "params-changed@policy-device-profile",
  type = "on-event",
  priority = "device-params-changed-policy-device-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "params-changed" },
      Constraint { "event.subject.type", "=", "device" },
      Constraint { "event.subject.param-id", "=", "EnumProfile" },
    },
  },
  execute = function (event)
    onDeviceParamsChanged (event:get_subject ())
  end
}:register()

SimpleEventHook {
  name = "device-removed@policy-device-profile",
  type = "on-event",
  priority = "device-removed-policy-device-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "object-removed" },
      Constraint { "event.subject.type", "=", "device" },
    },
  },
  execute = function (event)
    local device = event:get_subject ()
    local dev_id = device ["bound-id"]
    self.active_profiles [dev_id] = nil
  end
}:register ()

