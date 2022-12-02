-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Find the best profile for a device based on profile priorities and
-- availability

cutils = require ("common-utils")

SimpleEventHook {
  name = "find-best-profile@device",
  after = "find-stored-profile@device",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-profile" },
    },
  },
  execute = function (event)
    local selected_profile = event:get_data ("selected-profile")
    local device = event:get_subject ()
    local dev_name = device.properties["device.name"]
    local off_profile = nil
    local best_profile = nil
    local unk_profile = nil

    -- skip hook if profile is already selected
    if selected_profile then
      return
    end

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
      selected_profile = best_profile
    elseif unk_profile ~= nil then
      selected_profile = unk_profile
    elseif off_profile ~= nil then
      selected_profile = off_profile
    end

    if selected_profile then
      Log.info (device, string.format (
          "Found best profile '%s' (%d) for device '%s'",
          selected_profile.name, selected_profile.index, dev_name))
      event:set_data ("selected-profile", selected_profile)
    end
  end
}:register()