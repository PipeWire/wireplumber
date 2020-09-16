-- WirePlumber
--
-- Copyright © 2020 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

--
-- Bluez monitor
--

static_object {
  type = "monitor",
  factory = "api.bluez5.enum.dbus",
  callbacks = {
    ["create-child"] = "bluezCreateDevice",
  }
}

function bluezCreateDevice(child_id, type, spa_factory, properties, monitor_props)
  createChild(child_id, {
    type = "exported-device",
    factory = spa_factory,
    properties = properties,
    callbacks = {
      ["create-child"] = "bluezCreateNode",
    }
  })
end

function bluezCreateNode(child_id, type, spa_factory, properties, dev_props)
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

  createChild(child_id, {
    type = "exported-node",
    factory = "adapter",
    properties = properties,
  })
end
