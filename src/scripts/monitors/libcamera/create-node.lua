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

SimpleEventHook {
  name = "monitor/libcamera/create-node",
  after = "monitor/libcamera/name-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-libcamera-device-node" },
    },
  },
  execute = function(event)
    local properties = event:get_data ("node-properties")
    local parent = event:get_subject ()
    local id = event:get_data ("node-sub-id")

    -- apply properties from rules defined in JSON .conf file
    properties = JsonUtils.match_rules_update_properties (config.rules, properties)

    if properties["node.disabled"] then
      log:warning ("lib cam device node" .. properties["device.name"] .. " disabled")
      return
    end
    -- create the node
    local node = Node ("spa-node-factory", properties)
    node:activate (Feature.Proxy.BOUND)
    parent:store_managed_object (id, node)
  end
}:register ()
