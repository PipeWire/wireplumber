-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

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
