-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- This file contains all the logic related to saving device profiles
-- to a state file and restoring them later on.

-- A devices profile needs to be selected for any new device. the script selects
-- the device profile from the user preferences, as well as store the user
-- selected device profile to state file

cutils = require ("common-utils")
settings = require ("settings-device")
log = Log.open_topic ("s-device")

-- the state storage
state = nil
state_table = nil

find_stored_profile_hook = SimpleEventHook {
  name = "device/find-stored-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-profile" },
    },
  },
  execute = function (event)
    local selected_profile = event:get_data ("selected-profile")
    local device = event:get_subject ()
    local dev_name = device.properties["device.name"]

    -- skip hook if profile is already selected
    if selected_profile then
      return
    end

    if not dev_name then
      log:critical (device, "invalid device.name")
      return
    end

    local profile_name = state_table[dev_name]

    if profile_name then
      for p in device:iterate_params ("EnumProfile") do
        local profile = cutils.parseParam (p, "EnumProfile")
        if profile.name == profile_name then
          selected_profile = profile
          break
        end
      end
    end

    if selected_profile then
      log:info (device, string.format (
          "Found stored profile '%s' (%d) for device '%s'",
          selected_profile.name, selected_profile.index, dev_name))
      event:set_data ("selected-profile", selected_profile)
    end
  end
}

store_user_selected_profile_hook = SimpleEventHook {
  name = "device/store-user-selected-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "Profile" },
    },
  },
  execute = function (event)
    local device = event:get_subject ()

    for p in device:iterate_params ("Profile") do
      local profile = cutils.parseParam (p, "Profile")
      if profile.save then
        -- store only if this was a user-generated action (save == true)
        updateStoredProfile (device, profile)
      end
    end
  end
}

function updateStoredProfile (device, profile)
  local dev_name = device.properties["device.name"]
  local index = nil

  if not dev_name then
    log:critical (device, "invalid device.name")
    return
  end

  log:debug (device, string.format (
      "update stored profile to '%s' (%d) for device '%s'",
      profile.name, profile.index, dev_name))

  -- check if the new profile is the same as the current one
  if state_table[dev_name] == profile.name then
    log:debug (device, " ... profile is already stored")
    return
  end

  -- find the full profile from EnumProfile, making also sure that the
  -- user / client application has actually set an existing profile
  for p in device:iterate_params ("EnumProfile") do
    local enum_profile = cutils.parseParam (p, "EnumProfile")
    if enum_profile.name == profile.name then
      index = enum_profile.index
    end
  end

  if not index then
    log:info (device, string.format (
        "profile '%s' (%d) is not valid on device '%s'",
        profile.name, profile.index, dev_name))
    return
  end

  state_table[dev_name] = profile.name
  cutils.storeAfterTimeout (state, state_table)

  log:info (device, string.format (
      "stored profile '%s' (%d) for device '%s'",
      profile.name, index, dev_name))
end

function toggleState (enable)
  if enable and not state then
    state = State ("default-profile")
    state_table = state:load ()
    find_stored_profile_hook:register ()
    store_user_selected_profile_hook:register ()
  elseif not enable and state then
    state = nil
    state_table = nil
    find_stored_profile_hook:remove ()
    store_user_selected_profile_hook:remove ()
  end
end

settings:subscribe ("restore-profile", toggleState)
toggleState (settings.restore_profile)
