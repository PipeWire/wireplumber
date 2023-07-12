-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT
local mutils = require ("monitor-utils")

log = Log.open_topic ("s-monitors-v4l2")

SimpleEventHook {
  name = "monitor/v4l2/name-device",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-v4l2-device" },
    },
  },
  execute = function(event)
    local properties = event:get_properties ()
    local parent = event:get_subject ()
    local id = event:get_data ("device-sub-id")

    local name = "v4l2_device." ..
        (properties["device.name"] or
          properties["device.bus-id"] or
          properties["device.bus-path"] or
          tostring (id)):gsub ("([^%w_%-%.])", "_")

    properties["device.name"] = name

    -- deduplicate devices with the same name
    for counter = 2, 99, 1 do
      if mutils.findDuplicate (parent, id, "device.name", properties["node.name"]) then
        properties["device.name"] = name .. "." .. counter
      else
        break
      end
    end

    -- ensure the device has a description
    properties["device.description"] =
        properties["device.description"]
        or properties["device.product.name"]
        or "Unknown device"

    event:set_data ("device-properties", properties)
  end
}:register ()
