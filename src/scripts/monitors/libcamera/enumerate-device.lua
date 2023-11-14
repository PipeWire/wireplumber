-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-monitors-libcamera")

defaults = {}
defaults.properties = Json.Object {}

config = {}
config.properties = Conf.get_section (
  "monitor.libcamera.properties", defaults.properties):parse ()

function createCamDevice (parent, id, type, factory, properties)
  source = source or Plugin.find ("standard-event-source")

  local e = source:call ("create-event", "create-libcamera-device", parent, nil)
  e:set_data ("device-properties", properties)
  e:set_data ("factory", factory)
  e:set_data ("device-sub-id", id)

  EventDispatcher.push_event (e)
end

monitor = SpaDevice ("api.libcamera.enum.manager", config.properties)
if monitor then
  monitor:connect ("create-object", createCamDevice)
  monitor:activate (Feature.SpaDevice.ENABLED)
else
  log:notice ("PipeWire's libcamera SPA missing or broken. libcamera not supported.")
end
