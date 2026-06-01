-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

COMBINE_OFFSET = 64

log = Log.open_topic ("s-monitors-bluez")

SimpleEventHook {
  name = "monitor/bluez/remove-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "remove-bluez-device-node" },
    },
  },
  execute = function(event)
    local parent = event:get_subject ()
    local id = event:get_data ("node-sub-id")

    local dev_props = parent.properties
    local parent_spa_id = dev_props:get_int ("spa.object.id")

    log:debug("Remove node: " .. tostring (id))

     -- Clear also the device set module, if any
    parent:store_managed_object(id + COMBINE_OFFSET, nil)
  end
}:register ()
