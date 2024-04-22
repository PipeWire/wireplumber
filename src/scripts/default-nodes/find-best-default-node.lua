-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-default-nodes")

nutils = require ("node-utils")

SimpleEventHook {
  name = "default-nodes/find-best-default-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-node" },
    },
  },
  execute = function (event)
    local available_nodes = event:get_data ("available-nodes")
    local selected_prio = event:get_data ("selected-node-priority") or 0
    local selected_node = event:get_data ("selected-node")

    available_nodes = available_nodes and available_nodes:parse ()
    if not available_nodes then
      return
    end

    for _, node_props in ipairs (available_nodes) do
      -- Highest priority node wins
      local priority = nutils.get_session_priority (node_props)

      if priority > selected_prio or selected_node == nil then
        selected_prio = priority
        selected_node = node_props ["node.name"]
      end
    end

    event:set_data ("selected-node-priority", selected_prio)
    event:set_data ("selected-node", selected_node)
  end
}:register ()
