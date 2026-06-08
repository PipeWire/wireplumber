-- WirePlumber
--
-- Copyright © 2024 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Finds the user preferred profile for a device, based on the priorities
-- defined in the "device.profile.priority.rules" section of the configuration.

cutils = require ("common-utils")
log = Log.open_topic ("s-device")

config = {}
config.rules = Conf.get_section_as_json ("device.profile.priority.rules", Json.Array {})

function getRulesProfilePriorities (device)
  local props = JsonUtils.match_rules_update_properties (config.rules,
    device.properties)
  local p_array = props["priorities"]
  if not p_array then
    return nil
  end

  local p_json = Json.Raw (p_array)
  return p_json:parse ()
end

function getPreferredBluetoothProfilePriorities (device)
  if device.properties["device.api"] ~= "bluez5" then
    return nil
  end

  local preference = Settings.get_string ("bluetooth.profile-preference")
  if preference == "latency" then
    log:info (device, "using best latency profile")
    return { "a2dp-auto-prefer-latency" }
  elseif preference == "quality" then
    log:info (device, "using best quality profile")
    return { "a2dp-auto-prefer-quality" }
  end

  log:warning (device, "invalid preference value '" .. preference ..
      "'. Defaulting to best quality profile")
  return { "a2dp-auto-prefer-quality" }
end

SimpleEventHook {
  name = "device/find-preferred-profile",
  after = "device/find-stored-profile",
  before = "device/find-best-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-profile" },
    },
  },
  execute = function (event)
    local selected_profile = event:get_data ("selected-profile")

    -- skip hook if the profile is already selected for this device.
    if selected_profile then
      return
    end

    local device = event:get_subject ()
    local device_name = device.properties["device.name"] or ""

    -- Use device priority rules if any. Otherwise, get the preferred quality or
    -- latency priorities if BT device.
    local priorities = getRulesProfilePriorities (device)
    if priorities == nil then
      priorities = getPreferredBluetoothProfilePriorities (device)
    end
    if priorities == nil then
      log:info (device, string.format (
          "Preferred profile priorities not available for device '%s'",
          device_name))
      return
    end

    -- Find the preferred profile
    for _, priority_profile in ipairs(priorities) do
      for p in device:iterate_params("EnumProfile") do
        local device_profile = cutils.parseParam(p, "EnumProfile")
        if device_profile and device_profile.name == priority_profile then
          selected_profile = device_profile
          goto profile_set
        end
      end
    end

::profile_set::
    if selected_profile then
      log:info (device, string.format (
        "Found preferred profile '%s' (%d) for device '%s'",
        selected_profile.name, selected_profile.index, device_name))
      event:set_data ("selected-profile", selected_profile)
    else
      log:info (device, string.format (
          "Could not find preferred profile for device '%s'", device_name))
    end

  end
}:register()
