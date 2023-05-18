-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

local cutils = require ("common-utils")

local defaults = {}
defaults.properties = Json.Object {}

local config = {}
config.properties = Conf.get_section (
    "monitor.libcamera.properties", defaults.properties): parse ()

function findDuplicate(parent, id, property, value)
  for i = 0, id - 1, 1 do
    local obj = parent:get_managed_object(i)
    if obj and obj.properties[property] == value then
      return true
    end
  end
  return false
end

function createNode(parent, id, type, factory, properties)
  local dev_props = parent.properties
  local location = properties["api.libcamera.location"]

  -- set the device id and spa factory name; REQUIRED, do not change
  properties["device.id"] = parent["bound-id"]
  properties["factory.name"] = factory

  -- set the default pause-on-idle setting
  properties["node.pause-on-idle"] = false

  -- set the node name
  local name =
      (factory:find("sink") and "libcamera_output") or
       (factory:find("source") and "libcamera_input" or factory)
      .. "." ..
      (dev_props["device.name"]:gsub("^libcamera_device%.(.+)", "%1") or
       dev_props["device.name"] or
       dev_props["device.nick"] or
       dev_props["device.alias"] or
       "libcamera-device")
  -- sanitize name
  name = name:gsub("([^%w_%-%.])", "_")

  properties["node.name"] = name

  -- deduplicate nodes with the same name
  for counter = 2, 99, 1 do
    if findDuplicate(parent, id, "node.name", properties["node.name"]) then
      properties["node.name"] = name .. "." .. counter
    else
      break
    end
  end

  -- set the node description
  local desc = dev_props["device.description"] or "libcamera-device"
  if location == "front" then
    desc = I18n.gettext("Built-in Front Camera")
  elseif location == "back" then
    desc = I18n.gettext("Built-in Back Camera")
  end
  -- sanitize description, replace ':' with ' '
  properties["node.description"] = desc:gsub("(:)", " ")

  -- set the node nick
  local nick = properties["node.nick"] or
               dev_props["device.product.name"] or
               dev_props["device.description"] or
               dev_props["device.nick"]
  properties["node.nick"] = nick:gsub("(:)", " ")

  -- set priority
  if not properties["priority.session"] then
    local priority = 700
    if location == "external" then
      priority = priority + 150
    elseif location == "front" then
      priority = priority + 100
    elseif location == "back" then
      priority = priority + 50
    end
    properties["priority.session"] = priority
  end

  -- apply properties from rules defined in JSON .conf file
  cutils.evaluateRulesApplyProperties (properties, "monitor.libcamera.rules")
  if properties ["node.disabled"] then
    return
  end

  -- create the node
  local node = Node("spa-node-factory", properties)
  node:activate(Feature.Proxy.BOUND)
  parent:store_managed_object(id, node)
end

function createDevice(parent, id, type, factory, properties)
  -- ensure the device has an appropriate name
  local name = "libcamera_device." ..
      (properties["device.name"] or
       properties["device.bus-id"] or
       properties["device.bus-path"] or
       tostring(id)):gsub("([^%w_%-%.])", "_")

  properties["device.name"] = name

  -- deduplicate devices with the same name
  for counter = 2, 99, 1 do
    if findDuplicate(parent, id, "device.name", properties["device.name"]) then
      properties["device.name"] = name .. "." .. counter
    else
      break
    end
  end

  -- ensure the device has a description
  properties["device.description"] =
      properties["device.description"]
      or properties["device.product.name"]
      or "Unknown device"

  -- apply properties from rules defined in JSON .conf file
  cutils.evaluateRulesApplyProperties (properties, "monitor.libcamera.rules")
  if properties ["device.disabled"] then
    return
  end

  -- create the device
  local device = SpaDevice(factory, properties)
  if device then
    device:connect("create-object", createNode)
    device:activate(Feature.SpaDevice.ENABLED | Feature.Proxy.BOUND)
    parent:store_managed_object(id, device)
  else
    Log.warning ("Failed to create '" .. factory .. "' device")
  end
end

monitor = SpaDevice("api.libcamera.enum.manager", config.properties)
if monitor then
  monitor:connect("create-object", createDevice)
  monitor:activate(Feature.SpaDevice.ENABLED)
else
  Log.notice("PipeWire's libcamera SPA missing or broken. libcamera not supported.")
end
