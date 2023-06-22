-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- hook to make sure the user prefered device(default.configured.*) in other
-- words currently selected device is given higher priority

-- state-default-nodes.lua also does find out the default node out of the user
-- preferences(current and past), however it doesnt give any higher priority to
-- the currently selected device.

log = Log.open_topic ("s-default-nodes")

SimpleEventHook {
  name = "default-nodes/find-selected-default-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-node" },
    },
  },
  execute = function (event)
    local available_nodes = event:get_data ("available-nodes")

    available_nodes = available_nodes and available_nodes:parse ()
    if not available_nodes then
      return
    end

    local selected_prio = event:get_data ("selected-node-priority") or 0
    local selected_node = event:get_data ("selected-node")

    local source = event:get_source ()
    local props = event:get_properties ()
    local def_node_type = props ["default-node.type"]
    local metadata_om = source:call ("get-object-manager", "metadata")
    local metadata = metadata_om:lookup { Constraint { "metadata.name", "=", "default" } }
    local obj = metadata:find (0, "default.configured." .. def_node_type)

    if not obj then
      return
    end

    local json = Json.Raw (obj)
    local current_configured_node = json:parse ().name

    for _, node_props in ipairs (available_nodes) do
      local name = node_props ["node.name"]
      local priority = node_props ["priority.session"]
      priority = math.tointeger (priority) or 0

      if current_configured_node == name then
        priority = 30000 + priority

        if priority > selected_prio then

          selected_prio = priority
          selected_node = name

          event:set_data ("selected-node-priority", selected_prio)
          event:set_data ("selected-node", selected_node)
        end

        break
      end
    end
  end
}:register ()
