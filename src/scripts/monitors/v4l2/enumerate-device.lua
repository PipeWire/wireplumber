-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT
log = Log.open_topic ("s-monitors-v4l2")

local defaults = {}
defaults.properties = Json.Object {}

local config = {}
config.properties = Conf.get_section (
  "monitor.v4l2.properties", defaults.properties):parse ()


function createCamDevice (parent, id, type, factory, properties)
  source = source or Plugin.find ("standard-event-source")

  local e = source:call ("create-event", "create-v4l2-device", parent, nil)
  e:set_data ("device-properties", properties)
  e:set_data ("factory", factory)
  e:set_data ("device-sub-id", id)

  EventDispatcher.push_event (e)
end

monitor = SpaDevice ("api.v4l2.enum.udev", config.properties)
if monitor then
  monitor:connect ("create-object", createCamDevice)
  monitor:activate (Feature.SpaDevice.ENABLED)
else
  log:notice ("PipeWire's V4L SPA missing or broken. Video4Linux not supported.")
end
