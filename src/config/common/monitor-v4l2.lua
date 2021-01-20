-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

function createNode(parent, id, type, factory, properties)
  local dev_props = parent.properties

  -- ensure the node has a name and description
  local devname = dev_props["device.name"]
      or dev_props["device.nick"]
      or dev_props["device.alias"]
      or "v4l2-device"
  properties["node.name"] = factory .. "." .. devname
  properties["node.description"] = dev_props["device.description"] or devname

  -- set the device id and spa factory name; REQUIRED, do not change
  properties["device.id"] = parent["bound-id"]
  properties["factory.name"] = factory

  -- create the node
  local node = Node("spa-node-factory", properties)
  node:activate(Feature.Proxy.BOUND)
  parent:store_managed_object(id, node)
end

function createDevice(parent, id, type, factory, properties)
  -- ensure the device has a name
  properties["device.name"] = properties["device.name"]
      or "v4l2_device." .. (properties["device.bus-id"] or properties["device.bus-path"] or "unknown")

  -- ensure the device has a description
  properties["device.description"] = properties["device.description"]
      or properties["device.product.name"]
      or "Unknown device"

  -- create the device
  local device = SpaDevice(factory, properties)
  device:connect("create-object", createNode)
  device:activate(Feature.SpaDevice.ENABLED | Feature.Proxy.BOUND)
  parent:store_managed_object(id, device)
end

monitor = SpaDevice("api.v4l2.enum.udev")
monitor:connect("create-object", createDevice)
monitor:activate(Feature.SpaDevice.ENABLED)
