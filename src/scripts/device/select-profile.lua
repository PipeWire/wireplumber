-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- raise select-profile events when devices are added or their profiles change.

cutils = require ("common-utils")
dutils = require ("device-utils")
log = Log.open_topic ("s-device")

SimpleEventHook {
  name = "device/select-profile-on-device-added",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-added" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local device = event:get_subject ()
    source:call ("push-event", "select-profile", device, nil)
  end
}:register()

SimpleEventHook {
  name = "device/select-profile-on-device-enumprofile-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "EnumProfile" },
    },
  },
  execute = function (event)
    local device = event:get_subject ()
    if device.properties ["device.api"] == "bluez5" and
        dutils.is_bluetooth_autoswitch_enabled (device) then
      return
    end

    local source = event:get_source ()
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
