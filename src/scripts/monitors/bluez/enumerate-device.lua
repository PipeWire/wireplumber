-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-monitors-bluez")

config = {}
config.seat_monitoring = Core.test_feature ("monitor.bluez.seat-monitoring")
config.properties = Conf.get_section_as_properties ("monitor.bluez.properties")

-- This is not a setting, it must always be enabled
config.properties["api.bluez5.connection-info"] = true

source = nil
logind_plugin = nil
monitor = nil

function createBluezDevice (parent, id, type, factory, properties)
  source = source or Plugin.find ("standard-event-source")

  local e = source:call ("create-event", "create-bluez-device", parent, nil)
  e:set_data ("device-properties", properties)
  e:set_data ("factory", factory)
  e:set_data ("device-sub-id", id)

  log:info ("BT device " .. tostring (id) .. " connected")
  EventDispatcher.push_event (e)
end

function removeBluezDevice(parent, id)
  source = source or Plugin.find ("standard-event-source")

  local e = source:call ("create-event", "remove-bluez-device", parent, nil)
  e:set_data ("device-sub-id", id)

  log:info ("BT device " .. tostring (id) .. " connected")
  EventDispatcher.push_event (e)
end

function createMonitor()
  -- Create monitor
  local m = SpaDevice("api.bluez5.enum.dbus", config.properties)
  if m == nil then
    log:notice("PipeWire's BlueZ SPA plugin is missing or broken. " ..
        "Bluetooth devices will not be supported.")
    return nil
  end

  -- Handle signals
  m:connect("create-object", createBluezDevice)
  m:connect("object-removed", removeBluezDevice)

  -- Activate monitor
  m:activate (Feature.SpaDevice.ENABLED, function (_, e)
    if e ~= nil then
      log:warning ("Failed to activate BT monitor: " .. e)
    else
      log:info ("Created BT monitor")
    end
  end)
  return m
end

-- Find logind plugin if seat-monitoring is enabled
if config.seat_monitoring then
  logind_plugin = Plugin.find("logind")
end

-- If logind plugin was found, only activate the monitor if the seat is active.
-- Otherwise just activate the monitor
if logind_plugin then
  function startStopMonitor(seat_state)
    log:info(logind_plugin, "Seat state changed: " .. seat_state)
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

-- Store set nodes to their respective SPA device
SimpleEventHook {
  name = "monitor/bluez/select-device-for-set-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "api.bluez5.set.leader", "+", type = "pw" },
    },
  },
  execute = function(event)
    local source = event:get_source ()
    local devices_om = source:call ("get-object-manager", "device")
    local node = event:get_subject ()
    local node_bound_id = node["bound-id"]
    local device_id = node.properties:get_int ("device.id")

    log:info ("Device set node added: " .. tostring (node_bound_id))

    for device in devices_om:iterate {
      type = "device",
      Constraint { "object.id", "=", device_id }
    } do
      local spa_device_id = device.properties:get_int ("spa.object.id")
      if spa_device_id == nil then
        goto next_device
      end

      local spa_device = monitor:get_managed_object (spa_device_id)
      if spa_device == nil then
        goto next_device
      end

      local id = node.properties:get_int ("card.profile.device")
      if id ~= nil then
        log:info (".. assign to device: " .. tostring (device["bound-id"]) .. " node " .. tostring (id))
        spa_device:store_managed_object (id, node)
      end

      ::next_device::
    end
  end
}:register ()

-- Check BT loopbacks every time the autoswitch setting changes
Settings.subscribe ("bluetooth.autoswitch-to-headset-profile", function ()
  source = source or Plugin.find ("standard-event-source")
  for spa_device in monitor:iterate_managed_objects() do
    local e = source:call ("create-event", "create-bluez-device-loopback-node", spa_device, nil)
    EventDispatcher.push_event (e)
  end
end)
