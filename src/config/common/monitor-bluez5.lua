-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

function createNode(parent, id, type, factory, properties)
  local dev_props = parent.properties

  -- ensure the node has a name and description
  local devname = dev_props["device.description"]
      or dev_props["device.name"]
      or dev_props["device.nick"]
      or dev_props["device.alias"]
      or "bluetooth-device"
  properties["node.name"] = factory .. "." .. devname
  properties["node.description"] = devname

  -- transfer path & address from the device to the node
  properties["api.bluez5.path"] = dev_props["api.bluez5.path"]
  properties["api.bluez5.address"] = dev_props["api.bluez5.address"]

  -- set the device id and spa factory name; REQUIRED, do not change
  properties["device.id"] = parent["bound-id"]
  properties["factory.name"] = factory

  -- create the node; bluez requires "local" nodes, i.e. ones that run in
  -- the same process as the spa device, for several reasons
  local node = LocalNode("adapter", properties)
  node:activate(Feature.Proxy.BOUND)
  parent:store_managed_object(id, node)
end

function createDevice(parent, id, type, factory, properties)
  -- create the device
  local device = SpaDevice(factory, properties)
  device:connect("create-object", createNode)
  device:activate(Feature.SpaDevice.ENABLED | Feature.Proxy.BOUND)
  parent:store_managed_object(id, device)
end

monitor = SpaDevice("api.bluez5.enum.dbus")
monitor:connect("create-object", createDevice)
monitor:activate(Feature.SpaDevice.ENABLED)
