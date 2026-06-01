-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-monitors-bluez")

SimpleEventHook {
  name = "monitor/bluez/remove-device",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "remove-bluez-device" },
    },
  },
  execute = function(event)
    local id = event:get_data ("device-sub-id")

    log:debug("Remove device: " .. tostring (id))
  end
}:register ()
