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
  mutils:register_cam_node (parent, id, factory, properties)
end

AsyncEventHook {
  name = "monitor/libcamera/create-device",
  after = "monitor/libcamera/name-device",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-libcamera-device" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local properties = event:get_data ("device-properties")
        local factory = event:get_data ("factory")
        local parent = event:get_subject ()
        local id = event:get_data ("device-sub-id")

        -- apply properties from rules defined in JSON .conf file
        properties = JsonUtils.match_rules_update_properties (config.rules, properties)

        if cutils.parseBool (properties ["device.disabled"]) then
          log:notice ("libcamera device " .. properties["device.name"] .. " disabled")
          transition:advance ()
          return
        end

        -- create the device
        local device = SpaDevice (factory, properties)
        if device == nil then
          transition:return_error ("Failed to create '" .. factory .. "' device")
          return
        end

        -- handle signals
        device:connect ("create-object", createLibcamNode)

        -- activate the device
        device:activate (Features.ALL, function (d, e)
          if e ~= nil then
            transition:return_error ("Failed to activate libcamera device " ..
                tostring (properties ["device.name"]) .. ": " .. e)
            return
          end

          parent:store_managed_object (id, device)
          transition:advance ()
        end)
      end
    }
  }
}:register ()
