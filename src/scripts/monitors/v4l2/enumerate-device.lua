-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")
log = Log.open_topic ("s-monitors-v4l2")

config = {}
config.properties = cutils.get_config_section ("monitor.v4l2.properties")

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
  log:notice ("PipeWire's V4L2 SPA plugin is missing or broken. " ..
      "Some camera types may not be supported.")
end
