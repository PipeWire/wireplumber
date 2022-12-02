-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")

AsyncEventHook {
  name = "apply-profile@device",
  after = { "find-stored-profile@device", "find-best-profile@device" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-profile" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local device = event:get_subject ()
        local profile = event:get_data ("selected-profile")
        local dev_name = device.properties ["device.name"]

        if not profile then
          Log.info (device, "No profile found to set on " .. dev_name)
          transition:advance ()
          return
        end

        for p in device:iterate_params ("Profile") do
          local active_profile = cutils.parseParam (p, "Profile")
          if active_profile.index == profile.index then
            Log.info (device, "Profile " .. profile.name .. " is already set on " .. dev_name)
            transition:advance ()
            return
          end
        end

        local param = Pod.Object {
          "Spa:Pod:Object:Param:Profile", "Profile",
          index = profile.index,
        }
        Log.info (device, "Setting profile " .. profile.name .. " on " .. dev_name)
        device:set_param ("Profile", param)

        -- FIXME: add cancellability
        -- sync on the pipewire connection to ensure that the param
        -- has been configured on the remote device object
        Core.sync (function ()
          transition:advance ()
        end)
      end
    },
  }
}:register()
