-- WirePlumber
--
-- Copyright © 2020 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

--
-- V4L2 monitor
--

objects["v4l2"] = {
  type = "monitor",
  factory = "api.v4l2.enum.udev",
  on_create_object = "on_v4l2_monitor_create_object",
}

function on_v4l2_monitor_create_object(child_id, type, spa_factory, properties, monitor_props)
  -- we only expect to handle devices from a monitor
  if type ~= "device" then return end

  -- ensure the device has a name
  properties["device.name"] = properties["device.name"]
      or "v4l2_device." .. (properties["device.bus-id"] or properties["device.bus-path"] or "unknown")

  -- ensure the device has a description
  properties["device.description"] = properties["device.description"]
      or properties["device.product.name"]
      or "Unknown device"

  -- create the device
  local object_description = {
    ["type"] = "exported-device",
    ["factory"] = spa_factory,
    ["properties"] = properties,
    ["on_create_object"] = "on_v4l2_device_create_object",
    ["child_id"] = child_id,
  }
  wp.create_object(object_description)
end

function on_v4l2_device_create_object(child_id, type, spa_factory, properties, dev_props)
  -- we only expect to create nodes
  if type ~= "node" then return end

  local devname = dev_props["device.name"]
      or dev_props["device.nick"]
      or dev_props["device.alias"]
      or "v4l2-device"
  properties["node.name"] = spa_factory .. "." .. devname
  properties["node.description"] = dev_props["device.description"] or devname

  properties["factory.name"] = spa_factory

  local object_description = {
    ["type"] = "node",
    ["factory"] = "spa-node-factory",
    ["properties"] = properties,
    ["child_id"] = child_id,
  }
  wp.create_object(object_description)
end
