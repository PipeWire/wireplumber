-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT
local mutils = require ("monitor-utils")

log = Log.open_topic ("s-monitors-libcam")

SimpleEventHook {
  name = "monitor/libcam/name-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-libcamera-device-node" },
    },
  },
  execute = function(event)
    local properties = event:get_data ("node-properties")
    local parent = event:get_subject ()
    local dev_props = parent.properties
    local factory = event:get_data ("factory")
    local id = event:get_data ("node-sub-id")
    local location = properties ["api.libcamera.location"]

    -- set the device id and spa factory name; REQUIRED, do not change
    properties ["device.id"] = parent ["bound-id"]
    properties ["factory.name"] = factory

    -- set the default pause-on-idle setting
    properties ["node.pause-on-idle"] = false

    -- set the node name
    local name =
        (factory:find ("sink") and "libcamera_output") or
        (factory:find ("source") and "libcamera_input" or factory)
        .. "." ..
        (dev_props ["device.name"]:gsub ("^libcamera_device%.(.+)", "%1") or
          dev_props ["device.name"] or
          dev_props ["device.nick"] or
          dev_props ["device.alias"] or
          "libcamera-device")
    -- sanitize name
    name = name:gsub ("([^%w_%-%.])", "_")

    properties ["node.name"] = name

    -- deduplicate nodes with the same name
    for counter = 2, 99, 1 do
      if mutils.find_duplicate (parent, id, "node.name", properties ["node.name"]) then
        properties ["node.name"] = name .. "." .. counter
      else
        break
      end
    end

    -- set the node description
    local desc = dev_props ["device.description"] or "libcamera-device"
    if location == "front" then
      desc = I18n.gettext ("Built-in Front Camera")
    elseif location == "back" then
      desc = I18n.gettext ("Built-in Back Camera")
    end
    -- sanitize description, replace ':' with ' '
    properties ["node.description"] = desc:gsub ("(:)", " ")

    -- set the node nick
    local nick = properties ["node.nick"] or
        dev_props ["device.product.name"] or
        dev_props ["device.description"] or
        dev_props ["device.nick"]
    properties ["node.nick"] = nick:gsub ("(:)", " ")

    -- set priority
    if not properties ["priority.session"] then
      local priority = 700
      if location == "external" then
        priority = priority + 150
      elseif location == "front" then
        priority = priority + 100
      elseif location == "back" then
        priority = priority + 50
      end
      properties ["priority.session"] = priority
    end

    event:set_data ("node-properties", properties)
  end
}:register ()
