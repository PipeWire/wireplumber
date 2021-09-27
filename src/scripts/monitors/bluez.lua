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
  local desc =
      dev_props["device.description"]
      or dev_props["device.name"]
      or dev_props["device.nick"]
      or dev_props["device.alias"]
      or "bluetooth-device"
  -- sanitize description, replace ':' with ' '
  properties["node.description"] = desc:gsub("(:)", " ")

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

  -- autoconnect if it's a stream
  if properties["api.bluez5.profile"] == "headset-audio-gateway" or
     factory:find("a2dp.source") then
    properties["node.autoconnect"] = true
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
  local device = parent:get_managed_object(id)
  if not device then
    -- ensure a proper device name
    local name =
        (properties["device.name"] or
         properties["api.bluez5.address"] or
         properties["device.description"] or
         tostring(id)):gsub("([^%w_%-%.])", "_")

    if not name:find("^bluez_card%.", 1) then
      name = "bluez_card." .. name
    end
    properties["device.name"] = name

    -- apply properties from config.rules
    rulesApplyProperties(properties)

    -- create the device
    device = SpaDevice(factory, properties)
    device:connect("create-object", createNode)
    parent:store_managed_object(id, device)
  end

  Log.info(parent, string.format("%d, %s (%s): %s",
        id, properties["device.description"],
        properties["api.bluez5.address"], properties["api.bluez5.connection"]))

  -- activate the device after the bluez profiles are connected
  if properties["api.bluez5.connection"] == "connected" then
    device:activate(Feature.SpaDevice.ENABLED | Feature.Proxy.BOUND)
  else
    device:deactivate(Features.ALL)
  end
end

function createMonitor()
  local monitor_props = config.properties or {}
  monitor_props["api.bluez5.connection-info"] = true

  local monitor = SpaDevice("api.bluez5.enum.dbus", monitor_props)
  if monitor then
    monitor:connect("create-object", createDevice)
  else
    Log.message("PipeWire's BlueZ SPA missing or broken. Bluetooth not supported.")
    return nil
  end
  monitor:activate(Feature.SpaDevice.ENABLED)

  return monitor
end

logind_plugin = Plugin.find("logind")
if logind_plugin then
  -- if logind support is enabled, activate
  -- the monitor only when the seat is active
  function startStopMonitor(seat_state)
    Log.info(logind_plugin, "Seat state changed: " .. seat_state)

    if seat_state == "active" then
      monitor = createMonitor()
    elseif monitor then
      monitor:deactivate(Feature.SpaDevice.ENABLED)
      monitor = nil
    end
  end

  logind_plugin:connect("state-changed", function(p, s) startStopMonitor(s) end)
  startStopMonitor(logind_plugin:call("get-state"))
else
  monitor = createMonitor()
end
