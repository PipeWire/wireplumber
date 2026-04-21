-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- look for new devices and raise select-profile event.

cutils = require ("common-utils")
log = Log.open_topic ("s-device")

SimpleEventHook {
  name = "device/select-profile",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-added" },
    },
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "EnumProfile" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local device = event:get_subject ()
    source:call ("push-event", "select-profile", device, nil)
  end
}:register()

Settings.subscribe ("bluetooth.profile-preference", function ()
  source = source or Plugin.find ("standard-event-source")
  local device_om = source:call ("get-object-manager", "device")
  for device in device_om:iterate () do
    source:call ("push-event", "select-profile", device, nil)
  end
end)
