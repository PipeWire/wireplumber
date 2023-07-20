-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT
local mutils = require ("monitor-utils")

log = Log.open_topic ("s-monitors-v4l2")

SimpleEventHook {
  name = "monitor/v4l2/name-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-v4l2-device-node" },
    },
  },
  execute = function(event)
    local properties = event:get_data ("node-properties")
    local parent = event:get_subject ()
    local dev_props = parent.properties
    local factory = event:get_data ("factory")
    local id = event:get_data ("node-sub-id")

    -- set the device id and spa factory name; REQUIRED, do not change
    properties["device.id"] = parent["bound-id"]
    properties["factory.name"] = factory

    -- set the default pause-on-idle setting
    properties["node.pause-on-idle"] = false

    -- set the node name
    local name =
        (factory:find ("sink") and "v4l2_output") or
        (factory:find ("source") and "v4l2_input" or factory)
        .. "." ..
        (dev_props["device.name"]:gsub ("^v4l2_device%.(.+)", "%1") or
          dev_props["device.name"] or
          dev_props["device.nick"] or
          dev_props["device.alias"] or
          "v4l2-device")
    -- sanitize name
    name = name:gsub ("([^%w_%-%.])", "_")

    properties["node.name"] = name

    -- deduplicate nodes with the same name
    for counter = 2, 99, 1 do
      if mutils.find_duplicate (parent, id, "node.name", properties["node.name"]) then
        properties["node.name"] = name .. "." .. counter
      else
        break
      end
    end

    -- set the node description
    local desc = dev_props["device.description"] or "v4l2-device"
    desc = desc .. " (V4L2)"
    -- sanitize description, replace ':' with ' '
    properties["node.description"] = desc:gsub ("(:)", " ")

    -- set the node nick
    local nick = properties["node.nick"] or
        dev_props["device.product.name"] or
        dev_props["api.v4l2.cap.card"] or
        dev_props["device.description"] or
        dev_props["device.nick"]
    properties["node.nick"] = nick:gsub ("(:)", " ")

    -- set priority
    if not properties["priority.session"] then
      local path = properties["api.v4l2.path"] or "/dev/video100"
      local dev = path:gsub ("/dev/video(%d+)", "%1")
      properties["priority.session"] = 1000 - (tonumber (dev) * 10)
    end

    event:set_data ("node-properties", properties)
  end
}:register ()
