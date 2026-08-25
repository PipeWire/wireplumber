-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Find the best profile for a device based on profile priorities and
-- availability

cutils = require ("common-utils")
putils = require ("profile-utils")
log = Log.open_topic ("s-device")

SimpleEventHook {
  name = "device/find-best-profile",
  after = "device/find-preferred-profile",
  before = "device/apply-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-profile" },
    },
  },
  execute = function (event)
    local selected_profile = event:get_data ("selected-profile")

    -- skip hook if profile is already selected
    if selected_profile then
      return
    end

    local device = event:get_subject ()
    local dev_name = device.properties["device.name"] or ""
    local off_profile = nil
    local best_profile = nil
    local unk_profile = nil
    -- Takes absolute priority if available or unknown
    local profile_prop = device.properties["device.profile"]

    -- rank candidates on the output routes they actually offer, not just on
    -- the profile priority; see profile-utils.compareProfiles()
    local routes = {}
    for r in device:iterate_params ("EnumRoute") do
      local route = cutils.parseParam (r, "EnumRoute")
      if route then
        table.insert (routes, route)
      end
    end
    local scores = putils.buildOutputRouteScores (routes)

    for p in device:iterate_params ("EnumProfile") do
      profile = cutils.parseParam (p, "EnumProfile")
      if profile and profile.name == profile_prop and profile.available ~= "no" then
        selected_profile = profile
        goto profile_set
      elseif profile and profile.name ~= "pro-audio" then
        if profile.name == "off" then
          off_profile = profile
        elseif profile.available == "yes" then
          if best_profile == nil or
              putils.compareProfiles (profile, best_profile, scores) then
            best_profile = profile
          end
        elseif profile.available ~= "no" then
          if unk_profile == nil or
              putils.compareProfiles (profile, unk_profile, scores) then
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

::profile_set::
    if selected_profile then
      local score = scores [tonumber (selected_profile.index)] or
          { yes = 0, unknown = 0 }
      log:info (device, string.format (
          "Found best profile '%s' (%d) for device '%s' "
          .. "(%d available, %d unknown output routes)",
          selected_profile.name, selected_profile.index, dev_name,
          score.yes, score.unknown))
      event:set_data ("selected-profile", selected_profile)
    end
  end
}:register()
