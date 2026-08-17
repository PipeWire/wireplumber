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

AsyncEventHook {
  name = "monitor/libcamera/create-node",
  after = "monitor/libcamera/name-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-libcamera-device-node" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local properties = event:get_data ("node-properties")
        local parent = event:get_subject ()
        local id = event:get_data ("node-sub-id")

        -- apply properties from rules defined in JSON .conf file
        properties = JsonUtils.match_rules_update_properties (config.rules, properties)

        if cutils.parseBool (properties["node.disabled"]) then
          log:notice ("libcam node " .. properties ["node.name"] .. " disabled")
          transition:advance ()
          return
        end

        -- create the node
        local node = LocalNode ("spa-node-factory", properties)
        node:activate (Feature.Proxy.BOUND, function (n, e)
          if e ~= nil then
            transition:return_error ("Failed to activate libcamera node " ..
                tostring (properties ["node.name"]) .. ": " .. e)
            return
          end

          parent:store_managed_object (id, node)
          transition:advance ()
        end)
      end
    },
  }
}:register ()
