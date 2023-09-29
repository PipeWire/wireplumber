-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT

mutils = require ("monitor-utils")

log = Log.open_topic ("s-monitors-libcam")

SimpleEventHook {
  name = "monitor/libcam/name-device",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-libcam-device" },
    },
  },
  execute = function(event)
    local parent = event:get_subject ()
    local properties = event:get_data ("device-properties")
    local id = event:get_data ("device-sub-id")

    local name = "libcamera_device." ..
        (properties["device.name"] or
          properties["device.bus-id"] or
          properties["device.bus-path"] or
          tostring (id)):gsub ("([^%w_%-%.])", "_")

    properties["device.name"] = name

    -- deduplicate devices with the same name
    for counter = 2, 99, 1 do
      if mutils.find_duplicate (parent, id, "device.name", properties["node.name"]) then
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
