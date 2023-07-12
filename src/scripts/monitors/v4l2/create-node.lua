-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT

local cutils = require ("common-utils")
local mutils = require ("monitor-utils")

log = Log.open_topic ("s-monitors-v4l2")

SimpleEventHook {
  name = "monitor/v4l2/create-node",
  after = "monitor/v4l2/name-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-v4l2-device-node" },
    },
  },
  execute = function(event)
    local properties = event:get_data ("node-properties")
    local parent = event:get_subject ()
    local id = event:get_data ("node-sub-id")
    local factory = event:get_data ("factory")

    -- apply properties from rules defined in JSON .conf file
    cutils.evaluateRulesApplyProperties (properties, "monitor.v4l2.rules")
    if properties["node.disabled"] then
      log:warning ("v4l2 device node" .. properties["device.name"] .. " disabled")
      return
    end
    -- create the node
    local node = Node ("spa-node-factory", properties)
    node:activate (Feature.Proxy.BOUND)
    parent:store_managed_object (id, node)
  end
}:register ()
