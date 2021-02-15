-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

local config = ... or {}

-- preprocess rules and create Interest objects
for _, r in ipairs(config.rules or {}) do
  r.interests = {}
  for _, i in ipairs(r.matches) do
    local interest_desc = { type = "properties" }
    for _, c in ipairs(i) do
      c.type = "pw"
      table.insert(interest_desc, Constraint(c))
    end
    local interest = Interest(interest_desc)
    table.insert(r.interests, interest)
  end
  r.matches = nil
end

-- applies properties from config.rules when asked to
function rulesApplyProperties(properties)
  for _, r in ipairs(config.rules or {}) do
    if r.apply_properties then
      for _, interest in ipairs(r.interests) do
        if interest:matches(properties) then
          for k, v in pairs(r.apply_properties) do
            properties[k] = v
          end
        end
      end
    end
  end
end

function createNode(parent, id, type, factory, properties)
  local dev_props = parent.properties

  -- set the device id and spa factory name; REQUIRED, do not change
  properties["device.id"] = parent["bound-id"]
  properties["factory.name"] = factory

  -- set the default pause-on-idle setting
  properties["node.pause-on-idle"] = false

  -- set the node description
  properties["node.description"] =
      dev_props["device.description"]
      or dev_props["device.name"]
      or dev_props["device.nick"]
      or dev_props["device.alias"]
      or "bluetooth-device"

  -- set the node name
  local name =
      ((factory:find("sink") and "bluez_output") or
       (factory:find("source") and "bluez_input" or factory)) .. "." ..
      (properties["api.bluez5.address"] or dev_props["device.name"]) .. "." ..
      (properties["api.bluez5.profile"] or "unknown")
  -- sanitize name
  properties["node.name"] = name:gsub("([^%w_%-%.])", "_")

  -- set priority
  if not properties["priority.driver"] then
    local priority = factory:find("source") and 2010 or 1010
    properties["priority.driver"] = priority
    properties["priority.session"] = priority
  end

  -- apply properties from config.rules
  rulesApplyProperties(properties)

  -- create the node; bluez requires "local" nodes, i.e. ones that run in
  -- the same process as the spa device, for several reasons
  local node = LocalNode("adapter", properties)
  node:activate(Feature.Proxy.BOUND)
  parent:store_managed_object(id, node)
end

function createDevice(parent, id, type, factory, properties)
  -- apply properties from config.rules
  rulesApplyProperties(properties)

  -- create the device
  local device = SpaDevice(factory, properties)
  device:connect("create-object", createNode)
  device:activate(Feature.SpaDevice.ENABLED | Feature.Proxy.BOUND)
  parent:store_managed_object(id, device)
end

monitor = SpaDevice("api.bluez5.enum.dbus", config.properties or {})
monitor:connect("create-object", createDevice)
monitor:activate(Feature.SpaDevice.ENABLED)
