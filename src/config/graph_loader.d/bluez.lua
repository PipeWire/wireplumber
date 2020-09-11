-- WirePlumber
--
-- Copyright © 2020 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

--
-- Bluez monitor
--

objects["bluez"] = {
  type = "monitor",
  factory = "api.bluez5.enum.dbus",
  on_create_object = "on_bluez_monitor_create_object",
}

function on_bluez_monitor_create_object(child_id, type, spa_factory, properties, monitor_props)
  -- we only expect to handle devices from a monitor
  if type ~= "device" then return end

  -- create the device
  local object_description = {
    ["type"] = "exported-device",
    ["factory"] = spa_factory,
    ["properties"] = properties,
    ["on_create_object"] = "on_bluez_device_create_object",
    ["child_id"] = child_id,
  }
  wp.create_object(object_description)
end

function on_bluez_device_create_object(child_id, type, spa_factory, properties, dev_props)
  -- we only expect to create nodes
  if type ~= "node" then return end

  local devname = dev_props["device.description"]
      or dev_props["device.name"]
      or dev_props["device.nick"]
      or dev_props["device.alias"]
      or "bluetooth-device"

  properties["node.name"] = spa_factory .. "." .. devname
  properties["node.description"] = devname

  properties["api.bluez5.path"] = dev_props["api.bluez5.path"]
  properties["api.bluez5.address"] = dev_props["api.bluez5.address"]

  properties["factory.name"] = spa_factory

  local object_description = {
    ["type"] = "exported-node",
    ["factory"] = "adapter",
    ["properties"] = properties,
    ["child_id"] = child_id,
  }
  wp.create_object(object_description)
end
