-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")
mutils = require ("monitor-utils")

log = Log.open_topic ("s-monitors-libcamera")

config = {}
config.rules = Conf.get_section_as_json ("monitor.libcamera.rules", Json.Array {})

function createLibcamNode (parent, id, type, factory, properties)
  local registered = mutils:register_cam_node (parent, id, factory, properties)
  if not registered then
    source = source or Plugin.find ("standard-event-source")
    local e = source:call ("create-event", "create-libcamera-device-node",
      parent, nil)
    e:set_data ("factory", factory)
    e:set_data ("node-properties", properties)
    e:set_data ("node-sub-id", id)

    EventDispatcher.push_event (e)
  end
end

SimpleEventHook {
  name = "monitor/libcamera/create-device",
  after = "monitor/libcamera/name-device",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-libcamera-device" },
    },
  },
  execute = function(event)
    local properties = event:get_data ("device-properties")
    local factory = event:get_data ("factory")
    local parent = event:get_subject ()
    local id = event:get_data ("device-sub-id")

    -- apply properties from rules defined in JSON .conf file
    properties = JsonUtils.match_rules_update_properties (config.rules, properties)

    if cutils.parseBool (properties ["device.disabled"]) then
      log:notice ("libcam device " .. properties["device.name"] .. " disabled")
      return
    end
    local device = SpaDevice (factory, properties)

    if device then
      device:connect ("create-object", createLibcamNode)
      device:activate (Feature.SpaDevice.ENABLED | Feature.Proxy.BOUND)
      parent:store_managed_object (id, device)
    else
      log:warning ("Failed to create '" .. factory .. "' device")
    end
  end
}:register ()
