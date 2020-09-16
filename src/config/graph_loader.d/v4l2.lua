-- WirePlumber
--
-- Copyright © 2020 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

--
-- V4L2 monitor
--

static_object {
  type = "monitor",
  factory = "api.v4l2.enum.udev",
  callbacks = {
    ["create-child"] = "v4l2CreateDevice"
  }
}

function v4l2CreateDevice(child_id, type, spa_factory, properties, monitor_props)
  -- ensure the device has a name
  properties["device.name"] = properties["device.name"]
      or "v4l2_device." .. (properties["device.bus-id"] or properties["device.bus-path"] or "unknown")

  -- ensure the device has a description
  properties["device.description"] = properties["device.description"]
      or properties["device.product.name"]
      or "Unknown device"

  -- create the device
  createChild(child_id, {
    type = "exported-device",
    factory = spa_factory,
    properties = properties,
    callbacks = {
      ["create-child"] = "v4l2CreateNode"
    }
  })
end

function v4l2CreateNode(child_id, type, spa_factory, properties, dev_props)
  local devname = dev_props["device.name"]
      or dev_props["device.nick"]
      or dev_props["device.alias"]
      or "v4l2-device"
  properties["node.name"] = spa_factory .. "." .. devname
  properties["node.description"] = dev_props["device.description"] or devname

  properties["factory.name"] = spa_factory

  createChild(child_id, {
    type = "node",
    factory = "spa-node-factory",
    properties = properties,
  })
end
