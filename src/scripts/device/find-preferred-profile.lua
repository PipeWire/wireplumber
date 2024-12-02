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
    local props = JsonUtils.match_rules_update_properties (
        config.rules, device.properties)
    local p_array = props["priorities"]

    -- skip hook if the profile priorities are NOT defined for this device.
    if not p_array then
      return nil
    end

    local p_json = Json.Raw(p_array)
    local priorities = p_json:parse()
    local device_name = device.properties["device.name"] or ""

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
      log:info (device, "Profiles listed in 'device.profile.priority.rules'"
        .. " do not match the available ones of device: " .. device_name)
    end

  end
}:register()
