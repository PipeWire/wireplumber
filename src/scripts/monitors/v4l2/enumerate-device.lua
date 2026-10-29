-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")
mutils = require ("monitor-utils")
log = Log.open_topic ("s-monitors-v4l2")

config = {}
config.seat_monitoring = Core.test_feature ("monitor.camera.seat-monitoring")
config.properties = Conf.get_section_as_properties ("monitor.v4l2.properties")

logind_plugin = nil
monitor = nil

function createCamDevice (parent, id, type, factory, properties)
  source = source or Plugin.find ("standard-event-source")

  local e = source:call ("create-event", "create-v4l2-device", parent, nil)
  e:set_data ("device-properties", properties)
  e:set_data ("factory", factory)
  e:set_data ("device-sub-id", id)

  EventDispatcher.push_event (e)
end

function createMonitor ()
  local m = SpaDevice ("api.v4l2.enum.udev", config.properties)
  if m == nil then
    log:notice ("PipeWire's V4L2 SPA plugin is missing or broken. " ..
        "Some camera types may not be supported.")
    return nil
  end

  m:connect ("create-object", createCamDevice)
  m:activate (Feature.SpaDevice.ENABLED)
  return m
end

-- Find logind plugin if seat-monitoring is enabled
if config.seat_monitoring then
  logind_plugin = Plugin.find ("logind")
end

-- If logind plugin was found, only activate the monitor if the seat is active.
-- Otherwise just activate the monitor
if logind_plugin then
  function startStopMonitor (seat_state)
    log:info (logind_plugin, "Seat state changed: " .. seat_state)
    if seat_state == "active" then
      monitor = createMonitor ()
    elseif monitor then
      monitor:deactivate (Feature.SpaDevice.ENABLED)
      monitor = nil
      mutils:clear_cam_data ("v4l2")
    end
  end

  logind_plugin:connect ("state-changed", function (p, s) startStopMonitor (s) end)
  startStopMonitor (logind_plugin:call ("get-state"))
else
  monitor = createMonitor ()
end
